/**
 * @file InstructionExplainer.hpp
 * @brief Formal architectural instruction decoder, state-grounded semantic evaluator, and
 *        specification reference provider.
 *
 * Normative descriptions and formulas cite the official ratified RISC-V International manuals:
 * - The RISC-V Instruction Set Manual, Volume I: Unprivileged Architecture (Version 20260120)
 * - The RISC-V Instruction Set Manual, Volume II: Privileged Architecture (Version 20260120)
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "simrv/core/CsrTypes.hpp"
#include "simrv/explain/ExplainTypes.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
struct ArchState;
class CPU;
class Machine;
}  // namespace simrv::core

namespace simrv::explain {

/**
 * @brief Print a detailed educational breakdown of a 32-bit (or 16-bit compressed) instruction.
 * @param raw_inst The raw bits of the instruction to explain.
 */
void explain_instruction(uint32_t raw_inst);

/**
 * @brief Retrieve the full normative RISC-V specification details (mnemonic, description,
 *        section citation, extension, and operational formula) for an OperationId.
 */
[[nodiscard]] auto get_operation_spec(isa::OperationId op_id) -> OperationSpecDetails;

/**
 * @brief Get the mnemonic and normative behavioral description of an instruction by its
 *        OperationId.
 */
[[nodiscard]] auto get_operation_details(isa::OperationId op_id)
    -> std::pair<std::string_view, std::string_view>;

/**
 * @brief Get the symbolic name of a CSR from its address.
 */
[[nodiscard]] auto csr_name(uint32_t csr_addr) -> std::string;

/**
 * @brief Get the symbolic name of a CSR from its strongly typed number.
 */
[[nodiscard]] inline auto csr_name(simrv::xlen::CsrNumber csr) -> std::string {
    return csr_name(csr.value());
}

/**
 * @brief Get the symbolic name of a CSR from its strongly typed enum.
 */
[[nodiscard]] inline auto csr_name(simrv::core::Csr csr) -> std::string {
    return csr_name(static_cast<uint32_t>(csr));
}

/**
 * @brief Retrieve structured normative details for a CSR.
 */
[[nodiscard]] auto get_csr_details(uint32_t csr_addr) -> std::optional<CsrDetails>;

[[nodiscard]] inline auto get_csr_details(simrv::xlen::CsrNumber csr) -> std::optional<CsrDetails> {
    return get_csr_details(csr.value());
}

[[nodiscard]] inline auto get_csr_details(simrv::core::Csr csr) -> std::optional<CsrDetails> {
    return get_csr_details(static_cast<uint32_t>(csr));
}

/**
 * @brief Retrieve educational details for a Custom Function Unit (CFU) operation.
 */
[[nodiscard]] auto get_cfu_details(uint32_t funct7, uint32_t funct3) -> CfuDetails;

/**
 * @brief Get the ISA extension name (e.g., RV32I, RV64I, RV32M, Zicsr, Privileged) that an
 *        instruction belongs to.
 */
[[nodiscard]] auto get_isa_extension_name(isa::OperationId op_id) -> std::string_view;

/**
 * @brief Build a formal InstructionExplanation from a raw instruction word without live state.
 */
[[nodiscard]] auto explain_static(uint32_t raw_inst, simrv::xlen::Address pc = 0,
                                  unsigned xlen = simrv::xlen::kXLenBits) -> InstructionExplanation;

/**
 * @brief Evaluate an instruction word formally against a live ArchState, optional CPU, and
 *        optional Machine (100% side-effect-free).
 */
[[nodiscard]] auto explain_in_state(const simrv::core::ArchState& state, simrv::xlen::Address pc,
                                    uint32_t raw_inst, const simrv::core::CPU* cpu = nullptr,
                                    const simrv::core::Machine* machine = nullptr)
    -> InstructionExplanation;

/**
 * @brief Fetch and formally explain the instruction at a target PC on the specified hart without
 *        mutating caches, TLB replacement state, PC, pipeline stages, or counters.
 */
[[nodiscard]] auto explain_at_pc(const simrv::core::Machine& machine, size_t hart_index = 0,
                                 std::optional<simrv::xlen::Address> override_pc = std::nullopt)
    -> std::optional<InstructionExplanation>;

}  // namespace simrv::explain
