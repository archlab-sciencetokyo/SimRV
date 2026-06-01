/**
 * @file CsrFile.hpp
 * @brief SimRV CSR register-file interface.
 */
#pragma once

#include "simrv/xlen/Types.hpp"

namespace simrv::core {

class CPU;

/**
 * @class CsrFile
 * @brief Encapsulates CSR read/write semantics and side effects.
 */
class CsrFile {
   public:
    explicit CsrFile(CPU& cpu) : cpu_(cpu) {}

    [[nodiscard]] auto getMstatus(CSRValue mask) const -> CSRValue;
    void setMstatus(CSRValue wdata);
    [[nodiscard]] auto read(CSRAddress addr) const -> CSRValue;
    void write(CSRAddress addr, CSRValue wdata);

   private:
    CPU& cpu_;
};

}  // namespace simrv::core