#pragma once

#include <cstdint>
#include <utility>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::sbi {

// Standard RISC-V SBI specification version constants
inline constexpr uint32_t kSbiSpecVersion_0_1 = 0x00000001U;
inline constexpr uint32_t kSbiSpecVersion_0_2 = 0x00000002U;
inline constexpr uint32_t kSbiSpecVersion_0_3 = 0x00000003U;
inline constexpr uint32_t kSbiSpecVersion_1_0 = 0x01000000U;
inline constexpr uint32_t kSbiSpecVersion_2_0 = 0x02000000U;
inline constexpr uint32_t kSbiSpecVersion_3_0 = 0x03000000U;

// SBI domain types
enum class SbiError : int64_t {
    Success = 0,
    Failed = -1,
    NotSupported = -2,
    InvalidParam = -3,
    Denied = -4,
    InvalidAddress = -5,
    AlreadyAvailable = -6,
    AlreadyStarted = -7,
    AlreadyStopped = -8,
    NoShmem = -9,
    InvalidState = -10,
    BadRange = -11,
    Timeout = -12,
    Io = -13,
};

struct SbiRet {
    SbiError error{SbiError::Success};
    simrv::xlen::Word value{0};
};

enum class SbiExtId : uint32_t {
    LegacySetTimer = 0x00,
    LegacyConsolePutchar = 0x01,
    LegacyConsoleGetchar = 0x02,
    LegacyShutdown = 0x08,
    Base = 0x10,
    Time = 0x54494D45,          // TIME
    Rfence = 0x52464E43,        // RFNC
    Ipi = 0x735049,             // sPI
    Hsm = 0x48534D,             // HSM
    SystemReset = 0x53525354,   // SRST
    Pmu = 0x504D55,             // PMU
    DebugConsole = 0x4442434E,  // DBCN
    Susp = 0x53555350,          // SUSP
    Cppc = 0x43505043,          // CPPC
    Fwft = 0x46574654,          // FWFT
};

constexpr auto operator==(simrv::xlen::Word raw, SbiExtId ext) noexcept -> bool {
    return raw == std::to_underlying(ext);
}
constexpr auto operator==(SbiExtId ext, simrv::xlen::Word raw) noexcept -> bool {
    return std::to_underlying(ext) == raw;
}
constexpr auto operator!=(simrv::xlen::Word raw, SbiExtId ext) noexcept -> bool {
    return !(raw == ext);
}
constexpr auto operator!=(SbiExtId ext, simrv::xlen::Word raw) noexcept -> bool {
    return !(ext == raw);
}

}  // namespace simrv::sbi

namespace simrv {
using sbi::kSbiSpecVersion_0_1;
using sbi::kSbiSpecVersion_0_2;
using sbi::kSbiSpecVersion_0_3;
using sbi::kSbiSpecVersion_1_0;
using sbi::kSbiSpecVersion_2_0;
using sbi::kSbiSpecVersion_3_0;
using sbi::SbiError;
using sbi::SbiExtId;
using sbi::SbiRet;
}  // namespace simrv

// Global compatibility aliases
using simrv::sbi::kSbiSpecVersion_0_1;
using simrv::sbi::kSbiSpecVersion_0_2;
using simrv::sbi::kSbiSpecVersion_0_3;
using simrv::sbi::kSbiSpecVersion_1_0;
using simrv::sbi::kSbiSpecVersion_2_0;
using simrv::sbi::kSbiSpecVersion_3_0;
using simrv::sbi::SbiError;
using simrv::sbi::SbiExtId;
using simrv::sbi::SbiRet;
