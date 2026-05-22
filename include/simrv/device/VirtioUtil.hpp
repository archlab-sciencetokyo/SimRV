/**
 * @file VirtioUtil.hpp
 * @brief Shared utility functions for Virtio MMIO device queue operations.
 *
 * Provides common implementations for queue descriptor processing,
 * memory operations, and word/byte conversions used by Virtio devices
 * like disk and console.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <print>

#include "simrv/Define.hpp"
#include "simrv/device/Virtio.hpp"
#include "simrv/memory/MemoryUtil.hpp"

namespace simrv::virtio_detail {

/**
 * Read a structured memory block from DRAM into a strongly-typed struct.
 *
 * @tparam T Struct type to read
 * @param addr Starting address in DRAM
 * @param mmem Pointer to DRAM base
 * @param size Number of bytes to read (defaults to sizeof(T))
 * @return Populated struct instance
 */
template <typename T>
inline auto read_struct_from_ram(Address addr, Byte* mmem, std::size_t size = sizeof(T)) -> T {
    T result{};
    auto* p = reinterpret_cast<Byte*>(&result);
    const std::size_t limit = std::min(size, sizeof(T));
    Address const start_idx = addr & simrv::memory::kDramMask;
    if (start_idx + limit <= simrv::memory::kDramSize) {
        std::memcpy(p, mmem + start_idx, limit);
    } else {
        std::size_t const first_part = simrv::memory::kDramSize - start_idx;
        std::memcpy(p, mmem + start_idx, first_part);
        std::memcpy(p + first_part, mmem, limit - first_part);
    }
    return result;
}

/**
 * Write a structured memory block to DRAM from a strongly-typed struct.
 *
 * @tparam T Struct type to write
 * @param addr Starting address in DRAM
 * @param data Struct instance to write
 * @param mmem Pointer to DRAM base
 * @param size Number of bytes to write (defaults to sizeof(T))
 */
template <typename T>
inline void write_struct_to_ram(Address addr, const T& data, Byte* mmem,
                                std::size_t size = sizeof(T)) {
    const auto* p = reinterpret_cast<const Byte*>(&data);
    const std::size_t limit = std::min(size, sizeof(T));
    Address const start_idx = addr & simrv::memory::kDramMask;
    if (start_idx + limit <= simrv::memory::kDramSize) {
        std::memcpy(mmem + start_idx, p, limit);
    } else {
        std::size_t const first_part = simrv::memory::kDramSize - start_idx;
        std::memcpy(mmem + start_idx, p, first_part);
        std::memcpy(mmem, p + first_part, limit - first_part);
    }
}

/**
 * Load n bytes from DRAM at address, returning as a Word (little-endian).
 *
 * Supported sizes are 1, 2, and 4 bytes.
 *
 * @param addr Starting address in DRAM
 * @param n Number of bytes to load (must be 1, 2, or 4)
 * @param ram Pointer to DRAM base
 * @return Word with n bytes loaded in little-endian order
 */
inline auto load_from_ram(Address addr, int n, Byte* ram) -> Word {
    if (n != 1 && n != 2 && n != 4) {
        std::println(stderr, "__ Error: ram_r() not supported n={}", n);
        std::exit(EXIT_FAILURE);
    }
    Word data = 0;
    Address const start_idx = addr & simrv::memory::kDramMask;
    if (start_idx + static_cast<std::size_t>(n) <= simrv::memory::kDramSize) {
        std::memcpy(&data, ram + start_idx, static_cast<std::size_t>(n));
    } else {
        std::size_t const first_part = simrv::memory::kDramSize - start_idx;
        std::memcpy(&data, ram + start_idx, first_part);
        std::memcpy(reinterpret_cast<Byte*>(&data) + first_part, ram,
                    static_cast<std::size_t>(n) - first_part);
    }
    return data;
}

