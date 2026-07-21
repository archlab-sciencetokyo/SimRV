#pragma once

#include "simrv/Define.hpp"

namespace simrv::core {
class CPU;
class Machine;
}

namespace simrv::execute::vector {

void execute_vector_config(core::CPU& cpu, isa::OperationId op_id, Instruction ir,
                           RegId rd, RegId rs1, RegId rs2);

void execute_vector_memory(core::CPU& cpu, core::Machine& machine, isa::OperationId op_id,
                           RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew);

void execute_vector_integer(core::CPU& cpu, isa::OperationId op_id,
                            RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                            Register rs1_val, int32_t simm5);

void execute_vector_fixed_point(core::CPU& cpu, isa::OperationId op_id,
                                RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                Register rs1_val, int32_t simm5);

void execute_vector_float(core::CPU& cpu, isa::OperationId op_id,
                          RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew);

void execute_vector_permute(core::CPU& cpu, isa::OperationId op_id,
                            RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                            Register rs1_val, int32_t simm5);

} // namespace simrv::execute::vector
