/**
 * @file SpikeLockstep.cpp
 * @brief Spike co-simulation lockstep implementation.
 *
 * Spike is launched with:
 * @code
 *   spike --isa=<isa> -l [--dtb=<dtb>] <image>
 * @endcode
 *
 * With --log-commits Spike writes one line per committed instruction to
 * stderr in the format:
 *
 *   core   0: 3 0x80000000 (0x00000297) x5  0x80000000
 *   core   0: 3 0x80000004 (0x00028593)
 *
 * Fields:
 * @code
 *   "core"  <hart>  ":"  <priv>  <pc_hex>  "(" <insn_hex> ")"
 *   [<reg_name>  <reg_value_hex>]
 * @endcode
 *
 * We parse the PC and the optional register write, accumulate into a
 * SpikeCommitRecord, then compare against SimRV's ArchState.
 */
#include "simrv/debug/SpikeLockstep.hpp"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <charconv>
#include <csignal>
#include <cstring>
#include <format>
#include <limits>
#include <string_view>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::debug {

// ---------------------------------------------------------------------------
// ANSI colour helpers
// ---------------------------------------------------------------------------

namespace {
[[maybe_unused]] constexpr const char* kRed = "\033[31m";
[[maybe_unused]] constexpr const char* kGreen = "\033[32m";
[[maybe_unused]] constexpr const char* kYellow = "\033[33m";
[[maybe_unused]] constexpr const char* kBold = "\033[1m";
[[maybe_unused]] constexpr const char* kReset = "\033[0m";

// ABI register names for prettier output
constexpr std::array<const char*, 32> kAbiNames = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
}  // namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

SpikeLockstep::SpikeLockstep(std::string spike_bin, std::string mem_image, std::string disk_image,
                             std::string dtb_file, std::string isa_string, bool halt_on_diverge)
    : spike_bin_(std::move(spike_bin)),
      mem_image_(std::move(mem_image)),
      disk_image_(std::move(disk_image)),
      dtb_file_(std::move(dtb_file)),
      isa_string_(std::move(isa_string)),
      halt_on_diverge_(halt_on_diverge) {}

SpikeLockstep::~SpikeLockstep() { stop(); }

// ---------------------------------------------------------------------------
// Process management
// ---------------------------------------------------------------------------

auto SpikeLockstep::start() -> bool {
    // stderr pipe: Spike's commit log goes to stderr
    std::array<int, 2> stderr_pipe = {-1, -1};
    if (::pipe(stderr_pipe.data()) != 0) {
        simrv::log::error("SpikeLockstep: pipe() failed: {}", std::strerror(errno));
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        simrv::log::error("SpikeLockstep: fork() failed: {}", std::strerror(errno));
        ::close(stderr_pipe.at(0));
        ::close(stderr_pipe.at(1));
        return false;
    }

    if (pid == 0) {
        // Child: redirect stderr to write end of pipe
        ::close(stderr_pipe.at(0));
        ::dup2(stderr_pipe.at(1), STDERR_FILENO);
        ::close(stderr_pipe.at(1));

        // Redirect stdout to /dev/null (we don't want Spike console output)
        const int devnull =
            ::open("/dev/null", O_WRONLY);  // NOLINT(cppcoreguidelines-pro-type-vararg)
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::close(devnull);
        }

        // Build argument list
        // spike --isa=rv32gc -l [--dtb=dtb] [--disk=disk] mem_image
        std::vector<std::string> args_storage;
        args_storage.emplace_back(spike_bin_);
        args_storage.emplace_back(std::format("--isa={}", isa_string_));
        args_storage.emplace_back("-l");  // --log-commits (abbreviated)
        if (!dtb_file_.empty()) {
            args_storage.emplace_back(std::format("--dtb={}", dtb_file_));
        }
        if (!disk_image_.empty()) {
            args_storage.emplace_back(std::format("--disk={}", disk_image_));
        }
        args_storage.emplace_back(mem_image_);

        std::vector<char*> argv;
        argv.reserve(args_storage.size() + 1);
        for (auto& s : args_storage) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);

        ::execvp(spike_bin_.c_str(), argv.data());
        // execvp failed
        simrv::log::error("SpikeLockstep: execvp '{}' failed: {}", spike_bin_,
                          std::strerror(errno));
        ::_exit(127);
    }

    // Parent
    ::close(stderr_pipe.at(1));
    spike_stderr_ = stderr_pipe.at(0);
    spike_pid_ = pid;

    simrv::log::info("SpikeLockstep: Spike started (pid={})", spike_pid_);
    return true;
}

void SpikeLockstep::stop() {
    if (spike_stderr_ >= 0) {
        ::close(spike_stderr_);
        spike_stderr_ = -1;
    }
    if (spike_stdout_ >= 0) {
        ::close(spike_stdout_);
        spike_stdout_ = -1;
    }
    if (spike_pid_ > 0) {
        ::kill(spike_pid_, SIGTERM);
        int status = 0;
        ::waitpid(spike_pid_, &status, 0);
        spike_pid_ = -1;
    }
}

