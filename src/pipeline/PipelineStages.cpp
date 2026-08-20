/**
 * @file PipelineStages.cpp
 * @brief Consolidated pipeline stages implementation for Machine.
 */
#include <bit>
#include <cstdint>
#include <optional>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

// ==========================================
// IF (Instruction Fetch) Stage
// ==========================================

void CPU::run_fetch_stage(Machine& machine) {
    if (state_.regs.xlen == 32) {
        state_.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
    }
    auto& ctx = pipeline_context;
    ctx.tlb_miss = false;

    const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
    const Word alignment_mask = has_c ? 1u : 3u;
    if ((state_.pc & alignment_mask) != 0) {
        ctx.pending_exception = ExceptionCode::MisalignedFetch;
        ctx.pending_tval = state_.pc;
        ctx.ir = isa::RV32_NOP;
        ctx.op_id = isa::UNKNOWN;
        return;
    }

    const bool split_page =
        ((state_.pc & ~simrv::memory::kPageMask) != ((state_.pc + 2) & ~simrv::memory::kPageMask));
    const bool translation_enabled =
        state_.priv != kPrivMachine &&
        simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen);

    fetch_address_translate(machine);

    if (simrv::compiler::unlikely(translation_enabled)) {
        fetch_resolve_page_walk(machine, 1);
        if (simrv::compiler::likely(!split_page)) {
            if (!ctx.pending_exception.has_value() && ctx.padr1 != kWordAllOnes) {
                ctx.padr2 = ctx.padr1 + 2;
            }
        }
    }

    fetch_read_instruction_word(machine);
    decode_and_normalize_instruction(machine);
}

void CPU::fetch_address_translate(Machine& /*machine*/) {
    auto& ctx = pipeline_context;
    Word w_padr1 = kWordAllOnes;
    Word w_padr2 = kWordAllOnes;
    Word const w_vadr1 = state_.pc;
    Word const w_vadr2 = state_.pc + 2;

    ctx.cpc = state_.pc;

    if (state_.priv == kPrivMachine ||
        !simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen)) {
        w_padr1 = (state_.regs.xlen == 32) ? (w_vadr1 & 0xFFFFFFFFULL) : w_vadr1;
        w_padr2 = (state_.regs.xlen == 32) ? (w_vadr2 & 0xFFFFFFFFULL) : w_vadr2;
    } else {
        const bool split_page =
            ((w_vadr1 & ~simrv::memory::kPageMask) != (w_vadr2 & ~simrv::memory::kPageMask));
        const Word current_asid = simrv::xlen::satp_asid(state_.satp, state_.regs.xlen);

        TLBEntry* tlb_e1 = tlb.lookup_inst_r(w_vadr1, current_asid, state_.priv);
        if (tlb_e1) {
            w_padr1 = tlb_e1->p_addr + (w_vadr1 & simrv::memory::kPageMask);
        }

        if (simrv::compiler::likely(!split_page)) {
            if (w_padr1 != kWordAllOnes) {
                w_padr2 = w_padr1 + 2;
            }
        } else {
            TLBEntry* tlb_e2 = tlb.lookup_inst_r(w_vadr2, current_asid, state_.priv);
            if (tlb_e2) {
                w_padr2 = tlb_e2->p_addr + (w_vadr2 & simrv::memory::kPageMask);
            }
        }
    }
    ctx.padr1 = w_padr1;
    ctx.padr2 = w_padr2;
}

void CPU::fetch_resolve_page_walk(Machine& machine, int state) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    Word w_padr = (state == 1) ? ctx.padr1 : ctx.padr2;
    Word* r_padr = (state == 1) ? &ctx.padr1 : &ctx.padr2;
    Word const w_vadr = (state == 1) ? state_.pc : state_.pc + 2;
    if (w_padr == kWordAllOnes) {
        ctx.tlb_miss = true;
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(
                    !simrv::Mmu::is_canonical(w_vadr, state_.satp, state_.regs.xlen))) {
                ctx.pending_exception = ExceptionCode::FetchPageFault;
                ctx.pending_tval = w_vadr;
                return;
            }
        }

        auto* mmu = machine.memory_.mmu();
        auto translate_res = mmu->translate(w_vadr, PteAccess::Code, state_.priv, state_.mstatus,
                                            state_.satp, state_.regs.xlen);
        auto chain_res =
            translate_res
                .and_then([&](Address phys) -> std::expected<void, TrapCause> {
                    w_padr = phys;
                    tlb.insert_inst_r(w_vadr, w_padr,
                                      simrv::xlen::satp_asid(state_.satp, state_.regs.xlen),
                                      state_.priv);
                    return {};
                })
                .or_else([&](TrapCause error) -> std::expected<void, TrapCause> {
                    ctx.pending_exception = static_cast<ExceptionCode>(error);
                    ctx.pending_tval = w_vadr;
                    return {};
                });
        (void)chain_res;
    }
    *r_padr = w_padr;
}

