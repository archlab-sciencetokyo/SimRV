/**
 * @file MemorySubsystem.cpp
 * @brief Memory access and translation subsystem implementation.
 */
#include "simrv/memory/MemorySubsystem.hpp"

#include <cstdint>
#include <cstdio>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryUtil.hpp"

namespace simrv::memory {

void MemorySubsystem::initialize_mmu() {
    if (!mmu_) {
        const auto geometry = machine_.memory_geometry();
        mmu_ = std::make_unique<simrv::Mmu>(machine_.ram_data(), geometry.dram_base,
                                           geometry.dram_size);
    }
}

}  // namespace simrv::memory
