/**
 * @file CsrFile.hpp
 * @brief SimRV CSR register-file interface.
 */
#pragma once

#include <expected>

#include "simrv/isa/TrapTypes.hpp"
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

    [[nodiscard]] auto getMstatus(simrv::xlen::CSRValue mask) const -> simrv::xlen::CSRValue;
    void setMstatus(simrv::xlen::CSRValue wdata);
    [[nodiscard]] auto read(simrv::xlen::CSRAddress addr) const
        -> std::expected<simrv::xlen::CSRValue, ExceptionCode>;
    [[nodiscard]] auto read(simrv::xlen::CsrNumber csr) const
        -> std::expected<simrv::xlen::CSRValue, ExceptionCode> {
        return read(csr.raw());
    }
    auto write(simrv::xlen::CSRAddress addr, simrv::xlen::CSRValue wdata)
        -> std::expected<void, ExceptionCode>;
    auto write(simrv::xlen::CsrNumber csr, simrv::xlen::CSRValue wdata)
        -> std::expected<void, ExceptionCode> {
        return write(csr.raw(), wdata);
    }

   private:
    CPU& cpu_;
};

}  // namespace simrv::core
