/**
 * @file Module.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include <array>
#include <string_view>

#include "Define.hpp"

namespace simrv::module {

Instruction decompressInstruction(Instruction ir);
[[gnu::always_inline]] inline Instruction decodeImmediate(Instruction ir) {
    switch (opcode_of(ir)) {
        case Opcode::OpImm:
        case Opcode::Load:
        case Opcode::LoadFp:
        case Opcode::Jalr:
            return static_cast<Instruction>(static_cast<SignedWord>(ir) >> 20);
        case Opcode::Store: {
            const Instruction value = ((ir >> 7) & 0x1Fu) | ((ir >> 20) & 0xFE0u);
            return static_cast<Instruction>(static_cast<SignedWord>(value << 20) >> 20);
        }
        case Opcode::StoreFp: {
            const Instruction value = ((ir >> 7) & 0x1Fu) | ((ir >> 20) & 0xFE0u);
            return static_cast<Instruction>(static_cast<SignedWord>(value << 20) >> 20);
        }
        case Opcode::Branch: {
            const Instruction value = ((ir >> 31) << 12) | (((ir >> 7) & 1u) << 11) |
                                      (((ir >> 25) & 0x3Fu) << 5) | (((ir >> 8) & 0xFu) << 1);
            return static_cast<Instruction>(static_cast<SignedWord>(value << 19) >> 19);
        }
        case Opcode::Lui:
        case Opcode::Auipc:
            return static_cast<Instruction>(static_cast<SignedWord>(ir) >> 12);
        case Opcode::Jal: {
            const Instruction value = ((ir >> 31) << 20) | (((ir >> 12) & 0xFFu) << 12) |
                                      (((ir >> 20) & 0x1u) << 11) | (((ir >> 21) & 0x3FFu) << 1);
            return static_cast<Instruction>(static_cast<SignedWord>(value << 11) >> 11);
        }
        case Opcode::System:
            return static_cast<Instruction>((ir >> 15) & 0x1fu);
        default:
            return 0;
    }
}
OperationId decoder(Instruction ir);

Register ALU_IM(Register in1, Register in2, Instruction funct3, Instruction funct7);
Instruction ALU_B(Register in1, Register in2, Instruction funct3);
Register ALU_A(Register in1, Register in2, Instruction funct5);
CSRValue ALU_C(CSRValue rcsr, Register rrs1, Instruction imm, Instruction funct3);

extern const std::array<std::string_view, kOperationIdCount> OPERATION_NAME;

}  // namespace simrv::module
