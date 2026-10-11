#pragma once

#include <cstdint>

// Floating-point execution domain types
enum class FpFmt : uint8_t { Single = 0, Double = 1, Half = 2, Quad = 3 };
enum class VectorMaskMode : bool { Masked = false, Unmasked = true };

enum class FpPrecision : uint8_t { Single = 32, Double = 64, Quad = 128 };
