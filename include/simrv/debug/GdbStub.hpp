/**
 * @file GdbStub.hpp
 * @brief GDB Remote Serial Protocol (RSP) stub for SimRV.
 *
 * Implements a minimal GDB RSP server over TCP so that a RISC-V-aware GDB
 * client can attach to a running simulation and:
 *   - Read/write integer and floating-point registers
 *   - Read/write physical memory
 *   - Single-step or continue execution
 *   - Insert/remove software breakpoints (EBREAK injection)
 *   - Receive SIGTRAP on breakpoint hits or Ctrl-C
 *
 * Usage:
 *   GdbStub stub(1234);
 *   stub.wait_for_connection();          // blocks until GDB connects
 *   // in run loop:
 *   stub.poll(machine);                  // non-blocking check for ^C / 'c'/'s'
 *   // when EBREAK or hw-breakpoint fires:
 *   stub.notify_breakpoint(machine);     // enters the blocking command loop
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace simrv::core {
class Machine;
}

namespace simrv::debug {

/**
 * @class GdbStub
 * @brief GDB RSP server for a single-threaded RISC-V simulation.
 */
class GdbStub {
   public:
    /**
     * @brief Create a GDB RSP stub that listens on the given TCP port.
     * @param port TCP port to listen on (default 1234).
     */
    explicit GdbStub(uint16_t port);
    ~GdbStub();

    GdbStub(const GdbStub&) = delete;
    auto operator=(const GdbStub&) -> GdbStub& = delete;
    GdbStub(GdbStub&&) = delete;
    auto operator=(GdbStub&&) -> GdbStub& = delete;

    /** @brief Block until a GDB client connects. */
    void wait_for_connection();

    /** @return True when a GDB client is currently connected. */
    [[nodiscard]] bool is_connected() const { return conn_fd_ >= 0; }

    /**
     * @brief Non-blocking poll: process any pending RSP packets.
     *
     * Returns immediately if no data is available.  Should be called from the
     * main simulation loop.  When a 'c' (continue) packet has been processed,
     * returns normally.  When a step packet arrives this function sets
     * single_step_ and returns so the caller executes one `run_cycle()`.
     */
    void poll(simrv::core::Machine& machine);

    /**
     * @brief Notify GDB that execution has halted (breakpoint/EBREAK/single-step).
     *
     * Sends a SIGTRAP stop-reply and enters a blocking loop processing RSP
     * commands until GDB sends a 'c' or 's' packet.
     */
    void notify_breakpoint(simrv::core::Machine& machine);

    /** @return True if GDB requested single-step mode. */
    [[nodiscard]] bool single_step() const { return single_step_; }

   private:
    // ---- TCP socket management ----
    int listen_fd_ = -1;
    int conn_fd_ = -1;
    uint16_t port_;

    // ---- RSP protocol state ----
    bool single_step_ = false;
    bool no_ack_mode_ = false;

    // Software breakpoint table: addr -> original 4-byte word
    std::unordered_map<uint32_t, uint32_t> sw_breakpoints_;

    // ---- RSP packet I/O ----
    /** Send a formatted RSP packet (adds '$', checksum, '#'). */
    void send_packet(const std::string& data);
    /** Send a pre-computed packet string directly. */
    void send_raw(const std::string& s);
    /** Read one character from the connection; returns -1 on error/close. */
    [[nodiscard]] auto recv_char() -> int;
    /**
     * @brief Read one complete RSP packet into `out`.
     * @return true on success, false if connection closed or malformed.
     */
    [[nodiscard]] auto recv_packet(std::string& out) -> bool;

    // ---- RSP command handlers ----
    /**
     * @brief Dispatch one RSP packet to the appropriate handler.
     * @param pkt    Packet payload (without '$'/'#'/checksum).
     * @param machine The current machine state.
     * @return true if execution should resume (continue or detach), false to
     *         keep processing commands.
     */
    [[nodiscard]] auto handle_packet(const std::string& pkt, simrv::core::Machine& machine) -> bool;

    void handle_query(const std::string& pkt, simrv::core::Machine& machine);

    /** 'g' — read all registers */
    void cmd_read_registers(simrv::core::Machine& machine);
    /** 'G' — write all registers */
    void cmd_write_registers(const std::string& pkt, simrv::core::Machine& machine);
    /** 'p n' — read single register */
    void cmd_read_register(const std::string& pkt, simrv::core::Machine& machine);
    /** 'P n=v' — write single register */
    void cmd_write_register(const std::string& pkt, simrv::core::Machine& machine);
    /** 'm addr,len' — read memory */
    void cmd_read_memory(const std::string& pkt, simrv::core::Machine& machine);
    /** 'M addr,len:data' — write memory */
    void cmd_write_memory(const std::string& pkt, simrv::core::Machine& machine);
    /** 'Z0,addr,4' — insert software breakpoint */
    void cmd_insert_breakpoint(const std::string& pkt, simrv::core::Machine& machine);
    /** 'z0,addr,4' — remove software breakpoint */
    void cmd_remove_breakpoint(const std::string& pkt, simrv::core::Machine& machine);

   public:
    // ---- Public Utility (used by file-scope helpers in GdbStub.cpp) ----
    /** Format a XLEN-wide register value as a little-endian hex string. */
    static auto reg_to_hex(uint32_t val) -> std::string;
    /** Parse a hex string as a little-endian XLEN-wide register value. */
    static auto hex_to_reg(const std::string& s, std::size_t offset) -> uint32_t;
    /** Compute RSP checksum (sum of bytes mod 256). */
    static auto checksum(const std::string& data) -> uint8_t;

   private:
    // ---- Private Utility ----
    /** Close and reset the client connection socket. */
    void close_connection();

    // Number of registers in the GDB RISC-V target description:
    // x0–x31 (32), pc (1), f0–f31 (32) each 64-bit
    // For RV32: x-regs and pc are 32-bit, f-regs are 64-bit.
    static constexpr std::size_t kNumIntRegs = 33;  // x0..x31 + pc
    static constexpr std::size_t kNumFpRegs = 32;   // f0..f31
};

}  // namespace simrv::debug
