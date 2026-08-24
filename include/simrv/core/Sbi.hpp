/**
 * @file Sbi.hpp
 * @brief Supervisor Binary Interface (SBI) exception handler.
 */
#pragma once

#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class CPU;
}  // namespace simrv::core

namespace simrv::sbi {

namespace detail {

/** Direct SBI handles only supervisor ECALLs; M-mode ECALL remains an architectural trap. */
constexpr auto is_direct_sbi_ecall(TrapCause cause) -> bool {
    return cause == enum_mask(ExceptionCode::SupervisorEcall);
}

/** Determine whether a target hart is selected by an SBI (hart_mask, hart_mask_base) pair. */
constexpr auto is_hart_selected(Word hart_mask, Word hart_mask_base, size_t hart_id) -> bool {
    if (hart_mask_base == static_cast<Word>(-1)) {
        return true;
    }
    if (hart_id < static_cast<size_t>(hart_mask_base) ||
        (hart_id - static_cast<size_t>(hart_mask_base)) >= simrv::xlen::kXLenBits) {
        return false;
    }
    const auto shift = hart_id - static_cast<size_t>(hart_mask_base);
    return ((hart_mask >> shift) & Word{1}) != 0;
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

    auto handle_ecall(TrapCause cause) -> bool;

   private:
    // Modular sub-handlers for extensions
    auto handle_base(Word func_id) -> bool;
    auto handle_time(Word func_id) -> bool;
    auto handle_rfence(Word func_id) -> bool;
    auto handle_ipi(Word func_id) -> bool;
    auto handle_hsm(Word func_id) -> bool;
    auto handle_system_reset(Word func_id) -> bool;
    auto handle_dbcn(Word func_id) -> bool;
    auto handle_pmu(Word func_id) -> bool;
    auto handle_cppc(Word func_id) -> bool;
    auto handle_susp(Word func_id) -> bool;

    [[nodiscard]] auto timer_value() const -> Counter;
    void sbi_return(SignedWord error, Word value);

   private:
    core::CPU& cpu_;
};

}  // namespace simrv::sbi
