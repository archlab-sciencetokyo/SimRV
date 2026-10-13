/**
 * @file CsrFile.hpp
 * @brief SimRV CSR register-file interface.
 */
#pragma once

#include <expected>

#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::core {

class CPU;

/**
 * @class CsrFile
 * @brief Encapsulates CSR read/write semantics and side effects.
 */
class CsrFile {
   public:
    explicit CsrFile(CPU& cpu) : cpu_(cpu) {}

    [[nodiscard]] auto getMstatus(simrv::xlen::CsrValue mask) const -> simrv::xlen::CsrValue;
    void setMstatus(simrv::xlen::CsrValue wdata);
    [[nodiscard]] auto read(simrv::xlen::CsrNumber csr) const
        -> std::expected<simrv::xlen::CsrValue, ExceptionCode>;
    auto write(simrv::xlen::CsrNumber csr, simrv::xlen::CsrValue wdata)
        -> std::expected<void, ExceptionCode>;

   private:
    CPU& cpu_;
};

}  // namespace simrv::core