// ---------------------------------------------------------------------------
// Line-buffered reader
// ---------------------------------------------------------------------------

auto SpikeLockstep::read_line() -> std::string {
    auto record_and_return = [this](std::string line) -> std::string {
        if (!line.empty()) {
            spike_history_.push_back(line);
            if (spike_history_.size() > 20) {
                spike_history_.erase(spike_history_.begin());
            }
        }
        return line;
    };

    while (true) {
        // Check if we already have a newline in the buffer
        const auto pos = line_buf_.find('\n');
        if (pos != std::string::npos) {
            std::string line = line_buf_.substr(0, pos);
            line_buf_.erase(0, pos + 1);
            return record_and_return(line);
        }

        // Need more data
        std::array<char, 4096> tmp{};
        const ssize_t n = ::read(spike_stderr_, tmp.data(), tmp.size());
        if (n <= 0) {
            // EOF or error — return whatever is buffered
            std::string remainder = std::move(line_buf_);
            line_buf_.clear();
            return record_and_return(remainder);
        }
        line_buf_.append(tmp.data(), static_cast<std::size_t>(n));
    }
}

// ---------------------------------------------------------------------------
// Spike --log-commits line parser
// ---------------------------------------------------------------------------
// Format (from riscv-isa-sim/spike_main/spike.cc):
//   core   0: <priv> <pc> (<insn>) [<rd> <val>]
// e.g.:
//   core   0: 3 0x80000000 (0x00000297) x5  0x80000000
//   core   0: 3 0x80000004 (0x00028593)
// priv: 3=M, 1=S, 0=U

auto SpikeLockstep::parse_commit_line(const std::string& line, SpikeCommitRecord& rec)
    -> std::optional<SpikeCommitRecord> {
    // Must start with "core"
    if (!line.starts_with("core")) {
        return std::nullopt;
    }

    const std::string_view sv(line);

    // Find the instruction encoding parentheses "(0x"
    const auto paren_pos = sv.find("(0x");
    if (paren_pos == std::string::npos) {
        return std::nullopt;
    }

    // Find the last "0x" before "(0x"
    const auto pc_pos = sv.rfind("0x", paren_pos);
    if (pc_pos == std::string::npos) {
        return std::nullopt;
    }

    // Parse PC
    uint64_t pc_val = 0;
    const auto [ptr_pc, ec_pc] =
        std::from_chars(sv.data() + pc_pos + 2, sv.data() + paren_pos, pc_val, 16);
    if (ec_pc != std::errc{}) {
        return std::nullopt;
    }
    rec.pc = static_cast<Address>(pc_val);

    // Parse register write after the closing parenthesis of the instruction
    const auto close_paren_pos = sv.find(')', paren_pos);
    if (close_paren_pos == std::string::npos) {
        return std::nullopt;
    }

    auto skip_ws = [&](std::size_t idx) -> std::size_t {
        while (idx < sv.size() && (sv.at(idx) == ' ' || sv.at(idx) == '\t')) ++idx;
        return idx;
    };

    std::size_t i = skip_ws(close_paren_pos + 1);
    if (i >= sv.size()) {
        return rec;
    }

    // Parse register name (e.g. "x5", "a0", "zero", "pc")
    std::size_t reg_start = i;
    while (i < sv.size() && sv.at(i) != ' ' && sv.at(i) != '\t') ++i;
    const std::string_view reg_name = sv.substr(reg_start, i - reg_start);

    // Determine register index
    int reg_idx = -1;
    if (!reg_name.empty() && reg_name.at(0) == 'x') {
        // xN notation
        uint64_t n = 0;
        const auto [p, ec] =
            std::from_chars(reg_name.data() + 1, reg_name.data() + reg_name.size(), n, 10);
        if (ec == std::errc{} && n < 32) {
            reg_idx = static_cast<int>(n);
        }
    } else {
        // ABI name
        for (int k = 0; k < 32; ++k) {
            if (reg_name == kAbiNames.at(static_cast<std::size_t>(k))) {
                reg_idx = k;
                break;
            }
        }
    }

    if (reg_idx >= 0) {
        i = skip_ws(i);
        // Parse value "0x<hex>"
        if (i + 2 < sv.size() && sv.at(i) == '0' && sv.at(i + 1) == 'x') {
            i += 2;
            uint64_t val = 0;
            const auto [pv, ecv] = std::from_chars(sv.data() + i, sv.data() + sv.size(), val, 16);
            if (ecv == std::errc{}) {
                rec.gpr.at(static_cast<std::size_t>(reg_idx)) = static_cast<Register>(val);
                rec.gpr_valid.at(static_cast<std::size_t>(reg_idx)) = true;
            }
        }
    }

    return rec;
}

auto SpikeLockstep::read_and_cache_next_commit() -> std::optional<SpikeCommitRecord> {
    return peek_commit(0);
}

