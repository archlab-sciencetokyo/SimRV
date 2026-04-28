/**
 * @file VirtioUtil.hpp
 * @brief Shared utility functions for Virtio MMIO device queue operations.
 *
 * Provides common implementations for queue descriptor processing,
 * memory operations, and word/byte conversions used by Virtio devices
 * like disk and console.
 */
#pragma once

#include <cstdlib>
#include <print>

#include "Define.hpp"

namespace simrv::virtio_detail {

/**
 * Convert a single byte to a word (zero-extended).
 * @param b Byte value to convert
 * @return Word with byte value in LSB, zeros in upper bits
 */
static constexpr auto byte_to_word(Byte b) -> Word {
    return static_cast<Word>(std::to_integer<uint8_t>(b));
}

/**
 * Convert a word to a single byte (truncated to LSB).
 * @param w Word value to convert
 * @return LSB of the word as a byte
 */
static constexpr auto word_to_byte(Word w) -> Byte {
    return static_cast<Byte>(static_cast<uint8_t>(w & 0xffU));
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
static auto load_from_ram(Address addr, int n, Byte* ram) -> Word {
    if (n != 1 && n != 2 && n != 4) {
        std::println("__ Error: ram_r() not supported n={}", n);
        exit(0);
    }
    Word data = 0;
    for (int i = 0; i < n; i++) {
        data |= byte_to_word(ram[(addr + i) & simrv::memory::kDramMask]) << (8 * i);
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
static void store_to_ram(Address addr, Word data, int n, Byte* ram) {
    if (n != 1 && n != 2 && n != 4) {
        std::println("__ Error: dsk_w() not supported n={}", n);
        exit(0);
    }
    if (n == 1) {
        ram[addr & simrv::memory::kDramMask] = word_to_byte(data);
    } else if (n == 2) {
        ram[addr & simrv::memory::kDramMask] = word_to_byte(data);
        ram[(addr + 1) & simrv::memory::kDramMask] = word_to_byte(data >> 8);
    } else if (n == 4) {
        ram[addr & simrv::memory::kDramMask] = word_to_byte(data);
        ram[(addr + 1) & simrv::memory::kDramMask] = word_to_byte(data >> 8);
        ram[(addr + 2) & simrv::memory::kDramMask] = word_to_byte(data >> 16);
        ram[(addr + 3) & simrv::memory::kDramMask] = word_to_byte(data >> 24);
    }
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
static void update_descriptor(Word desc_idx, Word desc_len, int q_num, QueueState* qs, Byte* mmem) {
    Address const addr_used_idx = qs->UsedLow + 2;
    Word const index = static_cast<uint16_t>(load_from_ram(addr_used_idx, 2, mmem));

    store_to_ram(addr_used_idx, index + 1, 2, mmem);

    Address const addr_used_entry = qs->UsedLow + 4 + ((index & (q_num - 1)) * 8);
    store_to_ram(addr_used_entry, desc_idx, 4, mmem);
    store_to_ram(addr_used_entry + 4, desc_len, 4, mmem);
}

}  // namespace simrv::virtio_detail
