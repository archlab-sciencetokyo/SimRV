/**
 * @file MemoryStage.cpp
 * @brief Memory (MEM) stage implementation.
 */

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/ReservationTable.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::core {

using namespace simrv::isa;

// ==========================================
// MEM (Memory) Stage
// ==========================================

void CPU::run_memory_stage(Machine& machine) {
    if (isa::is_vector_op(active_context().op_id)) {
        return;
    }
    auto& ctx = active_context();
    if (!ctx.memory_load_complete) {
        memory_load_phase(machine);
        if (ca_state.waiting_for_interconnect || ctx.pending_exception.has_value()) return;
        ctx.memory_load_complete = true;
    }
    memory_prepare_store_data(machine);
    memory_store_phase(machine);
    if (ca_state.waiting_for_interconnect) return;
}

void CPU::memory_load_phase(Machine& machine) {
    auto& ctx = active_context();
    if (ctx.pending_exception.has_value()) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = ctx.funct5;

    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        ctx.mem_rdata =
            simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, ctx.mem_addr, ctx.funct3);
    }

    if (opcode == Opcode::LoadFp) {
        ctx.fp_mem_rdata =
            simrv::memory::MemoryAccess::loadFp(machine.memory_, *this, ctx.mem_addr, ctx.funct3);
    }

    if (opcode == Opcode::Amo && funct5 == Funct5Amo::Lr) {
        if (!ctx.pending_exception.has_value() && !ca_state.waiting_for_interconnect) {
            state_.load_res = ctx.mem_paddr.raw();
            state_.lr_reservation_active = true;
            machine.memory_.reservation_table().set_reservation(hart_id(), ctx.mem_paddr.raw());
        }
    }
}

void CPU::memory_prepare_store_data(Machine& /*machine*/) {
    auto& ctx = active_context();
    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = ctx.funct5;
    ctx.mem_wdata = (opcode != Opcode::Amo || funct5 == Funct5Amo::Sc)
                        ? ctx.rrs2
                        : execute::ExecuteUnit::aluAmo(ctx.rrs2, ctx.mem_rdata, funct5, ctx.funct3);

    if (opcode == Opcode::StoreFp) {
        ctx.mem_wdata = static_cast<simrv::xlen::Register>(
            ctx.fp_mem_wdata &
            static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kLower32Mask));
    }
}

void CPU::memory_store_phase(Machine& machine) {
    auto& ctx = active_context();
    if (ctx.pending_exception.has_value()) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = ctx.funct5;

    if ((opcode == Opcode::Store) || (opcode == Opcode::Amo && funct5 == Funct5Amo::Sc) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr && funct5 != Funct5Amo::Sc)) {
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, ctx.mem_addr, ctx.mem_wdata,
                                              ctx.funct3);
    }

    if (opcode == Opcode::StoreFp) {
        simrv::memory::MemoryAccess::storeFp(machine.memory_, *this, ctx.mem_addr, ctx.fp_mem_wdata,
                                             ctx.funct3);
    }

    if (ca_state.waiting_for_interconnect) return;
}

}  // namespace simrv::core
