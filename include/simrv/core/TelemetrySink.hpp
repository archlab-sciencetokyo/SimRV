/**
 * @file TelemetrySink.hpp
 * @brief Abstract telemetry and console sink interfaces decoupling the engine from UI.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "simrv/core/ExecutionController.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/AddressTypes.hpp"

namespace simrv::core {

class IConsoleSink {
   public:
    virtual ~IConsoleSink() = default;
    virtual void handle_char_write(char ch) = 0;
};

class ITelemetrySink : public IExecutionController {
   public:
    virtual ~ITelemetrySink() override = default;
    virtual void on_cycle_completed() = 0;
    [[nodiscard]] virtual bool is_trace_active() const = 0;
    [[nodiscard]] virtual bool captures_execution_detail() const = 0;

    virtual void record_instruction(simrv::xlen::VirtAddr /*pc*/, simrv::isa::Opcode /*opcode*/,
                                    simrv::isa::OperationId /*op_id*/, uint8_t /*rd*/,
                                    simrv::xlen::Register /*rd_val*/, uint8_t /*rs1*/,
                                    simrv::xlen::Register /*rs1_val*/, uint8_t /*rs2*/,
                                    simrv::xlen::Register /*rs2_val*/, int64_t /*imm*/,
                                    uint8_t /*hart*/) {}
    virtual void record_flight_instruction(simrv::xlen::VirtAddr /*pc*/,
                                           simrv::isa::Opcode /*opcode*/,
                                           simrv::isa::OperationId /*op_id*/, uint8_t /*hart*/) {}
    void record_instruction(simrv::xlen::Register, simrv::isa::Opcode, simrv::isa::OperationId,
                            uint8_t, simrv::xlen::Register, uint8_t, simrv::xlen::Register, uint8_t,
                            simrv::xlen::Register, int64_t, uint8_t) = delete;
    void record_flight_instruction(simrv::xlen::Register, simrv::isa::Opcode,
                                   simrv::isa::OperationId, uint8_t) = delete;
};

}  // namespace simrv::core
