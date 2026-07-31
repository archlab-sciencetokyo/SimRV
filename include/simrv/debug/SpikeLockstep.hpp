/**
 * @file SpikeLockstep.hpp
 * @brief Spike co-simulation for instruction-level lockstep verification.
 *
 * Launches the Spike RISC-V ISA reference simulator as a child process with
 * `--log-commits` and compares its retired-instruction stream against SimRV's
 * committed state after every instruction.
 *
 * On divergence, a coloured diff is printed to stderr and the simulation is
 * halted (configurable).
 *
 * Usage:
 *   SpikeLockstep lockstep("spike", mem_img, disk_img, dtb);
 *   lockstep.start();
 *   // after each committed instruction:
 *   lockstep.compare_and_report(cpu.state(), cpu.e_icount);
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
struct ArchState;
}

namespace simrv::debug {

/**
 * @brief Build the lowercase ISA string Spike expects from the active MISA value.
 *
 * Derives the ISA string from the compile-time XLEN (kIsXLen64) and the
 * runtime MISA extension bits.  Produces strings like "rv32gc", "rv64imac",
 * "rv64gc", etc.
 *
 * @param misa  The machine MISA CSR value (without the MXL field is fine;
 *              extension bits 0-25 are used).
 * @return      Lowercase ISA string suitable for Spike's --isa= flag.
 */
[[nodiscard]] inline auto spike_isa_string(CSRValue misa) -> std::string {
    unsigned xlen = 32;
    if constexpr (kIsXLen64) {
        const unsigned mxl = (misa >> 62) & 3;
        xlen = (mxl == 1) ? 32 : 64;
    }
    std::string s = (xlen == 64) ? "rv64" : "rv32";

    // Check IMAFD bits for the 'G' shorthand
    const auto bit = [&](char c) -> bool {
        return (misa & (static_cast<CSRValue>(1) << (c - 'a'))) != 0;
    };
    const bool is_g = bit('i') && bit('m') && bit('a') && bit('f') && bit('d');

    if (is_g) {
        s += 'g';
        // Append any extension beyond IMAFD
        for (int i = 0; i < 26; ++i) {
            if ((misa & (static_cast<CSRValue>(1) << i)) == 0) continue;
            const char c = static_cast<char>('a' + i);
            if (c == 'i' || c == 'm' || c == 'a' || c == 'f' || c == 'd') continue;
            if (c == 's' || c == 'u') continue;
            s += c;
        }
    } else {
        for (int i = 0; i < 26; ++i) {
            if ((misa & (static_cast<CSRValue>(1) << i)) != 0) {
                const char c = static_cast<char>('a' + i);
                if (c == 's' || c == 'u') continue;
                s += c;
            }
        }
    }
    return s;
}

/**
 * @struct SpikeCommitRecord
 * @brief One instruction's committed effect as reported by Spike --log-commits.
 */
struct SpikeCommitRecord {
    Address pc{};                      ///< PC of retired instruction
    std::array<Register, 32> gpr{};    ///< Full GPR snapshot after retire
    std::array<bool, 32> gpr_valid{};  ///< True for registers written this insn
};

/**
 * @class SpikeLockstep
 * @brief Manages a Spike child process and performs per-instruction comparison.
 */
class SpikeLockstep {
   public:
    /**
     * @brief Prepare lockstep session parameters.
     * @param spike_bin   Path to the spike executable (e.g. "spike" or "/opt/riscv/bin/spike").
     * @param mem_image   Memory image file path (same as SimRV -m).
     * @param disk_image  Disk image file path (same as SimRV -d), may be empty.
     * @param dtb_file    Device-tree binary file path (same as SimRV -c), may be empty.
     * @param isa_string  RISC-V ISA string passed to spike.  Use spike_isa_string(misa)
     *                    to derive automatically from the active MISA CSR.
     * @param halt_on_diverge  If true, set the halt flag on first mismatch.
     */
    SpikeLockstep(std::string spike_bin, std::string mem_image, std::string disk_image,
                  std::string dtb_file, std::string isa_string, bool halt_on_diverge = true);

    ~SpikeLockstep();

    SpikeLockstep(const SpikeLockstep&) = delete;
    auto operator=(const SpikeLockstep&) -> SpikeLockstep& = delete;
    SpikeLockstep(SpikeLockstep&&) = delete;
    auto operator=(SpikeLockstep&&) -> SpikeLockstep& = delete;

    /**
     * @brief Fork Spike and open the log-commit pipe.
     * @return true on success.
     */
    [[nodiscard]] auto start() -> bool;

    /** @brief Terminate Spike and close all file descriptors. */
    void stop();

    /** @return True if Spike is running. */
    [[nodiscard]] bool is_running() const { return spike_pid_ > 0; }

    /**
     * @brief Read the next commit record from Spike's output.
     * @return The record, or std::nullopt on EOF or parse error.
     */
    [[nodiscard]] auto next_commit() -> std::optional<SpikeCommitRecord>;

    /** @brief Reads the next commit from Spike (if not cached) and returns it. */
    auto read_and_cache_next_commit() -> std::optional<SpikeCommitRecord>;

    /**
     * @brief Compare SimRV's committed state with the next Spike record.
     *
     * Reads one record from Spike (blocking), then compares PC and any
     * GPR values that Spike reported as written.  Prints a coloured diff on
     * mismatch.
     *
     * @param state   SimRV's ArchState after the just-committed instruction.
     * @param icount  Instruction count (for diagnostics).
     * @return true if states match, false on divergence.
     */
    auto compare_and_report(const simrv::core::ArchState& state, Address current_pc,
                            uint64_t icount) -> bool;

    /** @return True if a divergence has been detected and simulation should halt. */
    [[nodiscard]] bool should_halt() const { return should_halt_; }

    /** Peek at a future commit record without consuming it. */
    [[nodiscard]] auto peek_commit(std::size_t index) -> std::optional<SpikeCommitRecord>;

    /** Determine if the current sc instruction succeeded based on Spike's future execution path. */
    [[nodiscard]] auto determine_sc_success() -> std::optional<bool>;

   private:
    std::string spike_bin_;
    std::string mem_image_;
    std::string disk_image_;
    std::string dtb_file_;
    std::string isa_string_;
    bool halt_on_diverge_;

    pid_t spike_pid_ = -1;
    int spike_stdout_ = -1;  ///< Read end of Spike's stdout pipe (--log-commits goes to stderr)
    int spike_stderr_ = -1;  ///< Read end of Spike's stderr pipe
    bool should_halt_ = false;

    // Line-level buffered reader for spike_stderr_
    std::string line_buf_;
    std::vector<std::string> spike_history_;
    std::vector<SpikeCommitRecord> cached_recs_;

    /** Read one '\n'-terminated line from Spike's stderr; returns empty on EOF. */
    [[nodiscard]] auto read_line() -> std::string;

    /** Parse one Spike --log-commits line into a SpikeCommitRecord. */
    [[nodiscard]] static auto parse_commit_line(const std::string& line, SpikeCommitRecord& rec)
        -> std::optional<SpikeCommitRecord>;

    /** Print a coloured divergence report. */
    void print_divergence(uint64_t icount, Address simrv_pc, Address spike_pc,
                          const simrv::core::ArchState& simrv_state,
                          const SpikeCommitRecord& spike_rec);
};

}  // namespace simrv::debug
