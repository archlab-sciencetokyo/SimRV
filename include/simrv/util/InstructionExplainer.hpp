/**
 * @file InstructionExplainer.hpp
 * @brief Visual/educational instruction decoder and explainer.
 */
#pragma once

#include <cstdint>
#include <utility>
#include <string>
#include <string_view>
#include "simrv/Define.hpp"

namespace simrv::util {

/**
 * @brief Print a detailed educational breakdown of a 32-bit (or 16-bit compressed) instruction.
 * @param raw_inst The raw bits of the instruction to explain.
 */
void explain_instruction(uint32_t raw_inst);

/**
 * @brief Get the educational mnemonic and description/behavior of an instruction by its OperationId.
 */
auto get_operation_details(isa::OperationId op_id) -> std::pair<std::string_view, std::string_view>;

/**
 * @brief Get the symbolic name of a CSR from its address.
 */
std::string csr_name(uint32_t csr_addr);

/**
 * @brief Get the ISA extension name (e.g., RV32I, RV32M, Privileged) that an instruction belongs to.
 */
auto get_isa_extension_name(isa::OperationId op_id) -> std::string_view;

} // namespace simrv::util
