/** @file VirtioFsBackend.cpp */
#include "simrv/device/virtio/VirtioFsBackend.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/fuse.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <type_traits>
#include <utility>

namespace simrv::device::virtio {
namespace {

template <typename T>
void append(std::vector<std::byte>& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* data = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), data, data + sizeof(T));
}

template <typename T>
auto input_at(std::span<const std::byte> input, size_t offset, T& value) -> bool {
    if (offset > input.size() || sizeof(T) > input.size() - offset) return false;
    std::memcpy(&value, input.data() + offset, sizeof(T));
    return true;
}

auto string_at(std::span<const std::byte> input, size_t offset) -> std::optional<std::string> {
    if (offset >= input.size()) return std::nullopt;
    const auto* begin = reinterpret_cast<const char*>(input.data() + offset);
    const size_t length = input.size() - offset;
    const auto* end = static_cast<const char*>(std::memchr(begin, '\0', length));
    if (end == nullptr) return std::nullopt;
    return std::string(begin, end);
}

auto fuse_attr_from_stat(const struct stat& st) -> fuse_attr {
    fuse_attr attr{};
    attr.ino = static_cast<uint64_t>(st.st_ino);
    attr.size = static_cast<uint64_t>(st.st_size);
    attr.blocks = static_cast<uint64_t>(st.st_blocks);
    attr.atime = static_cast<uint64_t>(st.st_atim.tv_sec);
    attr.mtime = static_cast<uint64_t>(st.st_mtim.tv_sec);
    attr.ctime = static_cast<uint64_t>(st.st_ctim.tv_sec);
    attr.atimensec = static_cast<uint32_t>(st.st_atim.tv_nsec);
    attr.mtimensec = static_cast<uint32_t>(st.st_mtim.tv_nsec);
    attr.ctimensec = static_cast<uint32_t>(st.st_ctim.tv_nsec);
    attr.mode = static_cast<uint32_t>(st.st_mode);
    attr.nlink = static_cast<uint32_t>(st.st_nlink);
    attr.uid = static_cast<uint32_t>(st.st_uid);
    attr.gid = static_cast<uint32_t>(st.st_gid);
    attr.rdev = static_cast<uint32_t>(st.st_rdev);
    attr.blksize = static_cast<uint32_t>(st.st_blksize);
    return attr;
}

}  // namespace

VirtioFsBackend::VirtioFsBackend(std::string tag) : tag_(std::move(tag)) {
    if (tag_.size() > 36) tag_.resize(36);
    nodes_.emplace(FUSE_ROOT_ID, std::filesystem::path{});
    node_lookups_.emplace(FUSE_ROOT_ID, 1);
}

VirtioFsBackend::~VirtioFsBackend() = default;

auto VirtioFsBackend::set_shared_directory(const std::filesystem::path& path) -> bool {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) return false;
    util::UniqueFd fd(::open(canonical.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!fd) return false;
    std::scoped_lock lock(mutex_);
    handles_.clear();
    nodes_.clear();
    nodes_.emplace(FUSE_ROOT_ID, std::filesystem::path{});
    node_lookups_.clear();
    node_lookups_.emplace(FUSE_ROOT_ID, 1);
    root_fd_ = std::move(fd);
    return true;
}

auto VirtioFsBackend::path_for(uint64_t nodeid) const -> std::optional<std::filesystem::path> {
    const auto it = nodes_.find(nodeid);
    if (it == nodes_.end()) return std::nullopt;
    return it->second;
}

