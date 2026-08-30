/**
 * @file CsrFile.hpp
 * @brief SimRV CSR register-file interface.
 */
#pragma once

#include <expected>

#include "simrv/Define.hpp"
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
    [[nodiscard]] auto read(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode>;
    [[nodiscard]] auto read(CsrNumber csr) const -> std::expected<CSRValue, ExceptionCode> {
        return read(csr.raw());
    }
    auto write(CSRAddress addr, CSRValue wdata) -> std::expected<void, ExceptionCode>;
    auto write(CsrNumber csr, CSRValue wdata) -> std::expected<void, ExceptionCode> {
        return write(csr.raw(), wdata);
    }

   private:
    CPU& cpu_;
};

}  // namespace simrv::core
