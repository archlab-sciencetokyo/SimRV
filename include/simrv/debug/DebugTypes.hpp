#pragma once

#include <cstdint>

namespace simrv::debug {

// Symbol table & debugger domain types
enum class SymbolType : uint8_t {
    NoType = 0,
    Object = 1,
    Function = 2,
    Section = 3,
    File = 4,
    Common = 5,
    Tls = 6
};
enum class SymbolBinding : uint8_t { Local = 0, Global = 1, Weak = 2 };
using BreakpointId = uint32_t;
using WatchpointId = uint32_t;

// GDB RSP domain types
enum class GdbBreakpointType : uint8_t {
    Software = 0,
    Hardware,
    WriteWatch,
    ReadWatch,
    AccessWatch
};
enum class GdbSignal : uint8_t { SigInt = 2, SigTrap = 5, SigSegv = 11 };

}  // namespace simrv::debug

// Backward compatibility aliases in root namespace
using simrv::debug::BreakpointId;
using simrv::debug::GdbBreakpointType;
using simrv::debug::GdbSignal;
using simrv::debug::SymbolBinding;
using simrv::debug::SymbolType;
using simrv::debug::WatchpointId;
