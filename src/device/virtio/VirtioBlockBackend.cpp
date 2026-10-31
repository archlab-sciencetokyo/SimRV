/**
 * @file VirtioBlockBackend.cpp
 * @brief Implementation of overlay-backed VirtIO block storage backend.
 */
#include <fcntl.h>
#if defined(__linux__)
#include <linux/falloc.h>
#include <sys/syscall.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <span>

#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device::virtio {

BlockBackend::BlockBackend(const std::string& path) {
    if (!path.empty()) {
        load_disk(path);
    }
}

auto BlockBackend::load_disk(const std::string& path) -> bool {
    base_fd_.reset();
    overlay_fd_.reset();
    dirty_sectors_.clear();
    zero_sectors_.clear();
    size_bytes_ = 0;

    base_fd_.reset(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!base_fd_) return false;

    struct stat status{};
    if (::fstat(base_fd_.get(), &status) != 0 || status.st_size < 0) {
        base_fd_.reset();
        return false;
    }
    size_bytes_ = static_cast<uint64_t>(status.st_size);

    char temp_path[] = "/tmp/simrv-disk-overlay-XXXXXX";
    const int temp_fd = ::mkstemp(temp_path);
    if (temp_fd < 0) {
        base_fd_.reset();
        size_bytes_ = 0;
        return false;
    }
    (void)::unlink(temp_path);
    overlay_fd_.reset(temp_fd);
    const uint64_t sectors = capacity_sectors();
    dirty_sectors_.resize(static_cast<size_t>((sectors + 63) / 64), 0);
    zero_sectors_.resize(static_cast<size_t>((sectors + 63) / 64), 0);
    return true;
}

auto BlockBackend::read_sectors(uint64_t sector, std::span<std::byte> dst) -> bool {
    if (!is_loaded() || sector > std::numeric_limits<uint64_t>::max() / 512ULL) return false;
    const uint64_t offset = sector * 512ULL;
    if (offset > size_bytes_ || dst.size() > size_bytes_ - offset) return false;

    size_t done = 0;
    while (done < dst.size()) {
        const uint64_t current = offset + done;
        const uint64_t current_sector = current / 512ULL;
        const auto source = sector_source(current_sector);
        size_t run = std::min(dst.size() - done, static_cast<size_t>(512ULL - current % 512ULL));
        while (run < dst.size() - done) {
            const uint64_t next_sector = (current + run) / 512ULL;
            if (sector_source(next_sector) != source) break;
            const size_t extend = std::min(dst.size() - done - run, size_t{512});
            run += extend;
        }
        if (source == SectorSource::Zero) {
            std::memset(dst.data() + done, 0, run);
        } else {
            const int fd = source == SectorSource::Overlay ? overlay_fd_.get() : base_fd_.get();
            if (!pread_exact(fd, dst.data() + done, run, current)) return false;
        }
        done += run;
    }
    return true;
}

auto BlockBackend::read_sectors(uint64_t sector, std::byte* dst, std::size_t len) -> bool {
    return read_sectors(sector, std::span<std::byte>(dst, len));
}

auto BlockBackend::write_sectors(uint64_t sector, std::span<const std::byte> src) -> bool {
    if (!is_loaded() || sector > std::numeric_limits<uint64_t>::max() / 512ULL) return false;
    if (src.size() % 512ULL != 0) return false;
    const uint64_t offset = sector * 512ULL;
    if (offset > size_bytes_ || src.size() > size_bytes_ - offset) return false;
    if (!pwrite_exact(overlay_fd_.get(), src.data(), src.size(), offset)) return false;
    if (src.empty()) return true;
    const uint64_t first_sector = offset / 512ULL;
    const uint64_t last_sector = (offset + src.size() - 1) / 512ULL;
    for (uint64_t current = first_sector; current <= last_sector; ++current) {
        dirty_sectors_[static_cast<size_t>(current / 64)] |= 1ULL << (current % 64);
        zero_sectors_[static_cast<size_t>(current / 64)] &= ~(1ULL << (current % 64));
    }
    return true;
}