void CPU::fetch_read_instruction_word(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    if (machine.s_high_performance) {
        if (simrv::compiler::likely(ctx.padr2 == ctx.padr1 + 2 &&
                                    simrv::memory::is_dram_access(ctx.padr1, sizeof(uint16_t)))) {
            const Address masked = ctx.padr1 & simrv::memory::kDramMask;
            if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 3))) {
                uint32_t val = 0;
                std::memcpy(&val, machine.mmem + masked, 4);
                if ((val & 0x3) != 0x3) {
                    ctx.ir_org = val & 0xFFFF;
                } else {
                    ctx.ir_org = val;
                }
                return;
            }
            const uint16_t h1 = simrv::memory::ram_read_fast(
                ctx.padr1, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
            if ((h1 & 0x3) != 0x3) {
                ctx.ir_org = h1;
            } else {
                if (simrv::compiler::unlikely(
                        !simrv::memory::is_dram_access(ctx.padr2, sizeof(uint16_t)))) {
                    ctx.pending_exception = ExceptionCode::FaultFetch;
                    ctx.pending_tval = state_.pc + 2;
                    return;
                }
                const uint16_t h2 = simrv::memory::ram_read_fast(
                    ctx.padr2, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
                ctx.ir_org = (static_cast<uint32_t>(h2) << 16) | h1;
            }
            return;
        }
    }

    if (simrv::compiler::likely(ctx.padr2 == ctx.padr1 + 2 &&
                                simrv::memory::is_dram_access(ctx.padr1, sizeof(uint16_t)))) {
        auto fetch_halfword = [&](Address paddr, Address vaddr) -> std::optional<uint16_t> {
            uint16_t h_data = 0;
            if (icache.read16(paddr, h_data)) {
                return h_data;
            }
            const Address line_base =
                paddr & ~(static_cast<Address>(simrv::cache::ICache::kLineBytes - 1u));

            std::array<Byte, simrv::cache::ICache::kLineBytes> line_data{};
            const unsigned fetch_size = xlen::kFetchSize;
            const auto fetch_funct3 =
                static_cast<Instruction>(xlen::kIsXLen64 ? isa::Funct3::Sd : isa::Funct3::Sw);

            for (uint32_t i = 0; i < simrv::cache::ICache::kLineBytes; i += fetch_size) {
                simrv::memory::TlChannelA req{};
                req.opcode = simrv::memory::TlOpcodeA::Get;
                req.size = static_cast<uint8_t>(fetch_funct3 & 0x3);
                req.source = 1;
                req.address = line_base + i;
                machine.memory_.system_bus().send_request(req);

                simrv::memory::TlChannelD resp{};
                const bool received = machine.memory_.system_bus().get_response(1, resp);
                const bool contains_requested_halfword =
                    paddr >= req.address && paddr - req.address <= fetch_size - sizeof(uint16_t);
                if ((!received || resp.error) && contains_requested_halfword) {
                    ctx.pending_exception = ExceptionCode::FaultFetch;
                    ctx.pending_tval = vaddr;
                    return std::nullopt;
                }
                if (received && !resp.error) {
                    std::memcpy(line_data.data() + i, &resp.data, fetch_size);
                }
            }

            icache.insert(line_base, line_data.data());
            (void)icache.read16(paddr, h_data);
            return h_data;
        };

        const auto h1 = fetch_halfword(ctx.padr1, state_.pc);
        if (!h1.has_value()) {
            return;
        }
        if ((*h1 & 0x3) != 0x3) {
            ctx.ir_org = *h1;
        } else {
            const auto h2 = fetch_halfword(ctx.padr2, state_.pc + 2);
            if (!h2.has_value()) {
                return;
            }
            ctx.ir_org = (static_cast<uint32_t>(*h2) << 16) | *h1;
        }
    } else {
        Word ir_l = 0;
        Word ir_h = 0;

        simrv::memory::TlChannelA req_l{};
        req_l.opcode = simrv::memory::TlOpcodeA::Get;
        req_l.size = static_cast<uint8_t>(Funct3::Lhu) & 0x3;
        req_l.source = 1;
        req_l.address = ctx.padr1;
        machine.memory_.system_bus().send_request(req_l);
        simrv::memory::TlChannelD resp_l{};
        if (!machine.memory_.system_bus().get_response(1, resp_l) || resp_l.error) {
            ctx.pending_exception = ExceptionCode::FaultFetch;
            ctx.pending_tval = state_.pc;
            return;
        }
        ir_l = resp_l.data;

        simrv::pipeline::Decoder dec_temp(ir_l);
        if (!dec_temp.is_compressed()) {
            const bool translation_enabled =
                state_.priv != kPrivMachine &&
                simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen);
            if (translation_enabled && ctx.padr2 == kWordAllOnes) {
                fetch_resolve_page_walk(machine, 2);
            }

            if (!ctx.pending_exception.has_value()) {
                simrv::memory::TlChannelA req_h{};
                req_h.opcode = simrv::memory::TlOpcodeA::Get;
                req_h.size = static_cast<uint8_t>(Funct3::Lhu) & 0x3;
                req_h.source = 1;
                req_h.address = ctx.padr2;
                machine.memory_.system_bus().send_request(req_h);
                simrv::memory::TlChannelD resp_h{};
                if (!machine.memory_.system_bus().get_response(1, resp_h) || resp_h.error) {
                    ctx.pending_exception = ExceptionCode::FaultFetch;
                    ctx.pending_tval = state_.pc + 2;
                    return;
                }
                ir_h = resp_h.data;
            }
        }

        ctx.ir_org = (ir_h << 16) | (ir_l & 0xFFFF);
    }
}

