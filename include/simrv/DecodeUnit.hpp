/**
 * @file DecodeUnit.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include "Define.hpp"

class DecodeUnit {
   public:
    Instruction decompress(Instruction raw_ir) const;
    Instruction immGen(Instruction ir) const;
    OperationId decodeOp(Instruction ir) const;
    bool isCompressed(Instruction raw_ir) const;
};
