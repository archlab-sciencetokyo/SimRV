/**
 * @file FdtGenerator.hpp
 * @brief Flattened Device Tree (FDT) binary synthesizer.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/SoCConfig.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::util {

struct FdtConfig {
    uint32_t num_harts = 1;
    uint64_t dram_base = 0x80000000ULL;
    uint64_t dram_size = 256ULL * 1024ULL * 1024ULL;
    unsigned int xlen = 64;
    std::string bootargs =
        "console=tty0 console=ttyS0,115200 earlycon=uart8250,mmio,0x10000000,115200n8 "
        "root=/dev/vda rw "
        "loglevel=7";
    std::string isa_string = (SIMRV_XLEN == 64) ? "rv64imafdcbv" : "rv32imafdcbv";
    bool enable_pcie = true;
    bool enable_mmio = false;
    bool enable_framebuffer = true;
    uint64_t framebuffer_base = 0x84000000ULL;
    uint32_t framebuffer_width = 640;
    uint32_t framebuffer_height = 480;
    uint32_t framebuffer_stride = 640 * 4;
    simrv::core::SoCConfig soc = simrv::core::SoCConfig::virt_pcie();
};

class FdtGenerator {
   public:
    static auto generate(const FdtConfig& config) -> std::vector<uint8_t>;
};

}  // namespace simrv::util