void CPU::decode_and_normalize_instruction(Machine& machine) {
    auto& ctx = pipeline_context;
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        ctx.ir = isa::RV32_NOP;
        return;
    }

    simrv::pipeline::Decoder dec_org(ctx.ir_org);
    bool const w_compressed = dec_org.is_compressed();
    Instruction const w_ir_tmp = w_compressed ? simrv::pipeline::decompressInstruction(
                                                    ctx.ir_org, state_.current_xlen() == 64)
                                              : ctx.ir_org;

    bool is_valid = true;
    if (simrv::compiler::unlikely(machine.s_misa_profile != kMisaDefault)) {
        is_valid = instruction_enabled_by_misa(state_.misa, w_ir_tmp, w_compressed);
    }

    const isa::OperationId op_id = simrv::pipeline::decoder(w_ir_tmp);
    if (simrv::compiler::unlikely(op_id == isa::UNKNOWN)) {
        if (!machine.s_tuimode) {
            simrv::log::warn("[DECODER] Unknown instruction: PC=0x{:x}, HEX=0x{:x}", state_.pc,
                             w_ir_tmp);
        }
        is_valid = false;
    }

    if (is_valid && state_.regs.xlen == 32 && requires_rv64(op_id)) {
        is_valid = false;
    }

    if (is_valid && state_.regs.xlen == 32) {
        if (op_id == OperationId::SLLI || op_id == OperationId::SRLI ||
            op_id == OperationId::SRAI) {
            if ((funct7_of(w_ir_tmp) & 0x01) != 0) {
                is_valid = false;
            }
        }
    }

    if (is_valid) {
        const auto op = opcode_of(w_ir_tmp);
        if (op == Opcode::Amo) {
            const auto f3 = std::to_underlying(funct3_of(w_ir_tmp));
            if (state_.regs.xlen == 32) {
                if (f3 != 2) {
                    is_valid = false;
                }
            } else {
                if (f3 != 2 && f3 != 3) {
                    is_valid = false;
                }
            }
        }
    }

    if (is_valid) {
        const auto op = opcode_of(w_ir_tmp);
        const bool is_vector =
            (op == Opcode::OpV) ||
            ((op == Opcode::LoadFp || op == Opcode::StoreFp) &&
             (funct3_of(w_ir_tmp) != Funct3::Fld && funct3_of(w_ir_tmp) != Funct3::Fsd &&
              static_cast<uint8_t>(funct3_of(w_ir_tmp)) != 2));
        const bool is_fp_op =
            !is_vector && ((op == Opcode::LoadFp) || (op == Opcode::StoreFp) ||
                           (op == Opcode::OpFp) || (op == Opcode::MAdd) || (op == Opcode::MSub) ||
                           (op == Opcode::NMAdd) || (op == Opcode::NMSub));
        if (is_vector) {
            if (simrv::compiler::unlikely((state_.mstatus & enum_mask(MstatusBit::Vs)) == 0)) {
                if (!machine.s_tuimode) {
                    simrv::log::warn("[VS CHECK] VS is 0! mstatus=0x{:x}, Vs mask=0x{:x}",
                                     state_.mstatus, enum_mask(MstatusBit::Vs));
                }
                is_valid = false;
            }
        } else if (is_fp_op) {
            if (simrv::compiler::unlikely((state_.mstatus & enum_mask(MstatusBit::Fs)) == 0)) {
                is_valid = false;
            }
        }
    }

    if (simrv::compiler::likely(is_valid)) {
        ctx.ir = w_ir_tmp;
        ctx.op_id = op_id;
    } else {
        ctx.pending_exception = ExceptionCode::IllegalInstruction;
        ctx.pending_tval = ctx.ir_org;
        ctx.ir = isa::RV32_NOP;
        ctx.op_id = isa::UNKNOWN;
    }

    ctx.cinsn = w_compressed ? 1U : 0U;
    e_instmix.at(static_cast<std::size_t>(ctx.op_id))++;
}

