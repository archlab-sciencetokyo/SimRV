/**
 * @file DecodeUnit.cpp
 * @brief SimRV implementation unit.
 */
#include "DecodeUnit.hpp"

#include "Module.hpp"

Instruction DecodeUnit::decompress(Instruction raw_ir) const {
    return simrv::module::CB_inst_decomp(raw_ir);
}

Instruction DecodeUnit::immGen(Instruction ir) const { return simrv::module::CB_imm_gen(ir); }

OperationId DecodeUnit::decodeOp(Instruction ir) const { return simrv::module::decoder(ir); }

bool DecodeUnit::isCompressed(Instruction raw_ir) const { return (raw_ir & 3u) != 3u; }
