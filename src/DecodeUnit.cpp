/**
 * @file DecodeUnit.cpp
 * @brief SimRV implementation unit.
 */
#include "DecodeUnit.hpp"

uint32_t DecodeUnit::decompress(uint32_t raw_ir) const { return CB_inst_decomp(raw_ir); }

uint32_t DecodeUnit::immGen(uint32_t ir) const { return CB_imm_gen(ir); }

OPERATION_ID DecodeUnit::decodeOp(uint32_t ir) const { return decoder(ir); }

bool DecodeUnit::isCompressed(uint32_t raw_ir) const { return (raw_ir & 3u) != 3u; }
