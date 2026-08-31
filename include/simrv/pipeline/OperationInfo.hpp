#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>

#include "simrv/isa/OperationId.hpp"

namespace simrv::pipeline::operation {

enum class ExecutionClass : uint8_t {
    Default,
    Multiply,
    DivideOrRemainder,
    FpAlu,
    FpDivideOrSqrt,
};

struct OperationInfo {
    ExecutionClass execution_class = ExecutionClass::Default;
};

consteval auto generate_operation_info() {
    std::array<OperationInfo, isa::kOperationIdCount> info{};
    const auto assign = [&info](ExecutionClass execution_class,
                                std::initializer_list<isa::OperationId> operations) {
        for (const auto operation : operations) {
            auto& entry = info.at(static_cast<std::size_t>(operation));
            if (entry.execution_class != ExecutionClass::Default) {
                throw "operation has multiple execution classes";
            }
            entry.execution_class = execution_class;
        }
    };

    using enum isa::OperationId;
    assign(ExecutionClass::Multiply, {MUL, MULH, MULHSU, MULHU, MULW});
    assign(ExecutionClass::DivideOrRemainder, {DIV, DIVU, REM, REMU, DIVW, DIVUW, REMW, REMUW});
    assign(ExecutionClass::FpDivideOrSqrt, {FDIV_S, FSQRT_S, FDIV_D, FSQRT_D});
    assign(ExecutionClass::FpAlu,
           {FADD_S,    FSUB_S,    FMUL_S,    FMADD_S,   FMSUB_S,   FNMADD_S,  FNMSUB_S,
            FADD_D,    FSUB_D,    FMUL_D,    FMADD_D,   FMSUB_D,   FNMADD_D,  FNMSUB_D,
            FMIN_S,    FMAX_S,    FMIN_D,    FMAX_D,    FSGNJ_S,   FSGNJN_S,  FSGNJX_S,
            FSGNJ_D,   FSGNJN_D,  FSGNJX_D,  FCVT_S_D,  FCVT_D_S,  FCVT_W_S,  FCVT_WU_S,
            FCVT_S_W,  FCVT_S_WU, FCVT_L_S,  FCVT_LU_S, FCVT_S_L,  FCVT_S_LU, FCVT_W_D,
            FCVT_WU_D, FCVT_D_W,  FCVT_D_WU, FCVT_L_D,  FCVT_LU_D, FCVT_D_L,  FCVT_D_LU});
    return info;
}

inline constexpr auto kOperationInfo = generate_operation_info();

[[nodiscard]] constexpr auto info(isa::OperationId operation) noexcept -> OperationInfo {
    const auto index = static_cast<std::size_t>(operation);
    return index < kOperationInfo.size() ? kOperationInfo[index] : OperationInfo{};
}

}  // namespace simrv::pipeline::operation
