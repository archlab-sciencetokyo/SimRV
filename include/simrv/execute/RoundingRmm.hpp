#pragma once

#include <cstdint>

namespace simrv::execute::rmm {

[[nodiscard]] auto add(float lhs, float rhs) -> float;
[[nodiscard]] auto add(double lhs, double rhs) -> double;
[[nodiscard]] auto sub(float lhs, float rhs) -> float;
[[nodiscard]] auto sub(double lhs, double rhs) -> double;
[[nodiscard]] auto mul(float lhs, float rhs) -> float;
[[nodiscard]] auto mul(double lhs, double rhs) -> double;
[[nodiscard]] auto div(float lhs, float rhs) -> float;
[[nodiscard]] auto div(double lhs, double rhs) -> double;
[[nodiscard]] auto sqrt(float value) -> float;
[[nodiscard]] auto sqrt(double value) -> double;
[[nodiscard]] auto fma(float lhs, float rhs, float addend) -> float;
[[nodiscard]] auto fma(double lhs, double rhs, double addend) -> double;
[[nodiscard]] auto narrow_f32(double value) -> float;

[[nodiscard]] auto from_integer_f32(int32_t value) -> float;
[[nodiscard]] auto from_integer_f32(uint32_t value) -> float;
[[nodiscard]] auto from_integer_f32(int64_t value) -> float;
[[nodiscard]] auto from_integer_f32(uint64_t value) -> float;
[[nodiscard]] auto from_integer_f64(int32_t value) -> double;
[[nodiscard]] auto from_integer_f64(uint32_t value) -> double;
[[nodiscard]] auto from_integer_f64(int64_t value) -> double;
[[nodiscard]] auto from_integer_f64(uint64_t value) -> double;

}  // namespace simrv::execute::rmm
