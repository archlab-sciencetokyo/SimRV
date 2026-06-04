/**
 * @file Sbi.hpp
 * @brief Supervisor Binary Interface (SBI) exception handler.
 */
#pragma once

#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class CPU;
}  // namespace simrv::core

namespace simrv::sbi {

class Sbi {
   public:
    explicit Sbi(core::CPU& cpu);

    auto handle_ecall(TrapCause cause) -> bool;

    private:
    // Modular sub-handlers for extensions
    auto handle_base(Word func_id) -> bool;
    auto handle_time(Word func_id) -> bool;
    auto handle_rfence(Word func_id) -> bool;
    auto handle_hsm(Word func_id) -> bool;
    auto handle_ipi(Word func_id) -> bool;
    auto handle_system_reset(Word func_id) -> bool;

    [[nodiscard]] auto timer_value() const -> Counter;
    void sbi_return(SignedWord error, Word value);

   private:
    core::CPU& cpu_;
};

}  // namespace simrv::sbi