/**
 * @file StageMEM.cpp
 * @brief MEM stage implementation for Machine.
 */
#include <cstdint>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

void CPU::run_memory_stage(Machine& machine) {
    memory_load_phase(machine);
    memory_prepare_store_data(machine);
    memory_store_phase(machine);
}

/* memory_load_phase(Load Data) stage */
void CPU::memory_load_phase(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);

    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        ctx.mem_rdata =
            simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, ctx.mem_addr, ctx.funct3);
    }

    if (opcode == Opcode::LoadFp) {
        ctx.fp_mem_rdata =
            simrv::memory::MemoryAccess::loadFp(machine.memory_, *this, ctx.mem_addr, ctx.funct3);
    }

    if (opcode == Opcode::Amo && funct5 == Funct5Amo::Lr) {
        state_.load_res = ctx.mem_addr;
        state_.reserved = 1;
    }
}

/* memory_prepare_store_data(Execution 2) stage */
void CPU::memory_prepare_store_data(Machine& /*machine*/) {
    auto& ctx = pipeline_context;
    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);
    ctx.mem_wdata = (opcode != Opcode::Amo || funct5 == Funct5Amo::Sc)
                        ? ctx.rrs2
                        : execute::ExecuteUnit::aluAmo(ctx.rrs2, ctx.mem_rdata, funct5, ctx.funct3);

    if (opcode == Opcode::StoreFp) {
        ctx.mem_wdata =
            static_cast<Register>(ctx.fp_mem_wdata & static_cast<FloatingRegister>(kLower32Mask));
    }
}

/* memory_store_phase(Store Data) stage */
void CPU::memory_store_phase(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);

    if ((opcode == Opcode::Store) ||
        (opcode == Opcode::Amo &&
          (funct5 == Funct5Amo::Sc && (ctx.wb_data == 0u) && (state_.reserved != 0u))) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr && funct5 != Funct5Amo::Sc)) {
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, ctx.mem_addr, ctx.mem_wdata,
                                              ctx.funct3);
    }

    if (opcode == Opcode::StoreFp) {
        simrv::memory::MemoryAccess::storeFp(machine.memory_, *this, ctx.mem_addr, ctx.fp_mem_wdata,
                                             ctx.funct3);
    }

    // Any store instruction or non-LR AMO instruction from the same hart clears the reservation.
    if ((opcode == Opcode::Store) || (opcode == Opcode::StoreFp) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr)) {
        if (!ctx.pending_exception.has_value()) {
            state_.reserved = 0;
        }
    }
}

}  // namespace simrv::core
