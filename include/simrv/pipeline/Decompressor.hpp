#pragma once

#include "simrv/xlen/Types.hpp"

namespace simrv::pipeline {

/// Expand 16-bit compressed (C / Zca / Zcb / Zcd / Zcmop) instructions to canonical 32-bit forms.
auto decompressInstruction(simrv::xlen::Instruction ir, bool is_rv64) -> simrv::xlen::Instruction;

}  // namespace simrv::pipeline
