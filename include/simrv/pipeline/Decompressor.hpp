#pragma once

#include <cstdint>

#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::pipeline {

/// Expand 16-bit compressed (C / Zca / Zcb / Zcd / Zcmop) instructions to canonical 32-bit forms.
auto decompressInstruction(simrv::xlen::Instruction ir, bool is_rv64) -> simrv::xlen::Instruction;

/// Decode 16-bit Zcmp/Zcmt instructions that do not expand 1:1 into 32-bit instructions.
auto decode_compressed_op(uint16_t ir16) noexcept -> isa::OperationId;

}  // namespace simrv::pipeline
