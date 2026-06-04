/**
 * @file SpikeLockstep.cpp
 * @brief Spike co-simulation lockstep implementation.
 *
 * Spike is launched with:
 *   spike --isa=<isa> -l [--dtb=<dtb>] <image>
 *
 * With --log-commits Spike writes one line per committed instruction to
 * stderr in the format:
 *
 *   core   0: 3 0x80000000 (0x00000297) x5  0x80000000
 *   core   0: 3 0x80000004 (0x00028593)
 *
 * Fields:
 *   "core"  <hart>  ":"  <priv>  <pc_hex>  "(" <insn_hex> ")"
 *   [<reg_name>  <reg_value_hex>]
 *
 * We parse the PC and the optional register write, accumulate into a
 * SpikeCommitRecord, then compare against SimRV's ArchState.
 */
#include "simrv/debug/SpikeLockstep.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <charconv>
#include <cstring>
#include <format>
#include <print>
#include <string_view>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::debug {

// ---------------------------------------------------------------------------
// ANSI colour helpers
// ---------------------------------------------------------------------------

namespace {
constexpr const char* kRed    = "\033[31m";
constexpr const char* kGreen  = "\033[32m";
constexpr const char* kYellow = "\033[33m";
constexpr const char* kBold   = "\033[1m";
constexpr const char* kReset  = "\033[0m";

// ABI register names for prettier output
constexpr std::array<const char*, 32> kAbiNames = {
    "zero","ra","sp","gp","tp","t0","t1","t2",
    "s0","s1","a0","a1","a2","a3","a4","a5",
    "a6","a7","s2","s3","s4","s5","s6","s7",
    "s8","s9","s10","s11","t3","t4","t5","t6"
};
}  // namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

SpikeLockstep::SpikeLockstep(std::string spike_bin,
                               std::string mem_image,
                               std::string disk_image,
                               std::string dtb_file,
                               std::string isa_string,
                               bool        halt_on_diverge)
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

bool SpikeLockstep::start() {
    // stderr pipe: Spike's commit log goes to stderr
    int stderr_pipe[2] = {-1, -1};
    if (::pipe(stderr_pipe) != 0) {
        simrv::log::error("SpikeLockstep: pipe() failed: {}", std::strerror(errno));
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        simrv::log::error("SpikeLockstep: fork() failed: {}", std::strerror(errno));
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);
        return false;
    }

    if (pid == 0) {
        // Child: redirect stderr to write end of pipe
        ::close(stderr_pipe[0]);
        ::dup2(stderr_pipe[1], STDERR_FILENO);
        ::close(stderr_pipe[1]);

        // Redirect stdout to /dev/null (we don't want Spike console output)
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::close(devnull);
        }

        // Build argument list
        // spike --isa=rv32gc -l [--dtb=dtb] [--disk=disk] mem_image
        std::vector<std::string> args_storage;
        args_storage.push_back(spike_bin_);
        args_storage.push_back(std::format("--isa={}", isa_string_));
        args_storage.push_back("-l");  // --log-commits (abbreviated)
        if (!dtb_file_.empty()) {
            args_storage.push_back(std::format("--dtb={}", dtb_file_));
        }
        if (!disk_image_.empty()) {
            args_storage.push_back(std::format("--disk={}", disk_image_));
        }
        args_storage.push_back(mem_image_);

        std::vector<char*> argv;
        argv.reserve(args_storage.size() + 1);
        for (auto& s : args_storage) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);

        ::execvp(spike_bin_.c_str(), argv.data());
        // execvp failed
        std::println(stderr, "SpikeLockstep: execvp '{}' failed: {}",
                     spike_bin_, std::strerror(errno));
        ::_exit(127);
    }

    // Parent
    ::close(stderr_pipe[1]);
    spike_stderr_ = stderr_pipe[0];
    spike_pid_    = pid;

    simrv::log::info("SpikeLockstep: Spike started (pid={})", spike_pid_);
    return true;
}

