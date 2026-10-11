#pragma once

#include <cstdint>
#include <utility>

#include "simrv/xlen/ScalarTypes.hpp"

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
    NoShmem = -9
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
