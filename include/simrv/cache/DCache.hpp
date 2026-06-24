/**
 * @file DCache.hpp
 * @brief Level-1 Data Cache interface.
 */
#pragma once

#include "simrv/cache/BaseCache.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::cache {

class DCache : public BaseCache<64, 32, 4> {
   public:
    DCache() = default;

    [[nodiscard]] auto read(Address addr, Word& data, Instruction funct3) -> bool;
    void write(Address addr, Word data, Instruction funct3);
};

}  // namespace simrv::cache