auto SpikeLockstep::next_commit() -> std::optional<SpikeCommitRecord> {
    if (!cached_recs_.empty()) {
        auto rec = cached_recs_.front();
        cached_recs_.erase(cached_recs_.begin());
        return rec;
    }

    SpikeCommitRecord rec{};
    while (true) {
        const std::string line = read_line();
        if (line.empty()) {
            return std::nullopt;
        }
        const auto result = parse_commit_line(line, rec);
        if (result) {
            return result;
        }
    }
}

auto SpikeLockstep::peek_commit(std::size_t index) -> std::optional<SpikeCommitRecord> {
    while (cached_recs_.size() <= index) {
        SpikeCommitRecord rec{};
        bool found = false;
        while (true) {
            const std::string line = read_line();
            if (line.empty()) {
                break;
            }
            const auto result = parse_commit_line(line, rec);
            if (result) {
                cached_recs_.push_back(*result);
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return cached_recs_.at(index);
}

auto SpikeLockstep::determine_sc_success() -> std::optional<bool> {
    // Look at the next 2 instruction commits.
    // If a backward jump (next->pc < cur->pc) occurs between instruction 1 and 2,
    // it means the retry branch was taken, so sc failed (return false).
    // Otherwise, it succeeded (return true).
    for (std::size_t i = 0; i < 2; ++i) {
        auto cur = peek_commit(i);
        auto next = peek_commit(i + 1);
        if (!cur || !next) {
            return std::nullopt;
        }
        if (next->pc < cur->pc) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Divergence reporting
// ---------------------------------------------------------------------------

void SpikeLockstep::print_divergence(uint64_t icount, Address simrv_pc, Address spike_pc,
                                     const simrv::core::ArchState& simrv_state,
                                     const SpikeCommitRecord& spike_rec) {
    simrv::log::error("[LOCKSTEP] Divergence at instruction #{}", icount);

    simrv::log::error("  Spike commit history:");
    for (const auto& line : spike_history_) {
        simrv::log::error("    {}", line);
    }

    if (simrv_pc != spike_pc) {
        simrv::log::error("  PC:   SimRV=0x{:08x}  Spike=0x{:08x}", static_cast<uint32_t>(simrv_pc),
                          static_cast<uint32_t>(spike_pc));
    } else {
        simrv::log::error("  PC:   0x{:08x}  (match)", static_cast<uint32_t>(simrv_pc));
    }

    // Show mismatched GPRs that Spike reported as written
    for (std::size_t r = 0; r < 32; ++r) {
        if (!spike_rec.gpr_valid.at(r)) continue;
        const auto simrv_val = static_cast<uint32_t>(simrv_state.regs.read(static_cast<RegId>(r)));
        const auto spike_val = static_cast<uint32_t>(spike_rec.gpr.at(r));
        if (simrv_val != spike_val) {
            simrv::log::error("  {}:  SimRV=0x{:08x}  Spike=0x{:08x}", kAbiNames.at(r), simrv_val,
                              spike_val);
        }
    }
}

// ---------------------------------------------------------------------------
// compare_and_report
// ---------------------------------------------------------------------------

auto SpikeLockstep::compare_and_report(const simrv::core::ArchState& state, Address current_pc,
                                       uint64_t icount) -> bool {
    if (!is_running()) return true;

    constexpr Address pc_mask = std::numeric_limits<Address>::max();
    const Address masked_current_pc = current_pc & pc_mask;

    SpikeCommitRecord spike_rec{};
    if (icount == 1) {
        // Discard Spike's bootrom instructions until we align with SimRV's start PC (current_pc)
        while (true) {
            const auto spike_rec_opt = next_commit();
            if (!spike_rec_opt) {
                simrv::log::warn("SpikeLockstep: Spike EOF during alignment at instruction #{}",
                                 icount);
                should_halt_ = halt_on_diverge_;
                return false;
            }
            if ((spike_rec_opt->pc & pc_mask) == masked_current_pc) {
                spike_rec = *spike_rec_opt;
                break;
            }
        }
    } else {
        const auto spike_rec_opt = next_commit();
        if (!spike_rec_opt) {
            simrv::log::warn("SpikeLockstep: Spike EOF at instruction #{}", icount);
            should_halt_ = halt_on_diverge_;
            return false;
        }
        spike_rec = *spike_rec_opt;
    }

    bool ok = true;

    // Compare PC
    if (masked_current_pc != (spike_rec.pc & pc_mask)) {
        ok = false;
    }

    // Compare GPRs that Spike reported as written
    if (ok) {
        for (std::size_t r = 0; r < 32; ++r) {
            if (!spike_rec.gpr_valid.at(r)) continue;
            const auto simrv_val = static_cast<uint32_t>(state.regs.read(static_cast<RegId>(r)));
            const auto spike_val = static_cast<uint32_t>(spike_rec.gpr.at(r));
            if (simrv_val != spike_val) {
                ok = false;
                break;
            }
        }
    }

    if (!ok) {
        print_divergence(icount, masked_current_pc, spike_rec.pc & pc_mask, state, spike_rec);
        if (halt_on_diverge_) {
            should_halt_ = true;
        }
    }

    return ok;
}

}  // namespace simrv::debug