auto BlockBackend::discard_sectors(uint64_t sector, uint32_t count) -> bool {
    if (!valid_range(sector, count) || count > kMaxRangeSectors) return false;
    if (count == 0) return true;
    const uint64_t offset = sector * 512ULL;
    const uint64_t bytes = static_cast<uint64_t>(count) * 512ULL;
    (void)punch_overlay_hole(offset, bytes);
    for (uint64_t current = sector; current < sector + count; ++current) {
        const auto index = static_cast<size_t>(current / 64);
        const uint64_t mask = 1ULL << (current % 64);
        dirty_sectors_[index] &= ~mask;
        zero_sectors_[index] &= ~mask;
    }
    return true;
}

auto BlockBackend::write_zeroes(uint64_t sector, uint32_t count) -> bool {
    if (!valid_range(sector, count) || count > kMaxRangeSectors) return false;
    if (count == 0) return true;
    const uint64_t offset = sector * 512ULL;
    const uint64_t bytes = static_cast<uint64_t>(count) * 512ULL;
    if (!punch_overlay_hole(offset, bytes)) {
        constexpr size_t kChunkSize = 64 * 1024;
        const std::array<std::byte, kChunkSize> zeroes{};
        uint64_t done = 0;
        while (done < bytes) {
            const auto chunk = static_cast<size_t>(std::min<uint64_t>(kChunkSize, bytes - done));
            if (!pwrite_exact(overlay_fd_.get(), zeroes.data(), chunk, offset + done)) return false;
            done += chunk;
        }
    }
    for (uint64_t current = sector; current < sector + count; ++current) {
        const auto index = static_cast<size_t>(current / 64);
        const uint64_t mask = 1ULL << (current % 64);
        dirty_sectors_[index] &= ~mask;
        zero_sectors_[index] |= mask;
    }
    return true;
}

auto BlockBackend::write_sectors(uint64_t sector, const std::byte* src, std::size_t len) -> bool {
    return write_sectors(sector, std::span<const std::byte>(src, len));
}

auto BlockBackend::flush() -> bool { return overlay_fd_ && ::fsync(overlay_fd_.get()) == 0; }

auto BlockBackend::sector_source(uint64_t sector) const -> SectorSource {
    const auto index = static_cast<size_t>(sector / 64);
    const uint64_t mask = 1ULL << (sector % 64);
    if ((zero_sectors_[index] & mask) != 0) return SectorSource::Zero;
    if ((dirty_sectors_[index] & mask) != 0) return SectorSource::Overlay;
    return SectorSource::Base;
}

auto BlockBackend::valid_range(uint64_t sector, uint32_t count) const -> bool {
    return is_loaded() && sector <= capacity_sectors() &&
           static_cast<uint64_t>(count) <= capacity_sectors() - sector;
}

auto BlockBackend::punch_overlay_hole(uint64_t offset, uint64_t bytes) const -> bool {
#if defined(__linux__) && defined(SYS_fallocate)
    return ::syscall(SYS_fallocate, overlay_fd_.get(), FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                     static_cast<off_t>(offset), static_cast<off_t>(bytes)) == 0;
#else
    (void)offset;
    (void)bytes;
    return false;
#endif
}

auto BlockBackend::pread_exact(int fd, std::byte* dst, size_t size, uint64_t offset) -> bool {
    size_t done = 0;
    while (done < size) {
        const auto result = ::pread(fd, dst + done, size - done, static_cast<off_t>(offset + done));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        done += static_cast<size_t>(result);
    }
    return true;
}

auto BlockBackend::pwrite_exact(int fd, const std::byte* src, size_t size, uint64_t offset)
    -> bool {
    size_t done = 0;
    while (done < size) {
        const auto result =
            ::pwrite(fd, src + done, size - done, static_cast<off_t>(offset + done));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        done += static_cast<size_t>(result);
    }
    return true;
}

}  // namespace simrv::device::virtio
