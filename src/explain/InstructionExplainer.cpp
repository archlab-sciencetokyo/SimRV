/**
 * @file InstructionExplainer.cpp
 * @brief Formal architectural instruction decoder and side-effect-free state-grounded semantic
 *        evaluator.
 *
 * Normative descriptions and formulas cite the official ratified RISC-V International manuals:
 * - The RISC-V Instruction Set Manual, Volume I: Unprivileged Architecture (Version 20260120)
 * - The RISC-V Instruction Set Manual, Volume II: Privileged Architecture (Version 20260120)
 */
#include "simrv/explain/InstructionExplainer.hpp"

#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CsrFile.hpp"
#include "simrv/core/CsrTypes.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/core/TrapController.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/execute/ExecuteUnitInt.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/RegisterNames.hpp"
#include "simrv/isa/VectorTypes.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/OperationTraits.hpp"

namespace simrv::explain {

using simrv::core::Csr;
using simrv::isa::InstFormat;
using simrv::isa::Opcode;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;
using enum simrv::core::Csr;

namespace {

constexpr auto& ABI_NAMES = simrv::isa::kGprPedagogicalNames;
constexpr auto& FP_ABI_NAMES = simrv::isa::kFprAbiNames;

auto has_rd_field(InstFormat fmt) -> bool {
    return fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::U ||
           fmt == InstFormat::J || fmt == InstFormat::R4;
}

auto has_rs1_field(InstFormat fmt) -> bool {
    return fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::S ||
           fmt == InstFormat::B || fmt == InstFormat::R4;
}

auto has_rs2_field(InstFormat fmt) -> bool {
    return fmt == InstFormat::R || fmt == InstFormat::S || fmt == InstFormat::B ||
           fmt == InstFormat::R4;
}

auto has_imm_field(InstFormat fmt) -> bool {
    return fmt == InstFormat::I || fmt == InstFormat::S || fmt == InstFormat::B ||
           fmt == InstFormat::U || fmt == InstFormat::J;
}

auto build_bitfields(InstFormat fmt, uint32_t ir_org) -> std::vector<BitFieldEntry> {
    std::vector<BitFieldEntry> fields;
    auto add_field = [&](std::string_view label, unsigned high, unsigned low) {
        const unsigned bit_count = high - low + 1;
        const uint32_t mask = (bit_count >= 32) ? 0xFFFFFFFFU : ((uint32_t{1} << bit_count) - 1);
        const uint32_t value = (ir_org >> low) & mask;
        fields.push_back(BitFieldEntry{
            .label = std::string(label),
            .high_bit = high,
            .low_bit = low,
            .value = value,
            .bit_count = bit_count,
        });
    };

    switch (fmt) {
        case InstFormat::R:
            add_field("funct7", 31, 25);
            add_field("rs2", 24, 20);
            add_field("rs1", 19, 15);
            add_field("funct3", 14, 12);
            add_field("rd", 11, 7);
            add_field("opcode", 6, 0);
            break;
        case InstFormat::I:
            add_field("imm[11:0]", 31, 20);
            add_field("rs1", 19, 15);
            add_field("funct3", 14, 12);
            add_field("rd", 11, 7);
            add_field("opcode", 6, 0);
            break;
        case InstFormat::S:
            add_field("imm[11:5]", 31, 25);
            add_field("rs2", 24, 20);
            add_field("rs1", 19, 15);
            add_field("funct3", 14, 12);
            add_field("imm[4:0]", 11, 7);
            add_field("opcode", 6, 0);
            break;
        case InstFormat::B:
            add_field("imm[12|10:5]", 31, 25);
            add_field("rs2", 24, 20);
            add_field("rs1", 19, 15);
            add_field("funct3", 14, 12);
            add_field("imm[4:1|11]", 11, 7);
            add_field("opcode", 6, 0);
            break;
        case InstFormat::U:
            add_field("imm[31:12]", 31, 12);
            add_field("rd", 11, 7);
            add_field("opcode", 6, 0);
            break;
        case InstFormat::J:
            add_field("imm[20|10:1|11|19:12]", 31, 12);
            add_field("rd", 11, 7);
            add_field("opcode", 6, 0);
            break;
        case InstFormat::R4:
            add_field("rs3", 31, 27);
            add_field("fmt", 26, 25);
            add_field("rs2", 24, 20);
            add_field("rs1", 19, 15);
            add_field("funct3", 14, 12);
            add_field("rd", 11, 7);
            add_field("opcode", 6, 0);
            break;
        default:
            break;
    }
    return fields;
}

auto is_unary_float_op(OperationId op_id) -> bool {
    return op_id == FSQRT_S || op_id == FSQRT_D || op_id == FCLASS_S || op_id == FCLASS_D ||
           op_id == FMV_X_W || op_id == FMV_X_D || op_id == FMV_W_X || op_id == FMV_D_X ||
           (op_id >= FCVT_W_S && op_id <= FCVT_LU_S) || (op_id >= FCVT_W_D && op_id <= FCVT_LU_D) ||
           (op_id >= FCVT_S_W && op_id <= FCVT_S_LU) || (op_id >= FCVT_D_W && op_id <= FCVT_D_LU);
}

auto is_shift_imm_op(OperationId op_id) -> bool {
    return op_id == SLLI || op_id == SRLI || op_id == SRAI || op_id == SLLIW || op_id == SRLIW ||
           op_id == SRAIW;
}

auto is_csr_imm_op(OperationId op_id) -> bool {
    return op_id == CSRRWI || op_id == CSRRSI || op_id == CSRRCI;
}

auto format_vector_assembly(OperationId op_id, std::string_view mnemonic, simrv::xlen::RegId rd_id,
                            simrv::xlen::RegId rs1_id, simrv::xlen::RegId rs2_id,
                            simrv::xlen::ImmValue imm) -> std::string {
    auto vr_name = [](simrv::xlen::RegId r) -> std::string {
        return std::format("v{}", std::to_underlying(r));
    };
    auto gpr_name = [](simrv::xlen::RegId r) -> std::string {
        return std::string(ABI_NAMES.at(std::to_underlying(r) & 0x1FU));
    };
    auto fpr_name = [](simrv::xlen::RegId r) -> std::string {
        return std::string(FP_ABI_NAMES.at(std::to_underlying(r) & 0x1FU));
    };

    std::string rd = vr_name(rd_id);
    std::string rs1 = gpr_name(rs1_id);
    std::string rs2 = vr_name(rs2_id);

    if (op_id == VSETVLI) {
        return std::format("vsetvli {}, {}, {}", gpr_name(rd_id), gpr_name(rs1_id), imm);
    }
    if (op_id == VSETIVLI) {
        uint32_t uimm = std::to_underlying(rs1_id);
        return std::format("vsetivli {}, {}, {}", gpr_name(rd_id), uimm, imm);
    }
    if (op_id == VSETVL) {
        return std::format("vsetvl {}, {}, {}", gpr_name(rd_id), gpr_name(rs1_id),
                           gpr_name(rs2_id));
    }
    if ((op_id >= VLE8_V && op_id <= VLE64_V) || (op_id >= VSE8_V && op_id <= VSE64_V)) {
        return std::format("{} {}, ({})", mnemonic, rd, rs1);
    }
    if ((op_id >= VLSE8_V && op_id <= VLSE64_V) || (op_id >= VSSE8_V && op_id <= VSSE64_V)) {
        return std::format("{} {}, ({}), {}", mnemonic, rd, rs1, gpr_name(rs2_id));
    }
    if ((op_id >= VLUXEI8_V && op_id <= VLUXEI64_V) ||
        (op_id >= VLOXEI8_V && op_id <= VLOXEI64_V) ||
        (op_id >= VSUXEI8_V && op_id <= VSUXEI64_V) ||
        (op_id >= VSOXEI8_V && op_id <= VSOXEI64_V)) {
        return std::format("{} {}, ({}), {}", mnemonic, rd, rs1, rs2);
    }
    if (op_id == VID_V) {
        return std::format("vid.v {}", rd);
    }
    if (mnemonic.ends_with(".vv") || mnemonic.ends_with(".vvm") || mnemonic.ends_with(".MM")) {
        return std::format("{} {}, {}, {}", mnemonic, rd, rs2, vr_name(rs1_id));
    }
    if (mnemonic.ends_with(".vx") || mnemonic.ends_with(".vxm") || mnemonic.ends_with(".VX")) {
        return std::format("{} {}, {}, {}", mnemonic, rd, rs2, rs1);
    }
    if (mnemonic.ends_with(".vi") || mnemonic.ends_with(".vim") || mnemonic.ends_with(".VI")) {
        auto simm = static_cast<int32_t>(imm);
        return std::format("{} {}, {}, {}", mnemonic, rd, rs2, simm);
    }
    if (mnemonic.ends_with(".vf") || mnemonic.ends_with(".VF")) {
        return std::format("{} {}, {}, {}", mnemonic, rd, rs2, fpr_name(rs1_id));
    }
    if (op_id == VMV_X_S) {
        return std::format("vmv.x.s {}, {}", gpr_name(rd_id), rs2);
    }
    if (op_id == VMV_S_X) {
        return std::format("vmv.s.x {}, {}", rd, rs1);
    }
    return std::format("{} {}, {}, {}", mnemonic, rd, vr_name(rs1_id), rs2);
}

auto format_canonical_assembly(const InstructionExplanation& expl, const std::string& rd_abi,
                               const std::string& rs1_abi, const std::string& rs2_abi,
                               const std::string& rs3_abi) -> std::string {
    using namespace simrv::pipeline::operation;
    const auto op_id = expl.op_id;
    const auto fmt = expl.format;
    const auto mnemonic = expl.spec.mnemonic;

    if (op_id == ILLEGAL) {
        return "unknown / illegal instruction";
    }
    if (expl.opcode == Opcode::OpV) {
        return format_vector_assembly(op_id, mnemonic, expl.rd.reg_id, expl.rs1.reg_id,
                                      expl.rs2.reg_id, expl.imm);
    }
    if (fmt == InstFormat::R) {
        if (op_id == SFENCE_VMA) {
            return std::format("sfence.vma {}, {}", rs1_abi, rs2_abi);
        }
        if ((op_id >= LR_W && op_id <= SC_W) || (op_id >= LR_D && op_id <= SC_D)) {
            if (op_id == LR_W) return std::format("lr.w {}, ({})", rd_abi, rs1_abi);
            if (op_id == LR_D) return std::format("lr.d {}, ({})", rd_abi, rs1_abi);
            if (op_id == SC_W) return std::format("sc.w {}, {}, ({})", rd_abi, rs2_abi, rs1_abi);
            return std::format("sc.d {}, {}, ({})", rd_abi, rs2_abi, rs1_abi);
        }
        if ((op_id >= AMOSWAP_W && op_id <= AMOMAXU_W) ||
            (op_id >= AMOSWAP_D && op_id <= AMOMAXU_D)) {
            return std::format("{}.aqrl {}, {}, ({})", mnemonic, rd_abi, rs2_abi, rs1_abi);
        }
        if (is_unary_float_op(op_id)) {
            return std::format("{} {}, {}", mnemonic, rd_abi, rs1_abi);
        }
        if (op_id == CFU) {
            return std::format("cfu func{}, {}, {}, {} [f7=0x{:02X}]",
                               std::to_underlying(expl.funct3), rd_abi, rs1_abi, rs2_abi,
                               static_cast<uint32_t>(expl.funct7));
        }
        return std::format("{} {}, {}, {}", mnemonic, rd_abi, rs1_abi, rs2_abi);
    }
    if (fmt == InstFormat::I) {
        if (is_load(op_id)) {
            return std::format("{} {}, {}({})", mnemonic, rd_abi, expl.imm, rs1_abi);
        }
        if (op_id == JALR) {
            return std::format("jalr {}, {}({})", rd_abi, expl.imm, rs1_abi);
        }
        if (info(op_id).side_effects & SideEffectFlags::Csr) {
            std::string csr_str = csr_name(static_cast<uint32_t>(expl.imm) & 0xFFFU);
            if (is_csr_imm_op(op_id)) {
                return std::format("{} {}, {}, {}", mnemonic, rd_abi, csr_str,
                                   std::to_underlying(expl.rs1.reg_id));
            }
            return std::format("{} {}, {}, {}", mnemonic, rd_abi, csr_str, rs1_abi);
        }
        if (op_id == ECALL || op_id == EBREAK || op_id == URET || op_id == SRET || op_id == MRET ||
            op_id == WFI) {
            return std::string(mnemonic);
        }
        if (op_id == FENCE) return "fence";
        if (op_id == FENCE_I) return "fence.i";
        if (is_shift_imm_op(op_id)) {
            uint32_t shamt = static_cast<uint32_t>(expl.imm) & 0x3FU;
            return std::format("{} {}, {}, {}", mnemonic, rd_abi, rs1_abi, shamt);
        }
        return std::format("{} {}, {}, {}", mnemonic, rd_abi, rs1_abi, expl.imm);
    }
    switch (fmt) {
        case InstFormat::S:
            return std::format("{} {}, {}({})", mnemonic, rs2_abi, expl.imm, rs1_abi);
        case InstFormat::B:
            return std::format("{} {}, {}, {}", mnemonic, rs1_abi, rs2_abi, expl.imm);
        case InstFormat::U:
            return std::format("{} {}, 0x{:X}", mnemonic, rd_abi, static_cast<uint32_t>(expl.imm));
        case InstFormat::J:
            return std::format("jal {}, {}", rd_abi, expl.imm);
        case InstFormat::R4:
            return std::format("{} {}, {}, {}, {}", mnemonic, rd_abi, rs1_abi, rs2_abi, rs3_abi);
        default:
            return "unknown / illegal instruction";
    }
}

auto build_microarch_profile(OperationId op_id) -> MicroarchProfile {
    using namespace simrv::pipeline::operation;
    MicroarchProfile prof{};
    const auto info_entry = info(op_id);
    prof.execution_class = info_entry.execution_class;

    switch (info_entry.execution_class) {
        case ExecutionClass::Multiply:
            prof.unit_class = "Integer Multiplier";
            prof.latency_note = "Pipelined multi-cycle ALU";
            break;
        case ExecutionClass::DivideOrRemainder:
            prof.unit_class = "Integer Hardware Divider";
            prof.latency_note = "Iterative non-pipelined";
            break;
        case ExecutionClass::FpAlu:
            prof.unit_class = "Floating-Point ALU / FMA";
            prof.latency_note = "Pipelined FP unit";
            break;
        case ExecutionClass::FpDivideOrSqrt:
            prof.unit_class = "FP Divide & Square Root Unit";
            prof.latency_note = "Multi-cycle iterative";
            break;
        case ExecutionClass::Cfu:
            prof.unit_class = "Custom Function Unit (CFU)";
            prof.latency_note = "Configurable accelerator pipeline";
            break;
        case ExecutionClass::Default:
        default:
            if (is_load(op_id)) {
                prof.unit_class = "Load / Store Unit (LSU)";
                prof.latency_note = "1 cyc hit / DCache fill";
            } else if (is_store(op_id)) {
                prof.unit_class = "Load / Store Unit (LSU)";
                prof.latency_note = "Posted write buffer";
            } else if (is_branch(op_id) || is_jump(op_id)) {
                prof.unit_class = "Branch & Jump Unit (BJU)";
                prof.latency_note = "Predicted at IF / resolved at EX";
            } else {
                prof.unit_class = "Single-Cycle Integer ALU";
                prof.latency_note = "1 cycle execution";
            }
            break;
    }

    if (is_serializing(op_id)) prof.hazard_tags.emplace_back("Pipeline Serializing");
    if (is_atomic(op_id)) prof.hazard_tags.emplace_back("Atomic Memory RMW");
    if (is_branch(op_id)) prof.hazard_tags.emplace_back("Conditional Branch");
    if (is_jump(op_id)) prof.hazard_tags.emplace_back("Unconditional Jump");
    if (has_side_effects(op_id)) prof.hazard_tags.emplace_back("System / CSR Side Effects");
    if (writes_rd(op_id)) prof.hazard_tags.emplace_back("RAW Hazard Producer");

    return prof;
}

auto read_csr_side_effect_free(const simrv::core::ArchState& st, uint32_t csr_addr)
    -> std::optional<simrv::xlen::CsrValue> {
    switch (csr_addr) {
        case 0x001:
            return st.fcsr & 0x1FU;
        case 0x002:
            return (st.fcsr >> 5U) & 0x7U;
        case 0x003:
            return st.fcsr & 0xFFU;
        case 0x008:
            return st.vstart;
        case 0x009:
            return st.vxsat;
        case 0x00A:
            return st.vxrm;
        case 0x00F:
            return (st.vxrm << 1U) | st.vxsat;
        case 0xC20:
            return st.vl;
        case 0xC21:
            return st.vtype;
        case 0xC22:
            return static_cast<simrv::xlen::CsrValue>(st.regs.vlen_bytes());
        case 0x100:
            return st.mstatus;
        case 0x104:
            return st.mie & st.mideleg;
        case 0x105:
            return st.stvec;
        case 0x106:
            return st.scounteren;
        case 0x10A:
            return st.senvcfg;
        case 0x140:
            return st.sscratch;
        case 0x141:
            return st.sepc;
        case 0x142:
            return static_cast<simrv::xlen::CsrValue>(st.scause);
        case 0x143:
            return st.stval;
        case 0x144:
            return st.mip & st.mideleg;
        case 0x14D:
            return static_cast<simrv::xlen::CsrValue>(st.stimecmp);
        case 0x180:
            return st.satp;
        case 0x300:
            return st.mstatus;
        case 0x301:
            return st.misa;
        case 0x302:
            return st.medeleg;
        case 0x303:
            return st.mideleg;
        case 0x304:
            return st.mie;
        case 0x305:
            return st.mtvec;
        case 0x306:
            return st.mcounteren;
        case 0x30A:
            return st.menvcfg;
        case 0x320:
            return st.mcountinhibit;
        case 0x340:
            return st.mscratch;
        case 0x341:
            return st.mepc;
        case 0x342:
            return static_cast<simrv::xlen::CsrValue>(st.mcause);
        case 0x343:
            return st.mtval;
        case 0x344:
            return st.mip;
        case 0x747:
            return st.mseccfg;
        case 0xF14:
            return st.mhartid;
        case 0xB02:
        case 0xC02:
            return static_cast<simrv::xlen::CsrValue>(st.minstret);
        default:
            if (csr_addr >= 0x3B0 && csr_addr <= 0x3BF) {
                return st.pmpaddr[csr_addr - 0x3B0];
            }
            return std::nullopt;
    }
}

auto translate_for_inspection(const simrv::core::ArchState& st, const simrv::core::CPU* cpu,
                              simrv::xlen::Address vaddr, bool is_fetch, bool is_write)
    -> std::optional<simrv::xlen::Address> {
    const auto active_xlen = st.current_xlen();
    if (st.priv == simrv::xlen::PrivilegeLevel::Machine ||
        !simrv::xlen::satp_translation_enabled(st.satp, active_xlen)) {
        return active_xlen == 32 ? (vaddr & 0xFFFFFFFFULL) : vaddr;
    }
    if (cpu == nullptr) return std::nullopt;
    const simrv::xlen::Word asid = simrv::xlen::satp_asid(st.satp, active_xlen);
    const simrv::core::TLBEntry* entry =
        is_fetch ? cpu->tlb.peek_inst_r(vaddr, asid, st.priv)
                 : (is_write ? cpu->tlb.peek_data_w(vaddr, asid, st.priv)
                             : cpu->tlb.peek_data_r(vaddr, asid, st.priv));
    if (entry == nullptr) return std::nullopt;
    return entry->p_addr | (vaddr & simrv::memory::kPageMask);
}

auto get_abi_reg_name(simrv::xlen::RegId reg, simrv::pipeline::operation::RegBank bank)
    -> std::string {
    using simrv::pipeline::operation::RegBank;
    uint32_t const idx = std::to_underlying(reg) & 0x1FU;
    switch (bank) {
        case RegBank::Float:
            return std::string(FP_ABI_NAMES.at(idx));
        case RegBank::Vector:
            return std::format("v{}", idx);
        case RegBank::Integer:
        default:
            return std::string(ABI_NAMES.at(idx));
    }
}

auto format_reg_value(simrv::xlen::Register val, unsigned xlen) -> std::string {
    if (xlen == 32) {
        return std::format("0x{:08x}", static_cast<uint32_t>(val));
    }
    return std::format("0x{:016x}", static_cast<uint64_t>(val));
}

auto build_operand_state(simrv::xlen::RegId reg, simrv::pipeline::operation::RegBank bank,
                         bool present, const simrv::core::ArchState* st, unsigned xlen)
    -> OperandState {
    using simrv::pipeline::operation::RegBank;
    OperandState op{};
    op.reg_id = reg;
    op.bank = bank;
    op.present = present;
    op.name = get_abi_reg_name(reg, bank);
    if (st != nullptr && present) {
        if (bank == RegBank::Integer) {
            uint32_t const idx = std::to_underlying(reg) & 0x1FU;
            op.raw_value = (idx == 0) ? 0 : st->regs.read(reg);
            op.formatted_value = format_reg_value(op.raw_value, xlen);
        } else if (bank == RegBank::Float) {
            op.raw_value = st->regs.read_fp(simrv::xlen::to_fp_reg_id(reg));
            op.formatted_value = format_reg_value(op.raw_value, xlen);
        } else if (bank == RegBank::Vector) {
            op.formatted_value = op.name;
        }
    }
    return op;
}

auto vlmul_name(Vlmul lmul) -> std::string {
    switch (lmul) {
        case Vlmul::LMUL_1:
            return "1";
        case Vlmul::LMUL_2:
            return "2";
        case Vlmul::LMUL_4:
            return "4";
        case Vlmul::LMUL_8:
            return "8";
        case Vlmul::LMUL_F8:
            return "1/8";
        case Vlmul::LMUL_F4:
            return "1/4";
        case Vlmul::LMUL_F2:
            return "1/2";
        case Vlmul::LMUL_RESERVED:
        default:
            return "reserved";
    }
}

auto is_w_alu_op(OperationId op_id) -> bool {
    return op_id == ADDW || op_id == ADDIW || op_id == SUBW || op_id == SLLW || op_id == SLLIW ||
           op_id == SRLW || op_id == SRLIW || op_id == SRAW || op_id == SRAIW || op_id == MULW ||
           op_id == DIVW || op_id == DIVUW || op_id == REMW || op_id == REMUW || op_id == ADD_UW ||
           op_id == SLLI_UW || op_id == SH1ADD_UW || op_id == SH2ADD_UW || op_id == SH3ADD_UW ||
           op_id == CLZW || op_id == CTZW || op_id == CPOPW || op_id == ROLW || op_id == RORW ||
           op_id == RORIW || op_id == PACKW;
}

auto is_pure_int_alu_op(OperationId op_id) -> bool {
    return (op_id >= ADDI && op_id <= SRA) || (op_id >= ADDIW && op_id <= SRAW) ||
           (op_id >= MUL && op_id <= REMUW) || (op_id >= SH1ADD && op_id <= PACKW) ||
           op_id == CZERO_EQZ || op_id == CZERO_NEZ;
}

auto build_explanation_impl(uint32_t raw_inst, simrv::xlen::Address pc, unsigned active_xlen,
                            const simrv::core::ArchState* st, const simrv::core::CPU* cpu,
                            const simrv::core::Machine* machine) -> InstructionExplanation {
    using namespace simrv::pipeline::operation;
    InstructionExplanation expl{};
    expl.pc = pc;
    if (machine != nullptr) {
        expl.symbol = machine->symbol_table().lookup(pc);
    }
    expl.raw_bits = raw_inst;
    expl.is_compressed = (raw_inst & 0x3U) != 0x3U;
    expl.active_xlen = active_xlen;
    expl.active_privilege = (st != nullptr) ? st->priv : simrv::xlen::PrivilegeLevel::Machine;
    expl.state_grounded = (st != nullptr);

    if (expl.is_compressed) {
        expl.canonical_bits =
            simrv::pipeline::decompressInstruction(raw_inst & 0xFFFFU, active_xlen == 64);
    } else {
        expl.canonical_bits = raw_inst;
    }

    const simrv::pipeline::Decoder decoded(expl.canonical_bits);
    expl.opcode = decoded.opcode();
    expl.op_id = simrv::pipeline::decoder(expl.canonical_bits);
    expl.format = simrv::isa::get_instruction_format(expl.opcode);
    expl.funct3 = decoded.funct3();
    expl.funct7 = decoded.funct7();

    switch (expl.format) {
        case InstFormat::I:
            expl.imm = decoded.imm_i();
            expl.has_imm = true;
            break;
        case InstFormat::S:
            expl.imm = decoded.imm_s();
            expl.has_imm = true;
            break;
        case InstFormat::B:
            expl.imm = decoded.imm_b();
            expl.has_imm = true;
            break;
        case InstFormat::U:
            expl.imm = decoded.imm_u();
            expl.has_imm = true;
            break;
        case InstFormat::J:
            expl.imm = decoded.imm_j();
            expl.has_imm = true;
            break;
        default:
            expl.imm = 0;
            expl.has_imm = false;
            break;
    }

    expl.spec = get_operation_spec(expl.op_id);
    expl.bitfields = build_bitfields(expl.format, expl.raw_bits);
    expl.microarch = build_microarch_profile(expl.op_id);

    const auto rdb = rd_bank(expl.op_id);
    const auto rs1b = rs1_bank(expl.op_id);
    const auto rs2b = rs2_bank(expl.op_id);
    const auto rs3b = rs3_bank(expl.op_id);

    const bool rd_present = (rdb != RegBank::None) || has_rd_field(expl.format);
    const bool rs1_present = (rs1b != RegBank::None) || has_rs1_field(expl.format);
    const bool rs2_present = (rs2b != RegBank::None) || has_rs2_field(expl.format);
    const bool rs3_present = (rs3b != RegBank::None) || (expl.format == InstFormat::R4);

    const auto effective_rdb = (rdb != RegBank::None) ? rdb : RegBank::Integer;
    const auto effective_rs1b = (rs1b != RegBank::None) ? rs1b : RegBank::Integer;
    const auto effective_rs2b = (rs2b != RegBank::None) ? rs2b : RegBank::Integer;
    const auto effective_rs3b = (rs3b != RegBank::None) ? rs3b : RegBank::Float;

    expl.rd = build_operand_state(decoded.rd(), effective_rdb, rd_present, st, active_xlen);
    expl.rs1 = build_operand_state(decoded.rs1(), effective_rs1b, rs1_present, st, active_xlen);
    expl.rs2 = build_operand_state(decoded.rs2(), effective_rs2b, rs2_present, st, active_xlen);
    expl.rs3 = build_operand_state(decoded.rs3(), effective_rs3b, rs3_present, st, active_xlen);

    // Override banks to true architectural banks from OperationInfo for semantic queries
    expl.rd.bank = rdb;
    expl.rs1.bank = rs1b;
    expl.rs2.bank = rs2b;
    expl.rs3.bank = rs3b;

    expl.assembly =
        format_canonical_assembly(expl, expl.rd.name, expl.rs1.name, expl.rs2.name, expl.rs3.name);

    const simrv::xlen::Address step_bytes = expl.is_compressed ? 2U : 4U;
    expl.predicted_effect.next_pc = pc + step_bytes;
    if (rdb != RegBank::None) {
        expl.predicted_effect.written_reg = expl.rd.reg_id;
        expl.predicted_effect.written_bank = rdb;
        expl.predicted_effect.written_reg_name = expl.rd.name;
    }

    if (is_cfu(expl.op_id)) {
        expl.cfu =
            get_cfu_details(static_cast<uint32_t>(expl.funct7), std::to_underlying(expl.funct3));
    }

    if (st == nullptr) {
        return expl;
    }

    // 1. Evaluate Memory Dataflow
    if (is_memory(expl.op_id)) {
        MemoryDataflow mem{};
        mem.base_address = expl.rs1.raw_value;
        mem.offset = is_atomic(expl.op_id) ? 0 : expl.imm;
        mem.effective_vaddr = mem.base_address + static_cast<simrv::xlen::Address>(mem.offset);
        if (active_xlen == 32) {
            mem.effective_vaddr &= 0xFFFFFFFFULL;
        }
        mem.kind = info(expl.op_id).memory;
        const uint32_t f3 = std::to_underlying(expl.funct3);
        if (expl.opcode == Opcode::OpV) {
            if (expl.op_id >= VLE8_V && expl.op_id <= VLE64_V) {
                mem.access_bytes = 1U << (expl.op_id - VLE8_V);
            } else if (expl.op_id >= VSE8_V && expl.op_id <= VSE64_V) {
                mem.access_bytes = 1U << (expl.op_id - VSE8_V);
            } else {
                mem.access_bytes = 1U << (f3 & 0x3U);
            }
        } else {
            mem.access_bytes = 1U << (f3 & 0x3U);
        }
        mem.is_aligned =
            (mem.access_bytes <= 1U) || ((mem.effective_vaddr & (mem.access_bytes - 1U)) == 0);
        mem.translation_enabled = (st->priv != simrv::xlen::PrivilegeLevel::Machine) &&
                                  simrv::xlen::satp_translation_enabled(st->satp, active_xlen);
        const bool is_write =
            (mem.kind == MemoryAccessKind::Store || mem.kind == MemoryAccessKind::Atomic);
        mem.effective_paddr =
            translate_for_inspection(*st, cpu, mem.effective_vaddr, false, is_write);
        mem.tlb_hit = mem.effective_paddr.has_value();

        if (mem.effective_paddr.has_value()) {
            const auto pmp_access =
                is_write ? simrv::core::PmpAccessType::Write : simrv::core::PmpAccessType::Read;
            mem.pmp_permitted = simrv::core::pmp::check_access(*st, *mem.effective_paddr,
                                                               mem.access_bytes, pmp_access);
            if (mem.pmp_permitted && machine != nullptr && mem.access_bytes <= 8U &&
                machine->ram_view().contains(*mem.effective_paddr, mem.access_bytes)) {
                uint64_t raw_mem = 0;
                std::memcpy(&raw_mem, machine->ram_view().unchecked_ptr(*mem.effective_paddr),
                            mem.access_bytes);
                mem.memory_preview_value = raw_mem;
            }
        }
        expl.memory = mem;
    }

    // 2. Evaluate Control Flow
    if (is_control(expl.op_id) || expl.op_id == SRET || expl.op_id == MRET || expl.op_id == URET) {
        ControlDataflow ctrl{};
        ctrl.fallthrough_pc = pc + step_bytes;
        ctrl.link_pc = pc + step_bytes;
        if (expl.op_id == JALR) {
            ctrl.target_pc = (expl.rs1.raw_value + static_cast<simrv::xlen::Address>(expl.imm)) &
                             ~simrv::xlen::Address{1};
            ctrl.is_conditional = false;
            ctrl.condition_met = true;
            ctrl.condition_relation = "unconditional indirect jump";
        } else if (expl.op_id == JAL) {
            ctrl.target_pc = pc + static_cast<simrv::xlen::Address>(expl.imm);
            ctrl.is_conditional = false;
            ctrl.condition_met = true;
            ctrl.condition_relation = "unconditional relative jump";
        } else if (is_branch(expl.op_id)) {
            ctrl.target_pc = pc + static_cast<simrv::xlen::Address>(expl.imm);
            ctrl.is_conditional = true;
            ctrl.condition_met = simrv::execute::ExecuteUnitInt::branchTaken(
                expl.rs1.raw_value, expl.rs2.raw_value, expl.funct3, active_xlen);
            std::string_view rel_op = "==";
            switch (expl.funct3) {
                case simrv::isa::Funct3::Beq:
                    rel_op = "==";
                    break;
                case simrv::isa::Funct3::Bne:
                    rel_op = "!=";
                    break;
                case simrv::isa::Funct3::Blt:
                    rel_op = "<s";
                    break;
                case simrv::isa::Funct3::Bge:
                    rel_op = ">=s";
                    break;
                case simrv::isa::Funct3::Bltu:
                    rel_op = "<u";
                    break;
                case simrv::isa::Funct3::Bgeu:
                    rel_op = ">=u";
                    break;
                default:
                    break;
            }
            ctrl.condition_relation =
                std::format("{} ({}) {} {} ({}) -> {}", expl.rs1.name, expl.rs1.formatted_value,
                            rel_op, expl.rs2.name, expl.rs2.formatted_value,
                            ctrl.condition_met ? "TAKEN" : "NOT TAKEN");
        } else if (expl.op_id == MRET) {
            ctrl.target_pc = st->mepc;
            ctrl.is_conditional = false;
            ctrl.condition_met = true;
            ctrl.condition_relation = "return from M-mode trap (mepc)";
        } else if (expl.op_id == SRET) {
            ctrl.target_pc = st->sepc;
            ctrl.is_conditional = false;
            ctrl.condition_met = true;
            ctrl.condition_relation = "return from S-mode trap (sepc)";
        }
        if (active_xlen == 32) {
            ctrl.target_pc &= 0xFFFFFFFFULL;
            ctrl.fallthrough_pc &= 0xFFFFFFFFULL;
        }
        expl.predicted_effect.next_pc = ctrl.condition_met ? ctrl.target_pc : ctrl.fallthrough_pc;
        expl.control = ctrl;
    }

    // 3. Evaluate CSR Dataflow
    if (info(expl.op_id).side_effects & SideEffectFlags::Csr) {
        CsrDataflow csr_df{};
        csr_df.address = static_cast<uint32_t>(expl.imm) & 0xFFFU;
        csr_df.active_privilege = st->priv;
        if (auto details = get_csr_details(csr_df.address)) {
            csr_df.metadata = *details;
        } else {
            csr_df.metadata.name = "unknown";
            csr_df.metadata.min_privilege =
                static_cast<simrv::xlen::PrivilegeLevel>((csr_df.address >> 8U) & 0x3U);
            csr_df.metadata.read_only = ((csr_df.address >> 10U) & 0x3U) == 0x3U;
        }
        csr_df.privilege_permitted =
            std::to_underlying(st->priv) >= std::to_underlying(csr_df.metadata.min_privilege);
        const uint32_t rs1_or_uimm = std::to_underlying(expl.rs1.reg_id);
        csr_df.attempts_write = (expl.op_id == CSRRW || expl.op_id == CSRRWI) || (rs1_or_uimm != 0);
        csr_df.write_permitted = !(csr_df.attempts_write && csr_df.metadata.read_only);
        csr_df.current_value = read_csr_side_effect_free(*st, csr_df.address);
        if (csr_df.current_value.has_value() && csr_df.privilege_permitted &&
            csr_df.write_permitted) {
            auto wval = simrv::execute::ExecuteUnitInt::csrWriteValue(
                *csr_df.current_value, expl.rs1.raw_value, rs1_or_uimm, expl.funct3);
            if (wval.has_value()) {
                csr_df.predicted_write_value = *wval;
            }
        }
        expl.csr = csr_df;
    }

    // 4. Evaluate Vector Dataflow
    if (expl.opcode == Opcode::OpV || simrv::isa::is_vector_op(expl.op_id)) {
        VectorDataflow vec{};
        const VtypeView vt{st->vtype, static_cast<uint8_t>(active_xlen)};
        vec.vill = vt.vill();
        vec.vsew_bits = vt.sew_bits();
        vec.vlmul_str = vlmul_name(vt.vlmul());
        vec.vl = static_cast<uint32_t>(st->vl);
        vec.vstart = static_cast<uint32_t>(st->vstart);
        vec.unmasked = ((expl.canonical_bits >> 25U) & 0x1U) != 0U;
        expl.vector = vec;
    }

    // 5. Evaluate Predicted Post-State Delta (Writeback & Trap)
    if (expl.op_id == ILLEGAL) {
        expl.predicted_effect.predicted_trap =
            simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::IllegalInstruction);
        expl.predicted_effect.predicted_tval = expl.raw_bits;
    } else if (expl.op_id == ECALL) {
        const auto code = (st->priv == simrv::xlen::PrivilegeLevel::User)
                              ? simrv::isa::ExceptionCode::UserEcall
                              : ((st->priv == simrv::xlen::PrivilegeLevel::Supervisor)
                                     ? simrv::isa::ExceptionCode::SupervisorEcall
                                     : simrv::isa::ExceptionCode::MachineEcall);
        expl.predicted_effect.predicted_trap = simrv::isa::to_trap_cause(code);
    } else if (expl.op_id == EBREAK) {
        expl.predicted_effect.predicted_trap =
            simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::Breakpoint);
        expl.predicted_effect.predicted_tval = pc;
    } else if (expl.csr.has_value() &&
               (!expl.csr->privilege_permitted || !expl.csr->write_permitted)) {
        expl.predicted_effect.predicted_trap =
            simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::IllegalInstruction);
        expl.predicted_effect.predicted_tval = expl.raw_bits;
    } else if (expl.memory.has_value() && !expl.memory->pmp_permitted) {
        const auto code = (expl.memory->kind == MemoryAccessKind::Load)
                              ? simrv::isa::ExceptionCode::FaultLoad
                              : simrv::isa::ExceptionCode::FaultStore;
        expl.predicted_effect.predicted_trap = simrv::isa::to_trap_cause(code);
        expl.predicted_effect.predicted_tval = expl.memory->effective_vaddr;
    }

    if (!expl.predicted_effect.predicted_trap.has_value() && rdb == RegBank::Integer) {
        if (std::to_underlying(expl.rd.reg_id) == 0) {
            expl.predicted_effect.predicted_reg_value = 0;
        } else if (expl.op_id == LUI) {
            expl.predicted_effect.predicted_reg_value =
                static_cast<simrv::xlen::Register>(expl.imm);
        } else if (expl.op_id == AUIPC) {
            expl.predicted_effect.predicted_reg_value =
                pc + static_cast<simrv::xlen::Register>(expl.imm);
        } else if (expl.op_id == JAL || expl.op_id == JALR) {
            expl.predicted_effect.predicted_reg_value = pc + step_bytes;
        } else if (expl.csr.has_value() && expl.csr->current_value.has_value()) {
            expl.predicted_effect.predicted_reg_value = *expl.csr->current_value;
        } else if (is_pure_int_alu_op(expl.op_id)) {
            const simrv::xlen::Register rhs = has_imm_field(expl.format)
                                                  ? static_cast<simrv::xlen::Register>(expl.imm)
                                                  : expl.rs2.raw_value;
            if (is_w_alu_op(expl.op_id)) {
                expl.predicted_effect.predicted_reg_value =
                    simrv::execute::ExecuteUnitInt::aluIntW(expl.rs1.raw_value, rhs, expl.op_id);
            } else {
                expl.predicted_effect.predicted_reg_value = simrv::execute::ExecuteUnitInt::aluInt(
                    expl.rs1.raw_value, rhs, expl.op_id, active_xlen);
            }
        } else if (is_load(expl.op_id) && expl.memory.has_value() &&
                   expl.memory->memory_preview_value.has_value()) {
            const uint64_t raw_load = *expl.memory->memory_preview_value;
            switch (expl.op_id) {
                case LB:
                    expl.predicted_effect.predicted_reg_value = static_cast<simrv::xlen::Register>(
                        static_cast<int64_t>(static_cast<int8_t>(raw_load)));
                    break;
                case LH:
                    expl.predicted_effect.predicted_reg_value = static_cast<simrv::xlen::Register>(
                        static_cast<int64_t>(static_cast<int16_t>(raw_load)));
                    break;
                case LW:
                    expl.predicted_effect.predicted_reg_value = static_cast<simrv::xlen::Register>(
                        static_cast<int64_t>(static_cast<int32_t>(raw_load)));
                    break;
                case LBU:
                    expl.predicted_effect.predicted_reg_value =
                        static_cast<simrv::xlen::Register>(raw_load & 0xFFULL);
                    break;
                case LHU:
                    expl.predicted_effect.predicted_reg_value =
                        static_cast<simrv::xlen::Register>(raw_load & 0xFFFFULL);
                    break;
                case LWU:
                    expl.predicted_effect.predicted_reg_value =
                        static_cast<simrv::xlen::Register>(raw_load & 0xFFFFFFFFULL);
                    break;
                case LD:
                    expl.predicted_effect.predicted_reg_value =
                        static_cast<simrv::xlen::Register>(raw_load);
                    break;
                default:
                    break;
            }
        }

        if (expl.predicted_effect.predicted_reg_value.has_value()) {
            if (active_xlen == 32) {
                *expl.predicted_effect.predicted_reg_value &= 0xFFFFFFFFULL;
            }
            expl.predicted_effect.effect_equation = std::format(
                "{} <= {}", expl.rd.name,
                format_reg_value(*expl.predicted_effect.predicted_reg_value, active_xlen));
        }
    }

    return expl;
}

}  // namespace