/**
 * Store n bytes to DRAM at address from a Word (little-endian).
 *
 * Supported sizes are 1, 2, and 4 bytes.
 *
 * @param addr Starting address in DRAM
 * @param data Word value to store
 * @param n Number of bytes to store (must be 1, 2, or 4)
 * @param ram Pointer to DRAM base
 */
inline void store_to_ram(Address addr, Word data, int n, Byte* ram) {
    if (n != 1 && n != 2 && n != 4) {
        std::println(stderr, "__ Error: dsk_w() not supported n={}", n);
        std::exit(EXIT_FAILURE);
    }
    Address const start_idx = addr & simrv::memory::kDramMask;
    if (start_idx + static_cast<std::size_t>(n) <= simrv::memory::kDramSize) {
        std::memcpy(ram + start_idx, &data, static_cast<std::size_t>(n));
    } else {
        std::size_t const first_part = simrv::memory::kDramSize - start_idx;
        std::memcpy(ram + start_idx, &data, first_part);
        std::memcpy(ram, reinterpret_cast<const Byte*>(&data) + first_part,
                    static_cast<std::size_t>(n) - first_part);
    }
}

inline auto get_desc_addr_combined(const virtio::QueueState* qs) -> Address {
    if constexpr (kIsXLen64) {
        return (static_cast<Address>(qs->DescHigh) << 32) | static_cast<uint32_t>(qs->DescLow);
    } else {
        return qs->DescLow;
    }
}

inline auto get_avail_addr_combined(const virtio::QueueState* qs) -> Address {
    if constexpr (kIsXLen64) {
        return (static_cast<Address>(qs->AvailHigh) << 32) | static_cast<uint32_t>(qs->AvailLow);
    } else {
        return qs->AvailLow;
    }
}

inline auto get_used_addr_combined(const virtio::QueueState* qs) -> Address {
    if constexpr (kIsXLen64) {
        return (static_cast<Address>(qs->UsedHigh) << 32) | static_cast<uint32_t>(qs->UsedLow);
    } else {
        return qs->UsedLow;
    }
}

/**
 * Get the address of a descriptor header for the current available index.
 */
inline auto next_avail_desc_idx(const virtio::QueueState* qs, Word q_num, Byte* mmem) -> uint16_t {
    Address const adr = get_avail_addr_combined(qs) + 4 + ((qs->last_avail_idx & (q_num - 1)) * 2);
    return static_cast<uint16_t>(load_from_ram(adr, 2, mmem));
}

inline auto get_desc_addr(uint16_t desc_idx, const virtio::QueueState* qs) -> Address {
    return (static_cast<Address>(desc_idx) * 16) + get_desc_addr_combined(qs);
}

/**
 * Update a virtio queue descriptor in memory.
 *
 * Updates the 'used' ring to reflect that a descriptor has been processed.
 *
 * @param desc_idx Descriptor index to mark as used
 * @param desc_len Length/return value to store for this descriptor
 * @param q_num Queue size (for index wrapping)
 * @param qs Pointer to queue state structure containing queue metadata
 * @param mmem Pointer to memory containing virtio queue structures
 */
inline void update_descriptor(Word desc_idx, Word desc_len, int q_num, virtio::QueueState* qs,
                              Byte* mmem) {
    Address const addr_used_idx = get_used_addr_combined(qs) + 2;
    auto const index = read_struct_from_ram<uint16_t>(addr_used_idx, mmem);

    write_struct_to_ram<uint16_t>(addr_used_idx, index + 1, mmem);

    Address const addr_used_entry = get_used_addr_combined(qs) + 4 + ((index & (q_num - 1)) * 8);
    virtio::VirtqUsedElem const elem{static_cast<uint32_t>(desc_idx),
                                     static_cast<uint32_t>(desc_len)};
    write_struct_to_ram<virtio::VirtqUsedElem>(addr_used_entry, elem, mmem);
}

}  // namespace simrv::virtio_detail
