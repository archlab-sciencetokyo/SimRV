/**
 * @file InstructionExplainer.hpp
 * @brief Visual/educational instruction decoder and explainer.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "simrv/core/CsrTypes.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::util {

/**
 * @brief Structured educational metadata for a Control and Status Register (CSR).
 */
struct CsrDetails {
    std::string_view name;
    std::string_view description;
    simrv::xlen::PrivilegeLevel min_privilege = simrv::xlen::PrivilegeLevel::User;
    bool read_only = false;
    std::string_view extension = "Base";
};

/**
 * @brief Print a detailed educational breakdown of a 32-bit (or 16-bit compressed) instruction.
 * @param raw_inst The raw bits of the instruction to explain.
 */
void explain_instruction(uint32_t raw_inst);

/**
 * @brief Get the educational mnemonic and description/behavior of an instruction by its
 * OperationId.
 */
auto get_operation_details(isa::OperationId op_id) -> std::pair<std::string_view, std::string_view>;

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
 * @brief Retrieve educational details for a CSR.
 */
[[nodiscard]] auto get_csr_details(uint32_t csr_addr) -> std::optional<CsrDetails>;

[[nodiscard]] inline auto get_csr_details(simrv::xlen::CsrNumber csr) -> std::optional<CsrDetails> {
    return get_csr_details(csr.value());
}

[[nodiscard]] inline auto get_csr_details(simrv::core::Csr csr) -> std::optional<CsrDetails> {
    return get_csr_details(static_cast<uint32_t>(csr));
}

/**
 * @brief Structured educational metadata for a Custom Function Unit (CFU) instruction.
 */
struct CfuDetails {
    uint32_t funct3 = 0;
    uint32_t funct7 = 0;
    std::string_view name = "CFU";
    std::string description;
};

/**
 * @brief Retrieve educational details for a CFU operation.
 */
[[nodiscard]] auto get_cfu_details(uint32_t funct7, uint32_t funct3) -> CfuDetails;

/**
 * @brief Get the ISA extension name (e.g., RV32I, RV32M, Privileged) that an instruction belongs
 * to.
 */
auto get_isa_extension_name(isa::OperationId op_id) -> std::string_view;

}  // namespace simrv::util
