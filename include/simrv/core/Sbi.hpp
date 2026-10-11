/**
 * @file Sbi.hpp
 * @brief Supervisor Binary Interface (SBI) exception handler.
 */
#pragma once

#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class CPU;
}  // namespace simrv::core

namespace simrv::sbi {

namespace detail {

/** Direct SBI handles only supervisor ECALLs; M-mode ECALL remains an architectural trap. */
constexpr auto is_direct_sbi_ecall(simrv::xlen::TrapCause cause) -> bool {
    return cause == ExceptionCode::SupervisorEcall;
}
constexpr auto is_direct_sbi_ecall(ExceptionCode code) -> bool {
    return code == ExceptionCode::SupervisorEcall;
}

/** Determine whether a target hart is selected by an SBI (hart_mask, hart_mask_base) pair. */
constexpr auto is_hart_selected(simrv::xlen::Word hart_mask, simrv::xlen::Word hart_mask_base,
                                size_t hart_id) -> bool {
    if (hart_mask_base == static_cast<simrv::xlen::Word>(-1)) {
        return true;
    }
    if (hart_id < static_cast<size_t>(hart_mask_base) ||
        (hart_id - static_cast<size_t>(hart_mask_base)) >= simrv::xlen::kXLenBits) {
        return false;
    }
    const auto shift = hart_id - static_cast<size_t>(hart_mask_base);
    return ((hart_mask >> shift) & simrv::xlen::Word{1}) != 0;
}

}  // namespace detail

/**
 * @brief Direct single-hart SBI execution environment used when no M-mode firmware is loaded.
 *
 * Only extensions whose semantics can be provided by the simulator are advertised. When OpenSBI
 * is present, ECALLs are delivered architecturally to that firmware instead of being handled here.
 */
class Sbi {
   public:
    explicit Sbi(core::CPU& cpu);

    auto handle_ecall(simrv::xlen::TrapCause cause) -> bool;
    auto handle_ecall(ExceptionCode code) -> bool { return handle_ecall(to_trap_cause(code)); }

   private:
    // Modular sub-handlers for extensions
    auto handle_base(simrv::xlen::Word func_id) -> bool;
    auto handle_time(simrv::xlen::Word func_id) -> bool;
    auto handle_rfence(simrv::xlen::Word func_id) -> bool;
    auto handle_ipi(simrv::xlen::Word func_id) -> bool;
    auto handle_hsm(simrv::xlen::Word func_id) -> bool;
    auto handle_system_reset(simrv::xlen::Word func_id) -> bool;
    auto handle_dbcn(simrv::xlen::Word func_id) -> bool;
    auto handle_pmu(simrv::xlen::Word func_id) -> bool;
    auto handle_cppc(simrv::xlen::Word func_id) -> bool;
    auto handle_susp(simrv::xlen::Word func_id) -> bool;

    [[nodiscard]] auto timer_value() const -> simrv::xlen::Counter;
    void sbi_return(simrv::xlen::SignedWord error, simrv::xlen::Word value);

   private:
    core::CPU& cpu_;
};

}  // namespace simrv::sbi
