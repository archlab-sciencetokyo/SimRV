#pragma once

#include <cstdint>

// AIA domain types
enum class ImsicPrivilege : uint8_t { Machine = 0, Supervisor = 1 };
enum class AplicPrivilege : uint8_t { Machine = 0, Supervisor = 1 };
enum class AplicSourceMode : uint8_t {
    Inactive = 0,
    Detached = 1,
    Edge1 = 4,
    Edge0 = 5,
    Level1 = 6,
    Level0 = 7
};
