/**
 * @file RamView.hpp
 * @brief Non-owning, runtime-sized DRAM mapping view.
 */
#pragma once

#include <cstddef>

#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

#ifndef SIMRV_DRAM_SIZE_MB
#define SIMRV_DRAM_SIZE_MB 256
#endif

inline constexpr Address kDramBaseAddress = static_cast<Address>(0x80000000u);
inline constexpr Address kDramSize = static_cast<Address>(SIMRV_DRAM_SIZE_MB * 1024u * 1024u);

/// Overflow-safe containment test for a physical memory region.
[[nodiscard]] constexpr auto address_range_contains(Address base, Address extent, Address address,
                                                    size_t size) -> bool {
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
    constexpr RamView(Byte* data, Address base, Address size)
        : data_(data), base_(base), size_(size) {}

    [[nodiscard]] constexpr auto data() const noexcept -> Byte* { return data_; }
    [[nodiscard]] constexpr auto base() const noexcept -> Address { return base_; }
    [[nodiscard]] constexpr auto size() const noexcept -> Address { return size_; }
    [[nodiscard]] constexpr auto contains(Address address, size_t bytes = 1) const noexcept
        -> bool {
        return data_ != nullptr && address_range_contains(base_, size_, address, bytes);
    }
    [[nodiscard]] constexpr auto unchecked_ptr(Address address) const noexcept -> Byte* {
        return data_ + (address - base_);
    }

   private:
    Byte* data_ = nullptr;
    Address base_ = kDramBaseAddress;
    Address size_ = 0;
};

}  // namespace simrv::memory
