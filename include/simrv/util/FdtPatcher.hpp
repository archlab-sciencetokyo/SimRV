#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace simrv::util {

struct FdtFramebufferInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint64_t base = 0;
    uint64_t size = 0;
};

/** Reject CPU ISA extensions advertised by an FDT when they are disabled in the selected MISA. */
auto validate_fdt_isa(std::span<const uint8_t> blob, uint64_t selected_misa)
    -> std::expected<void, std::string>;

/** Patch a supplied flattened device tree to match the active SimRV RAM and hart count. */
auto patch_fdt(std::span<const uint8_t> blob, uint64_t dram_base, uint64_t dram_size,
               uint32_t num_harts, FdtFramebufferInfo* out_fb = nullptr, uint64_t timebase_hz = 0,
               uint64_t cpu_clock_hz = 0) -> std::expected<std::vector<uint8_t>, std::string>;

}  // namespace simrv::util
