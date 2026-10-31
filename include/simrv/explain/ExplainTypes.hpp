/**
 * @file ExplainTypes.hpp
 * @brief Formal architectural state and instruction explanation domain types.
 *
 * Normative instruction and CSR behavioral descriptions, encoding layouts, and operational
 * formulas are grounded in the official ratified RISC-V International specifications:
 * - The RISC-V Instruction Set Manual, Volume I: Unprivileged Architecture
 *   (Document Version 20260120, January 2026)
 * - The RISC-V Instruction Set Manual, Volume II: Privileged Architecture
 *   (Document Version 20260120, January 2026)
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/CsrTypes.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::explain {

/// Official RISC-V Unprivileged Architecture Manual citation string.
inline constexpr std::string_view kUnprivSpecDoc =
    "The RISC-V Instruction Set Manual, Vol. I: Unprivileged Architecture (V20260120)";

/// Official RISC-V Privileged Architecture Manual citation string.
inline constexpr std::string_view kPrivSpecDoc =
    "The RISC-V Instruction Set Manual, Vol. II: Privileged Architecture (V20260120)";

/**
 * @brief Provenance metadata referencing the official RISC-V ISA Specification.
 */
struct SpecReference {
    std::string_view document = kUnprivSpecDoc;
    std::string_view section = "Vol. I: Ch. 1 (RV32I Base Integer Instruction Set)";
    std::string_view extension = "RV32I";
    std::string_view formula;
};

/**
 * @brief Normative specification and operational semantics for an ISA operation.
 */
struct OperationSpecDetails {
    std::string_view mnemonic = "UNKNOWN";
    std::string_view description;
    SpecReference reference{};
};

/**
 * @brief Structured architectural and specification metadata for a CSR.
 */
struct CsrDetails {
    std::string_view name;
    std::string_view description;
    simrv::xlen::PrivilegeLevel min_privilege = simrv::xlen::PrivilegeLevel::User;
    bool read_only = false;
    std::string_view extension = "Privileged";
    std::string_view spec_section = "Vol. II: Control and Status Registers";
};

/**
 * @brief Structured educational metadata for a Custom Function Unit (CFU) operation.
 */
struct CfuDetails {
    uint32_t funct3 = 0;
    uint32_t funct7 = 0;
    std::string_view name = "CFU";
    std::string description;
    std::string_view spec_section = "Vol. I: Extending RISC-V, custom-0 opcode space";
};

/**
 * @brief One decoded bitfield of an instruction encoding.
 */
struct BitFieldEntry {
    std::string label;
    unsigned high_bit = 0;
    unsigned low_bit = 0;
    uint32_t value = 0;
    unsigned bit_count = 0;
};

/**
 * @brief Architectural register operand bound to its value in ArchState.
 */
struct OperandState {
    simrv::xlen::RegId reg_id{0};
    simrv::pipeline::operation::RegBank bank{simrv::pipeline::operation::RegBank::None};
    std::string name;
    simrv::xlen::Register raw_value{0};
    std::string formatted_value;
    bool present = false;
};

/**
 * @brief Memory access evaluation grounded in current architectural state, TLB, and PMP.
 */
struct MemoryDataflow {
    simrv::xlen::Address base_address{0};
    simrv::xlen::ImmValue offset{0};
    simrv::xlen::Address effective_vaddr{0};
    std::optional<simrv::xlen::Address> effective_paddr;
    unsigned access_bytes = 0;
    simrv::pipeline::operation::MemoryAccessKind kind{
        simrv::pipeline::operation::MemoryAccessKind::None};
    bool is_aligned = true;
    bool pmp_permitted = true;
    bool translation_enabled = false;
    bool tlb_hit = false;
    std::optional<uint64_t> memory_preview_value;
};

/**
 * @brief Control-flow resolution grounded in current register state and PC.
 */
struct ControlDataflow {
    simrv::xlen::Address target_pc{0};
    simrv::xlen::Address fallthrough_pc{0};
    simrv::xlen::Address link_pc{0};
    bool is_conditional = false;
    bool condition_met = true;
    std::string condition_relation;
};

/**
 * @brief CSR read/write evaluation grounded in current privilege mode and CSR state.
 */
struct CsrDataflow {
    uint32_t address = 0;
    CsrDetails metadata{};
    simrv::xlen::PrivilegeLevel active_privilege = simrv::xlen::PrivilegeLevel::Machine;
    bool active_virtualization = false;
    bool privilege_permitted = true;
    bool attempts_write = false;
    bool write_permitted = true;
    std::optional<simrv::isa::ExceptionCode> access_exception;
    std::optional<simrv::xlen::CsrValue> current_value;
    std::optional<simrv::xlen::CsrValue> predicted_write_value;
};

/**
 * @brief Vector execution parameters grounded in current vtype, vl, and vstart CSRs.
 */
struct VectorDataflow {
    uint32_t vsew_bits = 0;
    std::string vlmul_str;
    uint32_t vl = 0;
    uint32_t vstart = 0;
    bool vill = false;
    bool unmasked = true;
};

/**
 * @brief Predicted architectural post-state delta (next PC, destination writeback, or trap).
 */
struct PredictedStateEffect {
    simrv::xlen::Address next_pc{0};
    std::optional<simrv::xlen::PrivilegeLevel> next_privilege;
    std::optional<bool> next_virtualization;
    std::optional<simrv::xlen::RegId> written_reg;
    simrv::pipeline::operation::RegBank written_bank{simrv::pipeline::operation::RegBank::None};
    std::string written_reg_name;
    std::optional<simrv::xlen::Register> predicted_reg_value;
    std::optional<simrv::xlen::TrapCause> predicted_trap;
    simrv::xlen::Register predicted_tval{0};
    std::string effect_equation;
};

/**
 * @brief Microarchitectural execution class, functional unit, and hazard profile.
 */
struct MicroarchProfile {
    simrv::pipeline::operation::ExecutionClass execution_class{
        simrv::pipeline::operation::ExecutionClass::Default};
    std::string unit_class;
    std::string latency_note;
    std::vector<std::string> hazard_tags;
};

/**
 * @brief Formalized semantic breakdown of an instruction evaluated against architectural state.
 */
struct InstructionExplanation {
    simrv::xlen::Address pc{0};
    std::string symbol;
    uint32_t raw_bits = 0;
    bool is_compressed = false;
    uint32_t canonical_bits = 0;
    unsigned active_xlen = simrv::xlen::kXLenBits;
    simrv::xlen::PrivilegeLevel active_privilege = simrv::xlen::PrivilegeLevel::Machine;
    bool active_virtualization = false;
    bool state_grounded = false;

    simrv::isa::InstFormat format{simrv::isa::InstFormat::Unknown};
    simrv::isa::Opcode opcode{};
    simrv::isa::OperationId op_id{simrv::isa::OperationId::ILLEGAL};
    simrv::isa::Funct3 funct3{};
    simrv::xlen::Funct7 funct7 = 0;

    OperationSpecDetails spec{};
    std::string assembly;

    std::vector<BitFieldEntry> bitfields;

    OperandState rd{};
    OperandState rs1{};
    OperandState rs2{};
    OperandState rs3{};
    simrv::xlen::ImmValue imm = 0;
    bool has_imm = false;

    std::optional<MemoryDataflow> memory;
    std::optional<ControlDataflow> control;
    std::optional<CsrDataflow> csr;
    std::optional<VectorDataflow> vector;
    std::optional<CfuDetails> cfu;

    PredictedStateEffect predicted_effect{};
    MicroarchProfile microarch{};
};

}  // namespace simrv::explain