auto explain_static(uint32_t raw_inst, simrv::xlen::Address pc, unsigned xlen)
    -> InstructionExplanation {
    return build_explanation_impl(raw_inst, pc, xlen, nullptr, nullptr, nullptr);
}

auto explain_in_state(const simrv::core::ArchState& state, simrv::xlen::Address pc,
                      uint32_t raw_inst, const simrv::core::CPU* cpu,
                      const simrv::core::Machine* machine) -> InstructionExplanation {
    return build_explanation_impl(raw_inst, pc, state.current_xlen(), &state, cpu, machine);
}

auto explain_at_pc(const simrv::core::Machine& machine, size_t hart_index,
                   std::optional<simrv::xlen::Address> override_pc)
    -> std::optional<InstructionExplanation> {
    if (hart_index >= machine.num_harts()) {
        return std::nullopt;
    }
    const auto& cpu = machine.hart(hart_index);
    const auto& st = cpu.state();
    const simrv::xlen::Address target_pc = override_pc.value_or(st.pc);
    if (target_pc == 0) {
        return std::nullopt;
    }

    auto read_halfword = [&](simrv::xlen::Address vaddr) -> std::optional<uint16_t> {
        const auto paddr = translate_for_inspection(st, &cpu, vaddr, true, false);
        if (!paddr.has_value()) {
            return std::nullopt;
        }
        if (!simrv::core::pmp::check_access(st, *paddr, sizeof(uint16_t),
                                            simrv::core::PmpAccessType::Execute)) {
            return std::nullopt;
        }
        if (!machine.ram_view().contains(*paddr, sizeof(uint16_t))) {
            return std::nullopt;
        }
        uint16_t value = 0;
        std::memcpy(&value, machine.ram_view().unchecked_ptr(*paddr), sizeof(value));
        return value;
    };

    const auto first = read_halfword(target_pc);
    if (!first.has_value()) {
        return std::nullopt;
    }
    uint32_t raw_inst = *first;
    if ((*first & 0x3U) == 0x3U) {
        const auto second = read_halfword(target_pc + 2);
        if (!second.has_value()) {
            return std::nullopt;
        }
        raw_inst |= static_cast<uint32_t>(*second) << 16U;
    }

    return explain_in_state(st, target_pc, raw_inst, &cpu, &machine);
}

}  // namespace simrv::explain
