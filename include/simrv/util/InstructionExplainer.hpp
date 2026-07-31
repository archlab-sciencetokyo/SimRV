/**
 * @file InstructionExplainer.hpp
 * @brief Visual/educational instruction decoder and explainer.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "simrv/Define.hpp"

namespace simrv::util {

struct Bitfields {
    uint32_t opcode{0};
    uint32_t rd{0};
    uint32_t funct3{0};
    uint32_t rs1{0};
    uint32_t rs2{0};
    uint32_t funct7{0};
    int32_t imm_i{0};
    int32_t imm_s{0};
    int32_t imm_b{0};
    uint32_t imm_u{0};
    int32_t imm_j{0};
};

/**
 * @brief Decode bitfields from a 32-bit RISC-V instruction.
 */
auto extract_bitfields(uint32_t raw_inst) -> Bitfields;

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
std::string csr_name(uint32_t csr_addr);

/**
 * @brief Get the ISA extension name (e.g., RV32I, RV32M, Privileged) that an instruction belongs
 * to.
 */
auto get_isa_extension_name(isa::OperationId op_id) -> std::string_view;

}  // namespace simrv::util