void SpikeLockstep::stop() {
    if (spike_pid_ > 0) {
        ::kill(spike_pid_, SIGTERM);
        int status = 0;
        ::waitpid(spike_pid_, &status, 0);
        spike_pid_ = -1;
    }
    if (spike_stderr_ >= 0) {
        ::close(spike_stderr_);
        spike_stderr_ = -1;
    }
    if (spike_stdout_ >= 0) {
        ::close(spike_stdout_);
        spike_stdout_ = -1;
    }
}

// ---------------------------------------------------------------------------
// Line-buffered reader
// ---------------------------------------------------------------------------

std::string SpikeLockstep::read_line() {
    while (true) {
        // Check if we already have a newline in the buffer
        const auto pos = line_buf_.find('\n');
        if (pos != std::string::npos) {
            std::string line = line_buf_.substr(0, pos);
            line_buf_.erase(0, pos + 1);
            return line;
        }

        // Need more data
        std::array<char, 4096> tmp{};
        const ssize_t n = ::read(spike_stderr_, tmp.data(), tmp.size());
        if (n <= 0) {
            // EOF or error — return whatever is buffered
            std::string remainder = std::move(line_buf_);
            line_buf_.clear();
            return remainder;
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

std::optional<SpikeCommitRecord>
SpikeLockstep::parse_commit_line(const std::string& line,
                                  SpikeCommitRecord& rec) {
    // Must start with "core"
    if (!line.starts_with("core")) {
        return std::nullopt;
    }

    // Find PC: first "0x" after the ":"
    const auto colon_pos = line.find(':');
    if (colon_pos == std::string::npos) return std::nullopt;

    // Skip priv field, find PC hex
    const std::string_view sv(line);
    auto skip_ws = [&](std::size_t i) {
        while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\t')) ++i;
        return i;
    };

    // After colon: " <priv> 0x<pc>"
    std::size_t i = colon_pos + 1;
    i = skip_ws(i);
    // Skip priv digit(s)
    while (i < sv.size() && sv[i] != ' ' && sv[i] != '\t') ++i;
    i = skip_ws(i);

    // Parse PC
    if (i + 2 >= sv.size() || sv[i] != '0' || sv[i + 1] != 'x') return std::nullopt;
    i += 2;
    uint64_t pc_val = 0;
    const auto [ptr_pc, ec_pc] =
        std::from_chars(sv.data() + i, sv.data() + sv.size(), pc_val, 16);
    if (ec_pc != std::errc{}) return std::nullopt;
    rec.pc = static_cast<Address>(pc_val);
    i = static_cast<std::size_t>(ptr_pc - sv.data());

    // Skip "(0x<insn>)"
    i = skip_ws(i);
    if (i < sv.size() && sv[i] == '(') {
        while (i < sv.size() && sv[i] != ')') ++i;
        if (i < sv.size()) ++i;  // skip ')'
    }

    // Optional: " <regname> <val>"
    i = skip_ws(i);
    if (i >= sv.size()) return rec;

    // Parse register name (e.g. "x5", "a0", "zero", "pc")
    std::size_t reg_start = i;
    while (i < sv.size() && sv[i] != ' ' && sv[i] != '\t') ++i;
    const std::string_view reg_name = sv.substr(reg_start, i - reg_start);

    // Determine register index
    int reg_idx = -1;
    if (!reg_name.empty() && reg_name[0] == 'x') {
        // xN notation
        uint64_t n = 0;
        const auto [p, ec] = std::from_chars(
            reg_name.data() + 1, reg_name.data() + reg_name.size(), n, 10);
        if (ec == std::errc{} && n < 32) {
            reg_idx = static_cast<int>(n);
        }
    } else {
        // ABI name
        for (int k = 0; k < 32; ++k) {
            if (reg_name == kAbiNames[static_cast<std::size_t>(k)]) {
                reg_idx = k;
                break;
            }
        }
    }

    if (reg_idx >= 0) {
        i = skip_ws(i);
        // Parse value "0x<hex>"
        if (i + 2 < sv.size() && sv[i] == '0' && sv[i + 1] == 'x') {
            i += 2;
            uint64_t val = 0;
            const auto [pv, ecv] =
                std::from_chars(sv.data() + i, sv.data() + sv.size(), val, 16);
            if (ecv == std::errc{}) {
                rec.gpr[static_cast<std::size_t>(reg_idx)] =
                    static_cast<Register>(val);
                rec.gpr_valid[static_cast<std::size_t>(reg_idx)] = true;
            }
        }
    }

    return rec;
}

std::optional<SpikeCommitRecord> SpikeLockstep::next_commit() {
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
        // Skip non-commit lines (e.g. banner, interrupt messages)
    }
}

// ---------------------------------------------------------------------------
// Divergence reporting
// ---------------------------------------------------------------------------

void SpikeLockstep::print_divergence(uint64_t icount, Address simrv_pc,
                                      Address spike_pc,
                                      const simrv::core::ArchState& simrv_state,
                                      const SpikeCommitRecord& spike_rec) {
    // Use fprintf to avoid consteval format-string issues with const char* ANSI codes
    std::fprintf(stderr, "%s%s[LOCKSTEP] Divergence at instruction #%llu%s\n",
        kBold, kRed, static_cast<unsigned long long>(icount), kReset);

    if (simrv_pc != spike_pc) {
        std::fprintf(stderr, "  PC:   SimRV=%s0x%08x%s  Spike=%s0x%08x%s\n",
            kRed, static_cast<uint32_t>(simrv_pc), kReset,
            kGreen, static_cast<uint32_t>(spike_pc), kReset);
    } else {
        std::fprintf(stderr, "  PC:   0x%08x  (match)\n",
            static_cast<uint32_t>(simrv_pc));
    }

    // Show mismatched GPRs that Spike reported as written
    for (std::size_t r = 0; r < 32; ++r) {
        if (!spike_rec.gpr_valid[r]) continue;
        const auto simrv_val = static_cast<uint32_t>(
            simrv_state.regs.read(static_cast<RegId>(r)));
        const auto spike_val = static_cast<uint32_t>(spike_rec.gpr[r]);
        if (simrv_val != spike_val) {
            std::fprintf(stderr, "  %s%s%s:  SimRV=%s0x%08x%s  Spike=%s0x%08x%s\n",
                kYellow, kAbiNames[r], kReset,
                kRed, simrv_val, kReset,
                kGreen, spike_val, kReset);
        }
    }
}

// ---------------------------------------------------------------------------
// compare_and_report
// ---------------------------------------------------------------------------

bool SpikeLockstep::compare_and_report(const simrv::core::ArchState& state,
                                        uint64_t icount) {
    if (!is_running()) return true;

    const auto spike_rec_opt = next_commit();
    if (!spike_rec_opt) {
        simrv::log::warn("SpikeLockstep: Spike EOF at instruction #{}", icount);
        should_halt_ = halt_on_diverge_;
        return false;
    }
    const SpikeCommitRecord& spike_rec = *spike_rec_opt;

    bool ok = true;

    // Compare PC
    if (state.pc != spike_rec.pc) {
        ok = false;
    }

    // Compare GPRs that Spike reported as written
    if (ok) {
        for (std::size_t r = 0; r < 32; ++r) {
            if (!spike_rec.gpr_valid[r]) continue;
            const auto simrv_val = static_cast<uint32_t>(
                state.regs.read(static_cast<RegId>(r)));
            const auto spike_val = static_cast<uint32_t>(spike_rec.gpr[r]);
            if (simrv_val != spike_val) {
                ok = false;
                break;
            }
        }
    }

    if (!ok) {
        print_divergence(icount, state.pc, spike_rec.pc, state, spike_rec);
        if (halt_on_diverge_) {
            should_halt_ = true;
        }
    }

    return ok;
}

}  // namespace simrv::debug
