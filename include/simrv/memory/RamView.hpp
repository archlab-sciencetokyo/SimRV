/**
 * @file RamView.hpp
 * @brief Non-owning, runtime-sized DRAM mapping view.
 */
#pragma once

#include <cstddef>
#include <span>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/BuildConfig.hpp"
#include "simrv/xlen/AddressTypes.hpp"

namespace simrv::memory {

inline constexpr simrv::xlen::Address kDramBaseAddress =
    static_cast<simrv::xlen::Address>(0x80000000u);
inline constexpr simrv::xlen::Address kDramSize =
    static_cast<simrv::xlen::Address>(SIMRV_DRAM_SIZE_MB * 1024u * 1024u);

/// Overflow-safe containment test for a physical memory region.
[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto address_range_contains(
    simrv::xlen::Address base, simrv::xlen::Address extent, simrv::xlen::Address address,
    size_t size) noexcept -> bool {
    return size != 0 && size <= extent && address >= base && address - base <= extent - size;
}

/**
 * @class RamView
 * @brief A non-owning, runtime-sized DRAM mapping.
 *
 * Fast paths receive this value rather than relying on the build-time DRAM mask,
 * so a configured machine never aliases addresses above 256 MiB.
 */
class RamView {
   public:
    constexpr RamView() = default;
    constexpr RamView(simrv::xlen::Byte* data, simrv::xlen::Address base, simrv::xlen::Address size)
        : data_(data), base_(base), size_(size) {}
    constexpr RamView(std::span<simrv::xlen::Byte> storage,
                      simrv::xlen::Address base = kDramBaseAddress)
        : data_(storage.data()),
          base_(base),
          size_(static_cast<simrv::xlen::Address>(storage.size())) {}

    [[nodiscard]] constexpr auto data() const noexcept -> simrv::xlen::Byte* { return data_; }
    [[nodiscard]] constexpr auto base() const noexcept -> simrv::xlen::Address { return base_; }
    [[nodiscard]] constexpr auto size() const noexcept -> simrv::xlen::Address { return size_; }
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto host_offset() const noexcept -> uintptr_t {
        return reinterpret_cast<uintptr_t>(data_) - base_;
    }
    [[nodiscard]] constexpr auto span() const noexcept -> std::span<simrv::xlen::Byte> {
        return {data_, static_cast<std::size_t>(size_)};
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto contains(simrv::xlen::Address address,
                                                              size_t bytes = 1) const noexcept
        -> bool {
        return data_ != nullptr && address >= base_ && bytes <= size_ &&
               (static_cast<simrv::xlen::Address>(address - base_) <= (size_ - bytes));
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto contains(simrv::xlen::PhysAddr address,
                                                              size_t bytes = 1) const noexcept
        -> bool {
        return contains(address.raw(), bytes);
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto try_get_ptr(simrv::xlen::Address address,
                                                                 size_t bytes = 1) const noexcept
        -> simrv::xlen::Byte* {
        const auto offset = static_cast<simrv::xlen::Address>(address - base_);
        if (simrv::compiler::likely(data_ != nullptr && offset <= (size_ - bytes) &&
                                    bytes <= size_)) {
            return data_ + offset;
        }
        return nullptr;
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto try_get_ptr(simrv::xlen::PhysAddr address,
                                                                 size_t bytes = 1) const noexcept
        -> simrv::xlen::Byte* {
        return try_get_ptr(address.raw(), bytes);
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto try_get_ptr_fast(simrv::xlen::Address address,
                                                                      size_t bytes) const noexcept
        -> simrv::xlen::Byte* {
        const auto offset = static_cast<simrv::xlen::Address>(address - base_);
        if (simrv::compiler::likely(offset <= (size_ - bytes))) {
            return data_ + offset;
        }
        return nullptr;
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto try_get_ptr_fast(simrv::xlen::PhysAddr address,
                                                                      size_t bytes) const noexcept
        -> simrv::xlen::Byte* {
        return try_get_ptr_fast(address.raw(), bytes);
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto unchecked_ptr(
        simrv::xlen::Address address) const noexcept -> simrv::xlen::Byte* {
        return data_ + (address - base_);
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto unchecked_ptr(
        simrv::xlen::PhysAddr address) const noexcept -> simrv::xlen::Byte* {
        return unchecked_ptr(address.raw());
    }

   private:
    simrv::xlen::Byte* data_ = nullptr;
    simrv::xlen::Address base_ = kDramBaseAddress;
    simrv::xlen::Address size_ = 0;
};

}  // namespace simrv::memory
