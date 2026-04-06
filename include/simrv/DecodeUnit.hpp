/**
 * @file DecodeUnit.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include "Define.hpp"
#include "Module.hpp"

class DecodeUnit {
   public:
    [[nodiscard]] constexpr Instruction decompressInstruction(Instruction raw_ir) const noexcept {
        return simrv::module::decompressInstruction(raw_ir);
    }

    [[nodiscard]] constexpr Instruction decodeImmediate(Instruction ir) const noexcept {
        return simrv::module::decodeImmediate(ir);
    }

    [[nodiscard]] constexpr OperationId decodeOperation(Instruction ir) const noexcept {
        return simrv::module::decoder(ir);
    }

    [[nodiscard]] static constexpr bool isCompressedInstruction(Instruction raw_ir) noexcept {
        return (raw_ir & 3u) != 3u;
    }
};
