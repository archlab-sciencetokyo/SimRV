/**
 * @file CsrFile.hpp
 * @brief SimRV CSR register-file interface.
 */
#pragma once

#include <expected>

#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::core {

struct ArchState;
class CPU;

/**
 * @class CsrFile
 * @brief Encapsulates CSR read/write semantics and side effects.
 */
class CsrFile {
   public:
    explicit CsrFile(CPU& cpu) : cpu_(cpu) {}

    [[nodiscard]] static auto compute_mstatus(const ArchState& state,
                                              simrv::xlen::CsrValue mask) noexcept
        -> simrv::xlen::CsrValue;
    [[nodiscard]] static auto read_from_state(const ArchState& state, simrv::xlen::CsrNumber csr,
                                              const CPU* cpu = nullptr)
        -> std::expected<simrv::xlen::CsrValue, ExceptionCode>;

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