void CPU::run_fetch_stage_baremetal(Machine& machine) {
    if (state_.regs.xlen == 32) {
        state_.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
    }
    auto& ctx = pipeline_context;
    ctx.tlb_miss = false;
    ctx.cpc = state_.pc;

    const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
    const Word alignment_mask = has_c ? 1u : 3u;
    if ((state_.pc & alignment_mask) != 0) {
        ctx.pending_exception = ExceptionCode::MisalignedFetch;
        ctx.pending_tval = state_.pc;
        ctx.ir = isa::RV32_NOP;
        ctx.op_id = isa::UNKNOWN;
        return;
    }

    ctx.padr1 = (state_.regs.xlen == 32) ? (state_.pc & 0xFFFFFFFFULL) : state_.pc;
    ctx.padr2 = (state_.regs.xlen == 32) ? ((state_.pc + 2) & 0xFFFFFFFFULL) : (state_.pc + 2);

    // Fast path: DRAM physical fetch — valid only while the MMU has never been
    // enabled.  The latch is set once on the first satp write that activates
    // translation, so the branch predictor sees this as "not taken" for nearly
    // all cycles of a physical-only run and switches to "always taken" after
    // the OS enables virtual memory.
    if (simrv::compiler::likely(simrv::memory::is_dram_access(ctx.padr1, sizeof(uint16_t)) &&
                                !machine.s_mmu_ever_used)) {
        const uint16_t h1 = simrv::memory::ram_read_fast(
            ctx.padr1, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
        if ((h1 & 0x3) != 0x3) {
            ctx.ir_org = h1;
        } else {
            if (simrv::compiler::unlikely(
                    !simrv::memory::is_dram_access(ctx.padr2, sizeof(uint16_t)))) {
                ctx.pending_exception = ExceptionCode::FaultFetch;
                ctx.pending_tval = state_.pc + 2;
                ctx.ir = isa::RV32_NOP;
                ctx.op_id = isa::UNKNOWN;
                return;
            }
            const uint16_t h2 = simrv::memory::ram_read_fast(
                ctx.padr2, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
            ctx.ir_org = (static_cast<uint32_t>(h2) << 16) | h1;
        }
    } else {
        // Slow path: MMU may be active.  Compute translation_enabled here (not
        // on every cycle in the fast path above).
        const bool split_page = ((state_.pc & ~simrv::memory::kPageMask) !=
                                 ((state_.pc + 2) & ~simrv::memory::kPageMask));
        const bool translation_enabled =
            state_.priv != kPrivMachine &&
            simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen);

        fetch_address_translate(machine);

        if (simrv::compiler::unlikely(translation_enabled)) {
            fetch_resolve_page_walk(machine, 1);
            if (simrv::compiler::likely(!split_page)) {
                if (!ctx.pending_exception.has_value() && ctx.padr1 != kWordAllOnes) {
                    ctx.padr2 = ctx.padr1 + 2;
                }
            }
        }

        fetch_read_instruction_word(machine);
    }
    decode_and_normalize_instruction(machine);
}

// ==========================================
// ID (Instruction Decode) Stage
// ==========================================

void CPU::run_decode_stage(Machine& machine) {
    decode_fields(machine);
    fetch_operands(machine);
}

void CPU::decode_fields(Machine& /*machine*/) {
    auto& ctx = pipeline_context;
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        return;
    }

    simrv::pipeline::Decoder dec(ctx.ir);

    ctx.opcode = static_cast<Opcode>(dec.opcode());
    ctx.rd = dec.rd();
    ctx.rs1 = dec.rs1();
    ctx.rs2 = dec.rs2();
    ctx.funct3 = static_cast<Funct3>(dec.funct3());
    ctx.funct5 = static_cast<Funct5Amo>((ctx.ir >> 27) & 0x1F);
    ctx.funct7 = dec.funct7();
    ctx.funct12 = (ctx.ir >> 20);

    switch (dec.opcode()) {
        case Opcode::Lui:
        case Opcode::Auipc:
            ctx.imm = dec.imm_u();
            break;
        case Opcode::Jal:
            ctx.imm = dec.imm_j();
            break;
        case Opcode::Branch:
            ctx.imm = dec.imm_b();
            break;
        case Opcode::Store:
        case Opcode::StoreFp:
            ctx.imm = dec.imm_s();
            break;
        default:
            ctx.imm = dec.imm_i();
            break;
    }
}

void CPU::fetch_operands(Machine& /*machine*/) {
    auto& ctx = pipeline_context;
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        return;
    }

    const auto opcode = ctx.opcode;
    const auto funct3 = ctx.funct3;
    const Instruction funct12 = ctx.funct12;

    ctx.rrs1 = state_.regs.read(ctx.rs1);
    ctx.rrs2 = state_.regs.read(ctx.rs2);

    if (simrv::compiler::likely(opcode != Opcode::System)) {
        ctx.rcsr = 0;
        return;
    }

    CSRAddress const w_csr_addr =
        (funct3 != Funct3::Priv) ? static_cast<CSRAddress>(funct12)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Ecall)) ? csr_addr(Csr::Mtvec)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Uret))  ? csr_addr(Csr::Uepc)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Sret))  ? csr_addr(Csr::Sepc)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Mret))  ? csr_addr(Csr::Mepc)
                                                                    : 0;

    if (funct3 == Funct3::Priv) {
        if (!TrapController::canExecutePrivilegedInstruction(state_.priv, state_.misa,
                                                             state_.mstatus, funct12, ctx.funct7)) {
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir_org;
            return;
        }
    } else {
        const bool is_write =
            ((static_cast<uint8_t>(funct3) & 0x3u) == 0x1u) || (std::to_underlying(ctx.rs1) != 0);
        if (!TrapController::canAccessCsr(state_.priv, state_.misa, w_csr_addr, is_write)) {
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir_org;
            return;
        }
        if (w_csr_addr == csr_addr(Csr::Satp) && state_.priv == kPrivSupervisor &&
            (state_.mstatus & enum_mask(MstatusBit::Tvm)) != 0) {
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir_org;
            return;
        }

        if (static_cast<PrivilegeLevel>(std::to_underlying(state_.priv)) < kPrivMachine) {
            if ((w_csr_addr >= 0xC00 && w_csr_addr <= 0xC1F) ||
                (w_csr_addr >= 0xC80 && w_csr_addr <= 0xC9F)) {
                const Word counter_bit = 1u << (w_csr_addr & 0x1Fu);
                bool access_denied = (state_.mcounteren & counter_bit) == 0;
                if (state_.priv == kPrivUser) {
                    access_denied = access_denied || ((state_.scounteren & counter_bit) == 0);
                }
                if (access_denied) {
                    ctx.pending_exception = ExceptionCode::IllegalInstruction;
                    ctx.pending_tval = ctx.ir_org;
                    return;
                }
            }
        }
    }

    if (simrv::compiler::unlikely(w_csr_addr == csr_addr(Csr::Fflags) ||
                                  w_csr_addr == csr_addr(Csr::Frm) ||
                                  w_csr_addr == csr_addr(Csr::Fcsr))) {
        if ((state_.mstatus & enum_mask(MstatusBit::Fs)) == 0) {
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir_org;
            return;
        }
    }

    if (funct3 != Funct3::Priv) {
        auto res = read_csr(w_csr_addr);
        if (!res) {
            ctx.pending_exception = res.error();
            ctx.pending_tval = ctx.ir_org;
            return;
        }
        ctx.rcsr = *res;
        ctx.rcsr_write = ctx.rcsr;
        if (w_csr_addr == csr_addr(Csr::Mip)) {
            // SEIP reads as software || PLIC, but CSRRS/CSRRC operate only on
            // the software-writable component (Privileged ISA 1.13).
            ctx.rcsr_write = mip_rmw_base(ctx.rcsr, state_.seip_software);
        }
    } else {
        if (w_csr_addr != 0) {
            auto res = read_csr(w_csr_addr);
            if (res) ctx.rcsr = *res;
        }
    }
}

