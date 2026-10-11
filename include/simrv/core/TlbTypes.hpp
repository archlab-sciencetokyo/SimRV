#pragma once

#include <cstdint>

// TLB domain types
using Asid = uint16_t;
using TlbSetIndex = uint32_t;
using TlbWay = uint8_t;
enum class TlbAccessKind : uint8_t { Instruction, DataRead, DataWrite };
