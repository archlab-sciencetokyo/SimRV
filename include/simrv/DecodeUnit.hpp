/**
 * @file DecodeUnit.hpp
 * @brief Instruction decode facade for pipeline stages.
 */
#pragma once

#include "Define.hpp"
#include "Module.hpp"

/**
 * @class DecodeUnit
 * @brief Thin wrapper over module-level decode/decompress helpers.
 */
class DecodeUnit {
   public:
    /**
     * @brief Decompress a potentially compressed instruction.
     * @param raw_ir Raw instruction bits fetched from memory.
     * @return Expanded 32-bit instruction form.
     */
    [[nodiscard]] constexpr auto decompressInstruction(Instruction raw_ir) const noexcept
        -> Instruction {
        return simrv::module::decompressInstruction(raw_ir);
    }

    /**
     * @brief Decode immediate field according to opcode format.
     * @param ir Canonical 32-bit instruction.
     * @return Sign-extended immediate value.
     */
    [[nodiscard]] constexpr auto decodeImmediate(Instruction ir) const noexcept -> Instruction {
        return simrv::module::decodeImmediate(ir);
    }

    /**
     * @brief Decode instruction into operation ID.
     * @param ir Canonical 32-bit instruction.
     * @return Operation identifier for pipeline execution.
     */
    [[nodiscard]] constexpr auto decodeOperation(Instruction ir) const noexcept -> OperationId {
        return simrv::module::decoder(ir);
    }

    [[nodiscard]] static constexpr auto isCompressedInstruction(Instruction raw_ir) noexcept
        -> bool {
        return (raw_ir & 3u) != 3u;
    }
};
