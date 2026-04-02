/**
 * @file Module.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include <array>
#include <string_view>

#include "Define.hpp"

namespace simrv::module {

Instruction CB_inst_decomp(Instruction ir);  // Combinatorial Logical Circuit
Instruction CB_imm_gen(Instruction ir);      // Combinatorial Logical Circuit
OperationId decoder(Instruction ir);

Register ALU_IM(Register in1, Register in2, Instruction funct3, Instruction funct7);
Instruction ALU_B(Register in1, Register in2, Instruction funct3);
Register ALU_A(Register in1, Register in2, Instruction funct5);
CSRValue ALU_C(CSRValue rcsr, Register rrs1, Instruction imm, Instruction funct3);

extern const std::array<std::string_view, kOperationIdCount> OPERATION_NAME;

}  // namespace simrv::module
