#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace simrv::util {

/** Patch a supplied flattened device tree to match the active SimRV RAM and hart count. */
auto patch_fdt(std::span<const uint8_t> blob, uint64_t dram_base, uint64_t dram_size,
               uint32_t num_harts) -> std::expected<std::vector<uint8_t>, std::string>;

}  // namespace simrv::util