// ==========================================
// EX (Execute) Stage
// ==========================================

void CPU::run_execute_stage(Machine& machine) { execute_core(machine); }

void CPU::execute_core(Machine& machine) {
    auto& ctx = pipeline_context;
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        return;
    }

    if (ctx.op_id >= isa::OperationId::VSETVLI && ctx.op_id <= isa::OperationId::VWSLL_VI) {
        ctx.tkn = false;
        execute::ExecuteUnit::execute_vector(*this, machine, ctx.op_id, ctx.ir);
        // VS dirty tracking may be imprecise. Conservatively mark it Dirty
        // after dispatch because vector instructions can update registers,
        // vl/vtype/vstart/vxsat, or partial state before a restartable fault.
        state_.mstatus |= enum_mask(MstatusBit::Vs);
        return;
    }

    ctx.fp_wb_enable = false;
    ctx.int_wb_from_fp = false;

    switch (ctx.opcode) {
        case Opcode::Lui:
            ctx.tkn = false;
            ctx.wb_data = ctx.imm;
            break;
        case Opcode::Auipc:
            ctx.tkn = false;
            ctx.wb_data = state_.pc + ctx.imm;
            break;
        case Opcode::Jal:
            ctx.tkn = true;
            ctx.wb_data = state_.pc + ((ctx.cinsn != 0u) ? 2 : 4);
            ctx.jmp_pc = state_.pc + ctx.imm;
            break;
        case Opcode::Jalr:
            ctx.tkn = true;
            ctx.wb_data = state_.pc + ((ctx.cinsn != 0u) ? 2 : 4);
            ctx.jmp_pc = (ctx.rrs1 + ctx.imm) & ~static_cast<Register>(1);
            if (state_.regs.xlen == 32) {
                ctx.jmp_pc =
                    static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(ctx.jmp_pc)));
            }
            break;
        case Opcode::Op:
            ctx.tkn = false;
            ctx.wb_data =
                execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.rrs2, ctx.op_id, state_.regs.xlen);
            break;
        case Opcode::OpImm:
            ctx.tkn = false;
            ctx.wb_data =
                execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.imm, ctx.op_id, state_.regs.xlen);
            break;
        case Opcode::OpImm32:
            ctx.tkn = false;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(ctx.rrs1, ctx.imm, ctx.op_id);
            break;
        case Opcode::Op32:
            ctx.tkn = false;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(ctx.rrs1, ctx.rrs2, ctx.op_id);
            break;
        case Opcode::Load:
        case Opcode::LoadFp:
        case Opcode::Store:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            break;
        case Opcode::StoreFp:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            ctx.fp_mem_wdata = state_.regs.read_fp(ctx.rs2);
            break;
        case Opcode::MiscMem:
            ctx.tkn = false;
            if (ctx.funct3 == Funct3::FenceI) {
                icache.flush();
                dcache.flush();
                decode_cache.flush();
            } else if (ctx.ir == 0x0100000f) {
                // RISC-V PAUSE (Zihintpause / cpu_relax): architectural NOP pipeline hint
            }
            break;
        case Opcode::Branch:
            ctx.tkn =
                execute::ExecuteUnit::branchTaken(ctx.rrs1, ctx.rrs2, ctx.funct3, state_.regs.xlen);
            ctx.jmp_pc = state_.pc + ctx.imm;
            if (state_.regs.xlen == 32) {
                ctx.jmp_pc =
                    static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(ctx.jmp_pc)));
            }
            break;
        case Opcode::Amo:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1;
            if (ctx.funct5 == Funct5Amo::Sc) {
                // Zalrsc requires natural alignment even when the reservation will make SC fail.
                // A failed reservation must not bypass the architecturally required exception.
                if (!amo_address_aligned(ctx.mem_addr, ctx.funct3)) {
                    ctx.pending_exception = ExceptionCode::MisalignedStore;
                    ctx.pending_tval = ctx.mem_addr;
                    break;
                }
                const bool native_success =
                    machine.memory_.reservation_table().check_and_clear_reservation(
                        static_cast<HartId>(state_.mhartid), ctx.rrs1);
                state_.reserved = native_success ? 1 : 0;
                state_.load_res = ctx.rrs1;
                ctx.wb_data = native_success ? 0 : 1;
                if (machine.spike_lockstep && machine.spike_lockstep->is_running()) {
                    if (native_success) {
                        auto sc_success_opt = machine.spike_lockstep->determine_sc_success();
                        if (sc_success_opt.has_value() && !sc_success_opt.value()) {
                            ctx.wb_data = 1;
                        }
                    }
                }
            }
            break;
        case Opcode::System:
            execute_system(machine);
            break;
        case Opcode::Custom0:
            ctx.tkn = false;
            break;
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub:
        case Opcode::OpFp:
            execute_fp(machine);
            break;
        default:
            ctx.tkn = false;
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir;
            break;
    }
}

