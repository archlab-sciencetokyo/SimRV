/**
 * @file Sbi.hpp
 * @brief Supervisor Binary Interface (SBI) exception handler.
 */
#pragma once

#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class CPU;
}

namespace simrv::sbi {
auto handle_sbi_ecall(core::CPU& cpu, TrapCause cause) -> bool;
}  // namespace simrv::sbi