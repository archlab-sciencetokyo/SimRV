/**
 * @file StageMEM.cpp
 * @brief MEM stage implementation for Machine.
 */
#include "Machine.hpp"

void CPU::run_memory_stage(Machine& machine) {
    memory_load_phase(machine);
    memory_prepare_store_data(machine);
    memory_store_phase(machine);
}

/* memory_load_phase(Load Data) stage                                                                   */
void CPU::memory_load_phase(Machine& machine) {
    if (pending_exception != ~0u) return;

    const auto opcode = static_cast<Opcode>(pipeline_context.opcode);
    const auto funct5 = static_cast<Funct5Amo>(pipeline_context.funct5);

    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        pipeline_context.mem_rdata =
            machine.memory_.target_read(*this, pipeline_context.mem_addr, pipeline_context.funct3);
    }

    if (opcode == Opcode::LoadFp) {
        const auto funct3 = static_cast<Funct3>(pipeline_context.funct3);
        if (funct3 == Funct3::Flw) {
            const Word lo = machine.memory_.target_read(*this, pipeline_context.mem_addr,
                                                        static_cast<Instruction>(Funct3::Lw));
            pipeline_context.fp_mem_rdata = 0xffffffff00000000ull | static_cast<uint64_t>(lo);
        } else if (funct3 == Funct3::Fld) {
            const Word lo = machine.memory_.target_read(*this, pipeline_context.mem_addr,
                                                        static_cast<Instruction>(Funct3::Lw));
            const Word hi = machine.memory_.target_read(*this, pipeline_context.mem_addr + 4,
                                                        static_cast<Instruction>(Funct3::Lw));
            pipeline_context.fp_mem_rdata =
                static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        }
    }

    if (opcode == Opcode::Amo && funct5 == Funct5Amo::Lr) {
        load_res = pipeline_context.mem_addr;
        reserved = 1;
    }
}

/* memory_prepare_store_data(Execution 2) stage                                                                 */
void CPU::memory_prepare_store_data(Machine& /*machine*/) {
    const auto opcode = static_cast<Opcode>(pipeline_context.opcode);
    pipeline_context.mem_wdata =
        (opcode != Opcode::Amo)
            ? pipeline_context.rrs2
            : execute_unit.aluAmo(pipeline_context.rrs2, pipeline_context.mem_rdata,
                                  pipeline_context.funct5);

    if (opcode == Opcode::StoreFp) {
        pipeline_context.mem_wdata =
            static_cast<Register>(pipeline_context.fp_mem_wdata & 0xffffffffu);
    }
}

/* memory_store_phase(Store Data) stage                                                                  */
void CPU::memory_store_phase(Machine& machine) {
    if (pending_exception != ~0u) return;

    const auto opcode = static_cast<Opcode>(pipeline_context.opcode);
    const auto funct5 = static_cast<Funct5Amo>(pipeline_context.funct5);

    if ((opcode == Opcode::Store) ||
        (opcode == Opcode::Amo &&
         (funct5 == Funct5Amo::Sc && !pipeline_context.wb_data && reserved)) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr && funct5 != Funct5Amo::Sc)) {
        machine.memory_.target_write(*this, pipeline_context.mem_addr, pipeline_context.mem_wdata,
                                     pipeline_context.funct3);
    }
    if (opcode == Opcode::Amo && (funct5 == Funct5Amo::Sc && !pipeline_context.wb_data &&
                                  reserved && pending_exception == ~0u)) {
        reserved = 0;
    }

    if (opcode == Opcode::StoreFp) {
        const auto funct3 = static_cast<Funct3>(pipeline_context.funct3);
        if (funct3 == Funct3::Fsw) {
            machine.memory_.target_write(
                *this, pipeline_context.mem_addr,
                static_cast<Word>(pipeline_context.fp_mem_wdata & 0xffffffffu),
                static_cast<Instruction>(Funct3::Sw));
        } else if (funct3 == Funct3::Fsd) {
            machine.memory_.target_write(
                *this, pipeline_context.mem_addr,
                static_cast<Word>(pipeline_context.fp_mem_wdata & 0xffffffffu),
                static_cast<Instruction>(Funct3::Sw));
            machine.memory_.target_write(
                *this, pipeline_context.mem_addr + 4,
                static_cast<Word>((pipeline_context.fp_mem_wdata >> 32) & 0xffffffffu),
                static_cast<Instruction>(Funct3::Sw));
        }
    }
}