void CPU::execute_system(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.funct3 == Funct3::Priv) {
        switch (static_cast<Funct12Priv>(ctx.funct12)) {
            case Funct12Priv::Ecall:
                ctx.wb_data_csr =
                    enum_mask(ExceptionCode::UserEcall) + std::to_underlying(state_.priv);
                ctx.pending_exception = static_cast<ExceptionCode>(
                    enum_mask(ExceptionCode::UserEcall) + std::to_underlying(state_.priv));
                e_icount++;
                break;
            case Funct12Priv::Ebreak: {
                bool semihost_handled = false;
                const bool in_dram = simrv::memory::is_dram_addr(state_.pc - 4) &&
                                     simrv::memory::is_dram_addr(state_.pc + 4);
                if (in_dram) {
                    const Word inst_prev = simrv::memory::ram_read_fast(
                        state_.pc - 4, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                    const Word inst_next = simrv::memory::ram_read_fast(
                        state_.pc + 4, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                    if (inst_prev == 0x01f01013 && inst_next == 0x40705013) {
                        semihost_handled = true;
                        const Word semihost_op = state_.regs.read(RegId::A0);
                        const Address arg_ptr = state_.regs.read(RegId::A1);

                        switch (semihost_op) {
                            case 0x05: {
                                const Instruction load_op =
                                    kIsXLen64 ? static_cast<Instruction>(Funct3::Ld)
                                              : static_cast<Instruction>(Funct3::Lw);
                                const Address fd =
                                    simrv::memory::ram_read_fast(arg_ptr, load_op, machine.mmem);
                                const Address buf_addr = simrv::memory::ram_read_fast(
                                    arg_ptr + (kIsXLen64 ? 8 : 4), load_op, machine.mmem);
                                const Address len = simrv::memory::ram_read_fast(
                                    arg_ptr + (kIsXLen64 ? 16 : 8), load_op, machine.mmem);
                                (void)fd;

                                if (simrv::memory::is_dram_addr(buf_addr)) {
                                    for (Address i = 0; i < len; ++i) {
                                        const auto ch = static_cast<uint8_t>(
                                            simrv::memory::ram_read_fast(
                                                buf_addr + i, static_cast<Instruction>(Funct3::Lb),
                                                machine.mmem) &
                                            0xFF);
                                        if (machine.s_tuimode && machine.tui) {
                                            machine.tui->handle_char_write(static_cast<char>(ch));
                                        } else {
                                            (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                                        }
                                    }
                                }
                                state_.regs.write(RegId::A0, 0);
                                break;
                            }
                            case 0x03: {
                                if (simrv::memory::is_dram_addr(arg_ptr)) {
                                    const auto ch = static_cast<uint8_t>(
                                        simrv::memory::ram_read_fast(
                                            arg_ptr, static_cast<Instruction>(Funct3::Lb),
                                            machine.mmem) &
                                        0xFF);
                                    if (machine.s_tuimode && machine.tui) {
                                        machine.tui->handle_char_write(static_cast<char>(ch));
                                    } else {
                                        (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                                    }
                                }
                                state_.regs.write(RegId::A0, 0);
                                break;
                            }
                            case 0x04: {
                                Address ptr = arg_ptr;
                                if (simrv::memory::is_dram_addr(ptr)) {
                                    while (true) {
                                        const auto ch = static_cast<uint8_t>(
                                            simrv::memory::ram_read_fast(
                                                ptr, static_cast<Instruction>(Funct3::Lb),
                                                machine.mmem) &
                                            0xFF);
                                        if (ch == 0) break;
                                        if (machine.s_tuimode && machine.tui) {
                                            machine.tui->handle_char_write(static_cast<char>(ch));
                                        } else {
                                            (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                                        }
                                        ptr++;
                                    }
                                }
                                state_.regs.write(RegId::A0, 0);
                                break;
                            }
                            default:
                                simrv::log::warn("__ Unhandled semihosting op: 0x{:02x}",
                                                 semihost_op);
                                state_.regs.write(RegId::A0, static_cast<Word>(-1));
                                break;
                        }

                        ctx.tkn = true;
                        ctx.jmp_pc = state_.pc + 8;
                    }
                }

                if (!semihost_handled) {
                    ctx.wb_data_csr = enum_mask(ExceptionCode::Breakpoint);
                    ctx.pending_exception = ExceptionCode::Breakpoint;
                    ctx.tkn = false;
                    e_icount++;
                }
                break;
            }
            case Funct12Priv::Uret:
            case Funct12Priv::Sret:
            case Funct12Priv::Mret:
                ctx.tkn = true;
                ctx.jmp_pc = ctx.rcsr;
                break;
            case Funct12Priv::Wfi: {
                ctx.tkn = false;
                break;
            }
            default:
                if (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                    ctx.tkn = false;
                }
                break;
        }
    } else {
        const auto csr_val_imm = ((std::to_underlying(ctx.funct3) & 4) != 0)
                                     ? static_cast<ImmValue>(std::to_underlying(ctx.rs1))
                                     : ctx.imm;
        auto csr_result =
            execute::ExecuteUnit::csrWriteValue(ctx.rcsr_write, ctx.rrs1, csr_val_imm, ctx.funct3);
        if (csr_result.has_value()) {
            ctx.tkn = false;
            ctx.wb_data_csr = csr_result.value();
        } else {
            ctx.pending_exception = static_cast<ExceptionCode>(csr_result.error());
            ctx.pending_tval = ctx.ir;
        }
    }
}

void CPU::execute_fp(Machine& /*machine*/) {
    auto& ctx = pipeline_context;
    switch (ctx.opcode) {
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub: {
            ctx.tkn = false;
            const Word rm =
                (enum_mask(ctx.funct3) == 7) ? ((state_.fcsr >> 5) & 0x7) : enum_mask(ctx.funct3);
            if (simrv::compiler::unlikely(rm >= 5)) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const Word fmt = ctx.funct7 & 0x3;
            const Word rs3 = (ctx.ir >> 27) & 0x1F;
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp = execute::ExecuteUnit::fusedFp(
                ctx.opcode, fmt, std::to_underlying(ctx.rs1), std::to_underlying(ctx.rs2), rs3,
                enum_mask(ctx.funct3), state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mstatus |= enum_mask(MstatusBit::Fs);
            }
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        case Opcode::OpFp: {
            ctx.tkn = false;
            const Word rm =
                (enum_mask(ctx.funct3) == 7) ? ((state_.fcsr >> 5) & 0x7) : enum_mask(ctx.funct3);
            if (simrv::compiler::unlikely(rm >= 5)) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp = execute::ExecuteUnit::opFp(
                ctx.funct7, ctx.funct3, std::to_underlying(ctx.rs1), std::to_underlying(ctx.rs2),
                ctx.rrs1, state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mstatus |= enum_mask(MstatusBit::Fs);
            }
            ctx.wb_data = fp.int_wb_data;
            ctx.int_wb_from_fp = fp.int_wb_enable;
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        default:
            break;
    }
}

// ==========================================
// MEM (Memory) Stage
// ==========================================

void CPU::run_memory_stage(Machine& machine) {
    if (pipeline_context.op_id >= isa::OperationId::VSETVLI &&
        pipeline_context.op_id <= isa::OperationId::VWSLL_VI) {
        return;
    }
    memory_load_phase(machine);
    memory_prepare_store_data(machine);
    memory_store_phase(machine);
}

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
        machine.memory_.reservation_table().set_reservation(static_cast<HartId>(state_.mhartid),
                                                            ctx.mem_addr);
    }
}

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

    if ((opcode == Opcode::Store) || (opcode == Opcode::StoreFp) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr)) {
        if (!ctx.pending_exception.has_value()) {
            state_.reserved = 0;
            machine.memory_.reservation_table().invalidate_matching(
                ctx.mem_addr, static_cast<HartId>(state_.mhartid));
        }
    }
}

// ==========================================
// WB (Writeback) Stage
// ==========================================

void CPU::run_writeback_stage(Machine& machine) { writeback_registers(machine); }

void CPU::writeback_registers([[maybe_unused]] Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    e_icount++;

    if (ctx.op_id >= isa::OperationId::VSETVLI && ctx.op_id <= isa::OperationId::VWSLL_VI) {
        return;
    }

    Word wire_wb_r_data = 0;
    Word wire_wb_r_enable = 0;

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);
    const auto funct3 = static_cast<Funct3>(ctx.funct3);

    if ((opcode == Opcode::Load) || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        wire_wb_r_data = ctx.mem_rdata;
        wire_wb_r_enable = 1;
    } else if (opcode == Opcode::System && funct3 != Funct3::Priv) {
        wire_wb_r_data = ctx.rcsr;
        wire_wb_r_enable = 1;
    } else {
        if ((opcode == Opcode::Amo && funct5 == Funct5Amo::Sc) || (opcode == Opcode::Lui) ||
            (opcode == Opcode::Auipc) || (opcode == Opcode::Jal) || (opcode == Opcode::Jalr) ||
            (opcode == Opcode::Op) || (opcode == Opcode::OpImm) || (opcode == Opcode::Op32) ||
            (opcode == Opcode::OpImm32) || (opcode == Opcode::OpFp && (ctx.int_wb_from_fp != 0u))) {
            wire_wb_r_data = ctx.wb_data;
            wire_wb_r_enable = 1;
        }
    }

    if (opcode == Opcode::LoadFp) {
        state_.regs.write_fp(ctx.rd, ctx.fp_mem_rdata);
        state_.mstatus |= enum_mask(MstatusBit::Fs);
    }

    if ((opcode == Opcode::OpFp || opcode == Opcode::MAdd || opcode == Opcode::MSub ||
         opcode == Opcode::NMAdd || opcode == Opcode::NMSub) &&
        (ctx.fp_wb_enable != 0u)) {
        state_.regs.write_fp(ctx.rd, ctx.fp_wb_data);
        state_.mstatus |= enum_mask(MstatusBit::Fs);
    }

    if (wire_wb_r_enable != 0u) {
        state_.regs.write(ctx.rd, wire_wb_r_data);
    }
}

// ==========================================
// COMMIT (Commit) Stage
// ==========================================

void CPU::run_commit_stage(Machine& machine) { commit_control_flow_and_traps(machine); }

void CPU::commit_control_flow_and_traps([[maybe_unused]] Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.cinsn != 0u && !ctx.pending_exception.has_value()) {
        e_ccount++;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct3 = static_cast<Funct3>(ctx.funct3);

    if (!ctx.pending_exception.has_value() && opcode == Opcode::System) {
        if (funct3 == Funct3::Priv) {
            switch (static_cast<Funct12Priv>(ctx.funct12)) {
                case Funct12Priv::Uret: {
                    break;
                }
                case Funct12Priv::Sret: {
                    sret();
                    break;
                }
                case Funct12Priv::Mret: {
                    mret();
                    break;
                }
                default:
                    if (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                        const bool match_all_vaddr = (std::to_underlying(ctx.rs1) == 0);
                        const bool match_all_asid = (std::to_underlying(ctx.rs2) == 0);
                        TLB_flush(match_all_vaddr, ctx.rrs1, match_all_asid,
                                  static_cast<Word>(ctx.rrs2));
                    }
                    break;
            }
        } else {
            const bool is_write = (funct3 == Funct3::Csrrw || funct3 == Funct3::Csrrwi) ||
                                  (std::to_underlying(ctx.rs1) != 0);
            if (is_write) {
                auto res = write_csr(static_cast<CSRAddress>(ctx.funct12), ctx.wb_data_csr);
                if (!res) {
                    ctx.pending_exception = res.error();
                }
            }
        }
    }

    Word const pending_interrupts = state_.mip & state_.mie;
    Word enable_interrupts = 0;
    Word mask = 0;
    Word irq_num = 32;
    if (pending_interrupts != 0u) {
        switch (state_.priv) {
            case kPrivMachine: {
                if ((state_.mstatus & enum_mask(MstatusBit::Mie)) != 0u) {
                    enable_interrupts = ~state_.mideleg;
                }
                break;
            }
            case kPrivSupervisor: {
                enable_interrupts = ~state_.mideleg;
                if ((state_.mstatus & enum_mask(MstatusBit::Sie)) != 0u) {
                    enable_interrupts |= state_.mideleg;
                }
                break;
            }
            case kPrivUser: {
                enable_interrupts = ~0;
                break;
            }
            default:
                break;
        }
        mask = pending_interrupts & enable_interrupts;
        if (mask != 0) {
            irq_num = select_highest_priority_interrupt(mask);
        }
    }
    if (ctx.pending_exception.has_value()) {
        raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
    } else {
        if (ctx.tkn != 0u) {
            const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
            const Word alignment_mask = has_c ? 1u : 3u;
            if ((ctx.jmp_pc & alignment_mask) != 0) {
                ctx.pending_exception = ExceptionCode::MisalignedFetch;
                ctx.pending_tval = ctx.jmp_pc;
                raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
                return;
            }
            state_.pc = ctx.jmp_pc;
        } else {
            state_.pc = state_.pc + ((ctx.cinsn != 0u) ? 2 : 4);
        }
        if (state_.regs.xlen == 32) {
            state_.pc =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
        }
        if (mask != 0) {
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
}

void CPU::run_commit_stage_baremetal([[maybe_unused]] Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.cinsn != 0u && !ctx.pending_exception.has_value()) {
        e_ccount++;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct3 = static_cast<Funct3>(ctx.funct3);

    if (!ctx.pending_exception.has_value() && opcode == Opcode::System) {
        if (funct3 == Funct3::Priv) {
            switch (static_cast<Funct12Priv>(ctx.funct12)) {
                case Funct12Priv::Uret: {
                    break;
                }
                case Funct12Priv::Sret: {
                    sret();
                    break;
                }
                case Funct12Priv::Mret: {
                    mret();
                    break;
                }
                default:
                    if (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                        const bool match_all_vaddr = (std::to_underlying(ctx.rs1) == 0);
                        const bool match_all_asid = (std::to_underlying(ctx.rs2) == 0);
                        TLB_flush(match_all_vaddr, ctx.rrs1, match_all_asid,
                                  static_cast<Word>(ctx.rrs2));
                    }
                    break;
            }
        } else {
            const bool is_write = (funct3 == Funct3::Csrrw || funct3 == Funct3::Csrrwi) ||
                                  (std::to_underlying(ctx.rs1) != 0);
            if (is_write) {
                auto res = write_csr(static_cast<CSRAddress>(ctx.funct12), ctx.wb_data_csr);
                if (!res) {
                    ctx.pending_exception = res.error();
                }
            }
        }
    }

    Word const pending_interrupts = state_.mip & state_.mie;
    Word enable_interrupts = 0;
    Word mask = 0;
    Word irq_num = 32;
    if (simrv::compiler::unlikely(pending_interrupts != 0u)) {
        switch (state_.priv) {
            case kPrivMachine: {
                if ((state_.mstatus & enum_mask(MstatusBit::Mie)) != 0u) {
                    enable_interrupts = ~state_.mideleg;
                }
                break;
            }
            case kPrivSupervisor: {
                enable_interrupts = ~state_.mideleg;
                if ((state_.mstatus & enum_mask(MstatusBit::Sie)) != 0u) {
                    enable_interrupts |= state_.mideleg;
                }
                break;
            }
            case kPrivUser: {
                enable_interrupts = ~0;
                break;
            }
            default:
                break;
        }
        mask = pending_interrupts & enable_interrupts;
        if (mask != 0) {
            irq_num = select_highest_priority_interrupt(mask);
        }
    }

    if (ctx.pending_exception.has_value()) {
        raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
    } else {
        if (ctx.tkn != 0u) {
            const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
            const Word alignment_mask = has_c ? 1u : 3u;
            if ((ctx.jmp_pc & alignment_mask) != 0) {
                ctx.pending_exception = ExceptionCode::MisalignedFetch;
                ctx.pending_tval = ctx.jmp_pc;
                raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
                return;
            }
            state_.pc = ctx.jmp_pc;
        } else {
            state_.pc = state_.pc + ((ctx.cinsn != 0u) ? 2 : 4);
        }
        if (state_.regs.xlen == 32) {
            state_.pc =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
        }
        if (simrv::compiler::unlikely(mask != 0)) {
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
}

}  // namespace simrv::core