auto VirtioFsBackend::open_beneath(const std::filesystem::path& relative, int flags,
                                   uint32_t mode) const -> util::UniqueFd {
    if (!root_fd_ || relative.is_absolute()) return {};
    for (const auto& component : relative) {
        if (component == "..") {
            errno = EACCES;
            return {};
        }
    }
    const std::string path = relative.empty() ? "." : relative.generic_string();
    open_how how{};
    how.flags = static_cast<uint64_t>(flags | O_CLOEXEC);
    how.mode = mode;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS;
    int fd =
        static_cast<int>(::syscall(SYS_openat2, root_fd_.get(), path.c_str(), &how, sizeof(how)));
    if (fd >= 0) return util::UniqueFd(fd);
    if (errno != ENOSYS && errno != EINVAL && errno != E2BIG) return {};

    // Older kernels use a symlink-rejecting component walk as a safe fallback.
    util::UniqueFd current(::dup(root_fd_.get()));
    if (!current) return {};
    if (relative.empty()) {
        const int result = ::openat(current.get(), ".", flags | O_CLOEXEC | O_NOFOLLOW, mode);
        return util::UniqueFd(result);
    }
    std::vector<std::string> components;
    for (const auto& component : relative) {
        if (component.empty() || component == ".") continue;
        components.push_back(component.string());
    }
    if (components.empty()) {
        const int result = ::openat(current.get(), ".", flags | O_CLOEXEC | O_NOFOLLOW, mode);
        return util::UniqueFd(result);
    }
    for (size_t i = 0; i + 1 < components.size(); ++i) {
        util::UniqueFd next(::openat(current.get(), components[i].c_str(),
                                     O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (!next) return {};
        current = std::move(next);
    }
    return util::UniqueFd(
        ::openat(current.get(), components.back().c_str(), flags | O_NOFOLLOW | O_CLOEXEC, mode));
}

auto VirtioFsBackend::allocate_node(const std::filesystem::path& relative) -> uint64_t {
    for (const auto& [id, existing] : nodes_) {
        if (existing == relative) {
            ++node_lookups_[id];
            return id;
        }
    }
    uint64_t id = 2;
    while (nodes_.contains(id)) ++id;
    nodes_.emplace(id, relative);
    node_lookups_.emplace(id, 1);
    return id;
}

void VirtioFsBackend::process_queue(QueueState& queue, const DmaRead& dma_read,
                                    const DmaWrite& dma_write, const Interrupt& interrupt) {
    if (queue.ready == 0 || queue.num == 0 || queue.desc_addr == 0 || queue.driver_addr == 0 ||
        queue.device_addr == 0)
        return;
    uint16_t avail_idx = 0;
    if (!dma_read(queue.driver_addr + 2, &avail_idx, sizeof(avail_idx))) return;
    bool did_work = false;
    while (queue.last_avail_idx != avail_idx) {
        uint16_t head = 0;
        const auto avail_slot = static_cast<uint64_t>(queue.last_avail_idx % queue.num);
        if (!dma_read(queue.driver_addr + 4 + avail_slot * sizeof(head), &head, sizeof(head)))
            break;

        std::vector<std::byte> request;
        std::vector<virtio::VirtqDesc> outputs;
        uint16_t index = head;
        bool valid = true;
        for (size_t n = 0; n < queue.num; ++n) {
            virtio::VirtqDesc desc{};
            if (!dma_read(queue.desc_addr + static_cast<uint64_t>(index) * sizeof(desc), &desc,
                          sizeof(desc))) {
                valid = false;
                break;
            }
            if ((desc.flags & virtio::kVirtqDescFWrite) != 0) {
                outputs.push_back(desc);
            } else if (desc.len != 0) {
                if (request.size() > 1024 * 1024 || desc.len > 1024 * 1024 - request.size()) {
                    valid = false;
                    break;
                }
                const auto old_size = request.size();
                request.resize(old_size + desc.len);
                if (!dma_read(desc.addr, request.data() + old_size, desc.len)) {
                    valid = false;
                    break;
                }
            }
            if ((desc.flags & virtio::kVirtqDescFNext) == 0) break;
            index = desc.next;
            if (n + 1 == queue.num) valid = false;
        }
        std::vector<std::byte> response;
        if (valid && !request.empty()) response = process(request);
        size_t written = 0;
        for (const auto& desc : outputs) {
            if (written >= response.size()) break;
            const size_t count = std::min<size_t>(desc.len, response.size() - written);
            if (!dma_write(desc.addr, response.data() + written, count)) {
                valid = false;
                break;
            }
            written += count;
        }

        uint16_t used_idx = 0;
        if (!dma_read(queue.device_addr + 2, &used_idx, sizeof(used_idx))) break;
        const virtio::VirtqUsedElem elem{head, static_cast<uint32_t>(written)};
        const auto used_slot = static_cast<uint64_t>(used_idx % queue.num);
        if (!dma_write(queue.device_addr + 4 + used_slot * sizeof(elem), &elem, sizeof(elem)))
            break;
        ++used_idx;
        if (!dma_write(queue.device_addr + 2, &used_idx, sizeof(used_idx))) break;
        ++queue.last_avail_idx;
        did_work = true;
        (void)valid;
    }
    if (did_work && interrupt) interrupt();
}

auto VirtioFsBackend::process(std::span<const std::byte> request) -> std::vector<std::byte> {
    std::scoped_lock lock(mutex_);
    if (request.size() < sizeof(fuse_in_header)) return {};
    fuse_in_header in{};
    std::memcpy(&in, request.data(), sizeof(in));
    if (in.len < sizeof(in) || in.len > request.size()) return {};

    std::vector<std::byte> reply(sizeof(fuse_out_header));
    fuse_out_header out{static_cast<uint32_t>(sizeof(out)), 0, in.unique};
    auto set_error = [&](int error) { out.error = -error; };
    auto emit = [&](const auto& value) {
        append(reply, value);
        out.len = static_cast<uint32_t>(reply.size());
    };
    auto target = [&](uint64_t nodeid) -> std::optional<std::filesystem::path> {
        return path_for(nodeid);
    };
    auto entry = [&](const std::filesystem::path& relative, fuse_entry_out& result) -> bool {
        auto fd = open_beneath(relative, O_PATH);
        if (!fd) return false;
        struct stat st{};
        if (::fstat(fd.get(), &st) != 0) return false;
        result = {};
        result.nodeid = allocate_node(relative);
        result.generation = 1;
        result.entry_valid = 1;
        result.attr_valid = 1;
        result.attr = fuse_attr_from_stat(st);
        result.attr.ino = result.nodeid;
        return true;
    };
    const auto payload = request.subspan(sizeof(in), in.len - sizeof(in));

    if (!root_fd_) {
        set_error(ENODEV);
    } else {
        switch (in.opcode) {
            case FUSE_INIT: {
                fuse_init_in init_request{};
                if (!input_at(payload, 0, init_request)) {
                    set_error(EINVAL);
                    break;
                }
                fuse_init_out init{};
                init.major = FUSE_KERNEL_VERSION;
                init.minor =
                    std::min(init_request.minor, static_cast<uint32_t>(FUSE_KERNEL_MINOR_VERSION));
                init.max_readahead = 128 * 1024;
                init.max_write = 128 * 1024;
                init.max_background = 64;
                init.congestion_threshold = 48;
                emit(init);
                break;
            }
            case FUSE_LOOKUP: {
                auto name = string_at(payload, 0);
                auto parent = target(in.nodeid);
                if (!name || !parent || name->empty() || name->find('/') != std::string::npos ||
                    *name == "." || *name == "..") {
                    set_error(EINVAL);
                    break;
                }
                const auto relative = *parent / *name;
                fuse_entry_out result{};
                if (!entry(relative, result))
                    set_error(errno == 0 ? ENOENT : errno);
                else
                    emit(result);
                break;
            }
            case FUSE_GETATTR: {
                auto full = target(in.nodeid);
                struct stat st{};
                if (!full) {
                    set_error(ENOENT);
                    break;
                }
                auto fd = open_beneath(*full, O_PATH);
                if (!fd || ::fstat(fd.get(), &st) != 0) {
                    set_error(errno);
                    break;
                }
                fuse_attr_out result{};
                result.attr_valid = 1;
                result.attr = fuse_attr_from_stat(st);
                result.attr.ino = in.nodeid;
                emit(result);
                break;
            }
            case FUSE_OPEN:
            case FUSE_OPENDIR: {
                fuse_open_in op{};
                if (!input_at(payload, 0, op)) {
                    set_error(EINVAL);
                    break;
                }
                auto full = target(in.nodeid);
                if (!full) {
                    set_error(ENOENT);
                    break;
                }
                const bool directory = in.opcode == FUSE_OPENDIR;
                int flags =
                    (op.flags & (O_ACCMODE | O_APPEND | O_TRUNC | O_NONBLOCK | O_SYNC | O_DSYNC)) |
                    O_CLOEXEC;
                if (directory) flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
                util::UniqueFd fd = open_beneath(*full, flags);
                if (!fd) {
                    set_error(errno);
                    break;
                }
                const uint64_t handle = static_cast<uint64_t>(fd.get());
                handles_.insert_or_assign(handle, std::move(fd));
                fuse_open_out result{};
                result.fh = handle;
                emit(result);
                break;
            }
            case FUSE_READ: {
                fuse_read_in read{};
                if (!input_at(payload, 0, read)) {
                    set_error(EINVAL);
                    break;
                }
                const auto it = handles_.find(read.fh);
                if (it == handles_.end()) {
                    set_error(EBADF);
                    break;
                }
                const size_t count = std::min<size_t>(read.size, 1024 * 1024);
                std::vector<std::byte> data(count);
                const auto n =
                    ::pread(it->second.get(), data.data(), count, static_cast<off_t>(read.offset));
                if (n < 0) {
                    set_error(errno);
                    break;
                }
                data.resize(static_cast<size_t>(n));
                reply.insert(reply.end(), data.begin(), data.end());
                out.len = static_cast<uint32_t>(reply.size());
                break;
            }
            case FUSE_WRITE: {
                fuse_write_in write{};
                if (!input_at(payload, 0, write) || write.size > payload.size() - sizeof(write)) {
                    set_error(EINVAL);
                    break;
                }
                const auto it = handles_.find(write.fh);
                if (it == handles_.end()) {
                    set_error(EBADF);
                    break;
                }
                const auto* data = payload.data() + sizeof(write);
                const auto n =
                    ::pwrite(it->second.get(), data, write.size, static_cast<off_t>(write.offset));
                if (n < 0) {
                    set_error(errno);
                    break;
                }
                fuse_write_out result{static_cast<uint32_t>(n), 0};
                emit(result);
                break;
            }
            case FUSE_RELEASE:
            case FUSE_RELEASEDIR: {
                fuse_release_in release{};
                if (!input_at(payload, 0, release)) {
                    set_error(EINVAL);
                    break;
                }
                handles_.erase(release.fh);
                break;
            }
            case FUSE_FLUSH: {
                fuse_flush_in flush{};
                if (!input_at(payload, 0, flush)) {
                    set_error(EINVAL);
                    break;
                }
                const auto it = handles_.find(flush.fh);
                if (it == handles_.end()) {
                    set_error(EBADF);
                    break;
                }
                if (::fsync(it->second.get()) != 0) set_error(errno);
                break;
            }
            case FUSE_FSYNCDIR: {
                fuse_fsync_in fsync{};
                if (!input_at(payload, 0, fsync)) {
                    set_error(EINVAL);
                    break;
                }
                const auto it = handles_.find(fsync.fh);
                if (it == handles_.end()) {
                    set_error(EBADF);
                    break;
                }
                if (::fsync(it->second.get()) != 0) set_error(errno);
                break;
            }
            case FUSE_FORGET: {
                fuse_forget_in forget{};
                if (input_at(payload, 0, forget) && in.nodeid != FUSE_ROOT_ID) {
                    auto it = node_lookups_.find(in.nodeid);
                    if (it != node_lookups_.end()) {
                        if (forget.nlookup >= it->second) {
                            node_lookups_.erase(it);
                            nodes_.erase(in.nodeid);
                        } else {
                            it->second -= forget.nlookup;
                        }
                    }
                }
                break;
            }
            case FUSE_BATCH_FORGET: {
                fuse_batch_forget_in batch{};
                if (input_at(payload, 0, batch)) {
                    size_t offset = sizeof(batch);
                    for (uint32_t i = 0; i < batch.count; ++i) {
                        fuse_forget_one item{};
                        if (!input_at(payload, offset, item)) break;
                        offset += sizeof(item);
                        auto it = node_lookups_.find(item.nodeid);
                        if (item.nodeid != FUSE_ROOT_ID && it != node_lookups_.end()) {
                            if (item.nlookup >= it->second) {
                                node_lookups_.erase(it);
                                nodes_.erase(item.nodeid);
                            } else {
                                it->second -= item.nlookup;
                            }
                        }
                    }
                }
                break;
            }
            case FUSE_DESTROY:
                break;
            case FUSE_FSYNC: {
                fuse_fsync_in fsync{};
                if (!input_at(payload, 0, fsync)) {
                    set_error(EINVAL);
                    break;
                }
                const auto it = handles_.find(fsync.fh);
                if (it == handles_.end()) {
                    set_error(EBADF);
                    break;
                }
                if (::fsync(it->second.get()) != 0) set_error(errno);
                break;
            }
            case FUSE_READDIR: {
                fuse_read_in read{};
                if (!input_at(payload, 0, read)) {
                    set_error(EINVAL);
                    break;
                }
                const auto it = handles_.find(read.fh);
                if (it == handles_.end()) {
                    set_error(EBADF);
                    break;
                }
                const int duplicate = ::dup(it->second.get());
                if (duplicate < 0) {
                    set_error(errno);
                    break;
                }
                DIR* dir = ::fdopendir(duplicate);
                if (dir == nullptr) {
                    ::close(duplicate);
                    set_error(errno);
                    break;
                }
                ::seekdir(dir, static_cast<long>(read.offset));
                const size_t limit = std::min<size_t>(read.size, 1024 * 1024);
                std::vector<std::byte> data;
                while (auto* item = ::readdir(dir)) {
                    const size_t name_len = std::strlen(item->d_name);
                    const size_t rec_size = FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + name_len);
                    if (rec_size > limit - std::min(limit, data.size())) break;
                    const size_t pos = data.size();
                    data.resize(pos + rec_size);
                    fuse_dirent record{};
                    record.ino = item->d_ino;
                    record.off = static_cast<uint64_t>(::telldir(dir));
                    record.namelen = static_cast<uint32_t>(name_len);
                    record.type = static_cast<uint32_t>(item->d_type);
                    std::memcpy(data.data() + pos, &record, FUSE_NAME_OFFSET);
                    std::memcpy(data.data() + pos + FUSE_NAME_OFFSET, item->d_name, name_len);
                }
                ::closedir(dir);
                reply.insert(reply.end(), data.begin(), data.end());
                out.len = static_cast<uint32_t>(reply.size());
                break;
            }
            case FUSE_STATFS: {
                auto full = target(in.nodeid);
                struct statvfs st{};
                if (!full) {
                    set_error(ENOENT);
                    break;
                }
                auto fd = open_beneath(*full, O_RDONLY | O_NONBLOCK);
                if (!fd || ::fstatvfs(fd.get(), &st) != 0) {
                    set_error(errno);
                    break;
                }
                fuse_statfs_out result{};
                result.st.blocks = st.f_blocks;
                result.st.bfree = st.f_bfree;
                result.st.bavail = st.f_bavail;
                result.st.files = st.f_files;
                result.st.ffree = st.f_ffree;
                result.st.bsize = static_cast<uint32_t>(st.f_bsize);
                result.st.namelen = 255;
                result.st.frsize = static_cast<uint32_t>(st.f_frsize);
                emit(result);
                break;
            }
            case FUSE_MKDIR: {
                fuse_mkdir_in mkdir{};
                auto name = string_at(payload, sizeof(mkdir));
                auto parent = target(in.nodeid);
                if (!input_at(payload, 0, mkdir) || !name || name->empty() || !parent ||
                    name->find('/') != std::string::npos || *name == "." || *name == "..") {
                    set_error(EINVAL);
                    break;
                }
                const auto relative = *parent / *name;
                auto parent_fd = open_beneath(*parent, O_RDONLY | O_DIRECTORY);
                if (!parent_fd || ::mkdirat(parent_fd.get(), name->c_str(),
                                            mkdir.mode & ~mkdir.umask & 0777) != 0) {
                    set_error(errno);
                    break;
                }
                fuse_entry_out result{};
                if (!entry(relative, result))
                    set_error(errno);
                else
                    emit(result);
                break;
            }
            case FUSE_CREATE: {
                fuse_create_in create{};
                if (!input_at(payload, 0, create)) {
                    set_error(EINVAL);
                    break;
                }
                auto name = string_at(payload, sizeof(create));
                auto parent = target(in.nodeid);
                if (!name || !parent || name->empty() || name->find('/') != std::string::npos ||
                    *name == "." || *name == "..") {
                    set_error(EINVAL);
                    break;
                }
                const auto relative = *parent / *name;
                auto parent_fd = open_beneath(*parent, O_RDONLY | O_DIRECTORY);
                if (!parent_fd) {
                    set_error(errno);
                    break;
                }
                util::UniqueFd fd(
                    ::openat(parent_fd.get(), name->c_str(),
                             (create.flags & O_ACCMODE) | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                             create.mode & ~create.umask & 0777));
                if (!fd) {
                    set_error(errno);
                    break;
                }
                fuse_entry_out entry_result{};
                if (!entry(relative, entry_result)) {
                    set_error(errno);
                    break;
                }
                const uint64_t handle = static_cast<uint64_t>(fd.get());
                handles_.insert_or_assign(handle, std::move(fd));
                fuse_open_out open_result{};
                open_result.fh = handle;
                emit(entry_result);
                emit(open_result);
                break;
            }
            case FUSE_SETATTR: {
                fuse_setattr_in attr{};
                if (!input_at(payload, 0, attr)) {
                    set_error(EINVAL);
                    break;
                }
                auto full = target(in.nodeid);
                if (!full) {
                    set_error(ENOENT);
                    break;
                }
                const int fd =
                    attr.fh != 0 && handles_.contains(attr.fh) ? handles_.at(attr.fh).get() : -1;
                if (attr.fh != 0 && fd < 0) {
                    set_error(EBADF);
                    break;
                }
                util::UniqueFd path_fd;
                if (fd < 0) {
                    const int open_flags = (attr.valid & FATTR_SIZE) != 0 ? O_WRONLY : O_RDONLY;
                    path_fd = open_beneath(*full, open_flags | O_NONBLOCK);
                    if (!path_fd) {
                        set_error(errno);
                        break;
                    }
                }
                const int target_fd = fd >= 0 ? fd : path_fd.get();
                if ((attr.valid & FATTR_SIZE) != 0) {
                    if (::ftruncate(target_fd, static_cast<off_t>(attr.size)) != 0) {
                        set_error(errno);
                        break;
                    }
                }
                if ((attr.valid & FATTR_MODE) != 0) {
                    if (::fchmod(target_fd, attr.mode & 07777) != 0) {
                        set_error(errno);
                        break;
                    }
                }
                if ((attr.valid & (FATTR_UID | FATTR_GID)) != 0) {
                    const uid_t uid =
                        (attr.valid & FATTR_UID) != 0 ? attr.uid : static_cast<uid_t>(-1);
                    const gid_t gid =
                        (attr.valid & FATTR_GID) != 0 ? attr.gid : static_cast<gid_t>(-1);
                    if (::fchown(target_fd, uid, gid) != 0) {
                        set_error(errno);
                        break;
                    }
                }
                if ((attr.valid &
                     (FATTR_ATIME | FATTR_MTIME | FATTR_ATIME_NOW | FATTR_MTIME_NOW)) != 0) {
                    timespec times[2]{};
                    times[0].tv_sec = static_cast<time_t>(attr.atime);
                    times[0].tv_nsec = static_cast<long>(attr.atimensec);
                    times[1].tv_sec = static_cast<time_t>(attr.mtime);
                    times[1].tv_nsec = static_cast<long>(attr.mtimensec);
                    if ((attr.valid & FATTR_ATIME) == 0) times[0].tv_nsec = UTIME_OMIT;
                    if ((attr.valid & FATTR_MTIME) == 0) times[1].tv_nsec = UTIME_OMIT;
                    if ((attr.valid & FATTR_ATIME_NOW) != 0) times[0].tv_nsec = UTIME_NOW;
                    if ((attr.valid & FATTR_MTIME_NOW) != 0) times[1].tv_nsec = UTIME_NOW;
                    if (::futimens(target_fd, times) != 0) {
                        set_error(errno);
                        break;
                    }
                }
                struct stat st{};
                if (::fstat(target_fd, &st) != 0) {
                    set_error(errno);
                    break;
                }
                fuse_attr_out result{};
                result.attr_valid = 1;
                result.attr = fuse_attr_from_stat(st);
                result.attr.ino = in.nodeid;
                emit(result);
                break;
            }
            case FUSE_UNLINK:
            case FUSE_RMDIR: {
                auto name = string_at(payload, 0);
                auto parent = target(in.nodeid);
                if (!name || !parent || name->empty() || name->find('/') != std::string::npos ||
                    *name == "." || *name == "..") {
                    set_error(EINVAL);
                    break;
                }
                auto parent_fd = open_beneath(*parent, O_RDONLY | O_DIRECTORY);
                if (!parent_fd || ::unlinkat(parent_fd.get(), name->c_str(),
                                             in.opcode == FUSE_RMDIR ? AT_REMOVEDIR : 0) != 0)
                    set_error(errno);
                break;
            }
            case FUSE_RENAME:
            case FUSE_RENAME2: {
                uint64_t newdir = 0;
                uint32_t flags = 0;
                size_t names_offset = 0;
                if (in.opcode == FUSE_RENAME) {
                    fuse_rename_in rename{};
                    if (!input_at(payload, 0, rename)) {
                        set_error(EINVAL);
                        break;
                    }
                    newdir = rename.newdir;
                    names_offset = sizeof(rename);
                } else {
                    fuse_rename2_in rename{};
                    if (!input_at(payload, 0, rename)) {
                        set_error(EINVAL);
                        break;
                    }
                    newdir = rename.newdir;
                    flags = rename.flags;
                    names_offset = sizeof(rename);
                }
                auto old_name = string_at(payload, names_offset);
                auto new_name = old_name ? string_at(payload, names_offset + old_name->size() + 1)
                                         : std::nullopt;
                auto old_parent = target(in.nodeid);
                auto new_parent = target(newdir);
                if (flags != 0 || !old_name || !new_name || !old_parent || !new_parent ||
                    old_name->empty() || new_name->empty() || *old_name == "." ||
                    *new_name == "." || old_name->find('/') != std::string::npos ||
                    new_name->find('/') != std::string::npos || *old_name == ".." ||
                    *new_name == "..") {
                    set_error(flags ? EINVAL : EACCES);
                    break;
                }
                auto old_parent_fd = open_beneath(*old_parent, O_RDONLY | O_DIRECTORY);
                auto new_parent_fd = open_beneath(*new_parent, O_RDONLY | O_DIRECTORY);
                if (!old_parent_fd || !new_parent_fd ||
                    ::renameat(old_parent_fd.get(), old_name->c_str(), new_parent_fd.get(),
                               new_name->c_str()) != 0) {
                    set_error(errno);
                } else {
                    const auto old_relative = *old_parent / *old_name;
                    const auto new_relative = *new_parent / *new_name;
                    for (auto& [nodeid, node_path] : nodes_) {
                        (void)nodeid;
                        const auto mismatch = std::mismatch(
                            old_relative.begin(), old_relative.end(), node_path.begin(),
                            node_path.end());
                        if (mismatch.first == old_relative.end()) {
                            std::filesystem::path suffix;
                            for (auto it = mismatch.second; it != node_path.end(); ++it)
                                suffix /= *it;
                            node_path = new_relative / suffix;
                        }
                    }
                }
                break;
            }
            case FUSE_ACCESS: {
                fuse_access_in access{};
                auto full = target(in.nodeid);
                if (!input_at(payload, 0, access) || !full) {
                    set_error(EINVAL);
                    break;
                }
                auto fd = open_beneath(*full, O_PATH);
                if (!fd) set_error(errno);
                break;
            }
            default:
                set_error(ENOSYS);
                break;
        }
    }
    std::memcpy(reply.data(), &out, sizeof(out));
    if (in.opcode == FUSE_FORGET || in.opcode == FUSE_BATCH_FORGET || in.opcode == FUSE_DESTROY)
        return {};
    return reply;
}

}  // namespace simrv::device::virtio
