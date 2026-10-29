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

namespace {
[[nodiscard]] auto hypervisor_access_funct3(OperationId op) noexcept -> Funct3 {
    switch (op) {
        case OperationId::HLV_B: return Funct3::Lb;
        case OperationId::HLV_BU: return Funct3::Lbu;
        case OperationId::HLV_H: return Funct3::Lh;
        case OperationId::HLV_HU:
        case OperationId::HLVX_HU: return Funct3::Lhu;
        case OperationId::HLV_W: return Funct3::Lw;
        case OperationId::HLV_WU:
        case OperationId::HLVX_WU: return Funct3::Lwu;
        case OperationId::HLV_D:
        case OperationId::HSV_D: return Funct3::Ld;
        case OperationId::HSV_B: return Funct3::Sb;
        case OperationId::HSV_H: return Funct3::Sh;
        case OperationId::HSV_W: return Funct3::Sw;
        default: return Funct3::Lb;
    }
}
}  // namespace

// ==========================================
// MEM (Memory) Stage
// ==========================================

void CPU::run_memory_stage(Machine& machine) {
    if (isa::is_vector_op(active_context().op_id)) {
        return;
    }
    auto& ctx = active_context();
    const auto opcode = static_cast<Opcode>(ctx.opcode);

    if (isa::is_hlv_op(ctx.op_id)) {
        if (!ctx.memory_load_complete) {
            memory_load_phase(machine);
            if (ca_state.waiting_for_interconnect || ctx.pending_exception.has_value()) return;
            ctx.memory_load_complete = true;
        }
        return;
    }
    if (isa::is_hsv_op(ctx.op_id)) {
        ctx.mem_wdata = ctx.rrs2;
        memory_store_phase(machine);
        return;
    }

    if (isa::is_cmo_op(ctx.op_id)) {
        simrv::memory::MemoryAccess::execute_cmo(machine.memory_, *this, ctx.mem_addr, ctx.op_id);
        return;
    }

    if (simrv::compiler::likely(opcode == Opcode::Load || opcode == Opcode::LoadFp)) {
        if (!ctx.memory_load_complete) {
            memory_load_phase(machine);
            if (ca_state.waiting_for_interconnect || ctx.pending_exception.has_value()) return;
            ctx.memory_load_complete = true;
        }
        return;
    }

    if (simrv::compiler::likely(opcode == Opcode::Store || opcode == Opcode::StoreFp)) {
        ctx.mem_wdata = (opcode == Opcode::Store)
                            ? ctx.rrs2
                            : static_cast<simrv::xlen::Register>(
                                  ctx.fp_mem_wdata & static_cast<simrv::xlen::FloatingRegister>(
                                                         simrv::xlen::kLower32Mask));
        memory_store_phase(machine);
        return;
    }

    if (opcode != Opcode::Amo) {
        return;
    }

    if (!ctx.memory_load_complete) {
        memory_load_phase(machine);
        if (ca_state.waiting_for_interconnect || ctx.pending_exception.has_value()) return;
        ctx.memory_load_complete = true;
    }
    memory_prepare_store_data(machine);
    memory_store_phase(machine);
}

void CPU::memory_load_phase(Machine& machine) {
    auto& ctx = active_context();
    if (ctx.pending_exception.has_value()) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = ctx.funct5;

    if (opcode == Opcode::Load || isa::is_hlv_op(ctx.op_id) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        const auto access_funct3 = isa::is_hlv_op(ctx.op_id)
                                       ? hypervisor_access_funct3(ctx.op_id)
                                       : ctx.funct3;
        ctx.mem_rdata = simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, ctx.mem_addr,
                                                             access_funct3);
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

    if ((opcode == Opcode::Store) || isa::is_hsv_op(ctx.op_id) ||
        (opcode == Opcode::Amo && funct5 == Funct5Amo::Sc) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr && funct5 != Funct5Amo::Sc)) {
        const auto access_funct3 = isa::is_hsv_op(ctx.op_id)
                                       ? hypervisor_access_funct3(ctx.op_id)
                                       : ctx.funct3;
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, ctx.mem_addr, ctx.mem_wdata,
                                              access_funct3);
    }

    if (opcode == Opcode::StoreFp) {
        simrv::memory::MemoryAccess::storeFp(machine.memory_, *this, ctx.mem_addr, ctx.fp_mem_wdata,
                                             ctx.funct3);
    }

    if (ca_state.waiting_for_interconnect) return;
}

}  // namespace simrv::core
