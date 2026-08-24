/**
 * @file GlossaryModal.cpp
 * @brief Implementation of RISC-V Architecture Glossary and Concept Explorer dialog.
 */
#include "simrv/tui/modals/GlossaryModal.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::modals {

namespace {

/// Word-wrap `line` (which may contain ANSI escape sequences) to `max_w` visible characters.
/// Continuation lines are indented by `cont_indent` spaces to align under bullet text.
/// Returns one or more output lines ready to pass to add_row_cb (without trailing newline).
auto wrap_ansi_line(const std::string& line, int max_w, int cont_indent = 2)
    -> std::vector<std::string> {
    if (max_w <= 0) return {line};

    // Measure visible display width of a string that may have ANSI escapes.
    auto vis_width = [](const std::string& s) -> int { return get_display_width(s); };

    // Split line into tokens preserving ANSI escapes so we can re-emit them on continuation lines.
    // We walk the string character by character, collecting "words" (non-space runs including any
    // embedded ANSI sequences) and "spaces".
    std::vector<std::string> words;
    std::vector<bool> is_space;
    std::string cur;
    bool in_esc = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char ch = line.at(i);
        if (ch == '\033') {
            in_esc = true;
            cur += ch;
        } else if (in_esc) {
            cur += ch;
            if (ch >= 0x40 && ch <= 0x7E && ch != '[') in_esc = false;
            if (!in_esc && cur.size() >= 2 && cur.at(1) == '[') {
                // wait for terminator already consumed
            }
            // CSI: wait for final byte
            if (ch >= 0x40 && ch <= 0x7E) in_esc = false;
        } else if (ch == ' ') {
            if (!cur.empty()) {
                words.push_back(cur);
                is_space.push_back(false);
                cur.clear();
            }
            words.push_back(" ");
            is_space.push_back(true);
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) {
        words.push_back(cur);
        is_space.push_back(false);
    }

    std::vector<std::string> result;
    std::string current_line;
    int current_w = 0;
    std::string const indent(static_cast<std::size_t>(cont_indent), ' ');

    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words.at(i);
        if (is_space.at(i)) {
            // space: only emit if we have content on current line
            if (current_w > 0) {
                current_line += ' ';
                current_w += 1;
            }
            continue;
        }
        int ww = vis_width(w);
        if (current_w + ww > max_w && current_w > 0) {
            // flush current line, start new continuation
            result.push_back(current_line);
            current_line = indent;
            current_w = cont_indent;
        }
        current_line += w;
        current_w += ww;
    }
    if (!current_line.empty()) result.push_back(current_line);
    if (result.empty()) result.push_back("");
    return result;
}

struct TopicData {
    const char* title;
    const char* subtitle;
    std::vector<std::string> lines;
};

auto get_topic_data(int index) -> TopicData {
    switch (index) {
        case 0:  // Registers & ABI
            return {
                .title = "1. RISC-V Registers & ABI Conventions",
                .subtitle = "Standard Calling Conventions and Architectural State",
                .lines = {
                    "\033[1;36m── General Purpose Registers (GPRs: x0..x31) ──\033[0m",
                    "• \033[1;33mx0 (zero)\033[0m : Hardwired to 0; writes are discarded.",
                    "• \033[1;33mx1 (ra)\033[0m   : Return address for function calls "
                    "(Caller-saved).",
                    "• \033[1;33mx2 (sp)\033[0m   : Stack pointer; 16-byte aligned in standard ABI "
                    "(Callee-saved).",
                    "• \033[1;33mx3 (gp)\033[0m   : Global pointer for fast small-data access.",
                    "• \033[1;33mx4 (tp)\033[0m   : Thread pointer (thread-local storage TLS).",
                    "• \033[1;33mx5..x7 (t0..t2)\033[0m   : Temporary registers (Caller-saved).",
                    "• \033[1;33mx8 (s0/fp)\033[0m      : Saved register or Frame pointer "
                    "(Callee-saved).",
                    "• \033[1;33mx9 (s1)\033[0m         : Saved register (Callee-saved).",
                    "• \033[1;33mx10..x17 (a0..a7)\033[0m : Function arguments and return values "
                    "(a0, a1) (Caller-saved).",
                    "• \033[1;33mx18..x27 (s2..s11)\033[0m: Saved registers (Callee-saved, "
                    "preserved across calls).",
                    "• \033[1;33mx28..x31 (t3..t6)\033[0m : Temporaries (Caller-saved).",
                    "",
                    "\033[1;36m── Floating-Point Registers (FPRs: f0..f31) ──\033[0m",
                    "• \033[1;33mfa0..fa7 (f10..f17)\033[0m: FP Arguments and return values.",
                    "• \033[1;33mfs0..fs11\033[0m          : FP Saved registers (Callee-saved).",
                    "• \033[1;33mft0..ft11\033[0m          : FP Temporaries (Caller-saved).",
                    "",
                    "\033[1;36m── Vector Registers (v0..v31) ──\033[0m",
                    "• Scalable register size governed by \033[1;32mVLEN\033[0m (e.g. 128, 256 "
                    "bits).",
                    "• Element width governed by \033[1;32mSEW\033[0m and multiplier "
                    "\033[1;32mLMUL\033[0m configured by \033[1;33mvsetvli\033[0m."}};
        case 1:  // Pipeline & Hazards
            return {.title = "2. Pipeline Stages & Hazards",
                    .subtitle = "Instruction Pipelining, Dataflow, Forwarding, and Stalls",
                    .lines = {"\033[1;36m── Classic 5-Stage RISC-V Pipeline ──\033[0m",
                              "• \033[1;32m[IF]  Instruction Fetch\033[0m : Fetch 32-bit/16-bit "
                              "instruction from memory/I-Cache at PC.",
                              "• \033[1;32m[ID]  Instruction Decode\033[0m: Decode "
                              "opcode/bitfields, read register file (rs1, rs2).",
                              "• \033[1;32m[EX]  Execute / ALU\033[0m     : Perform "
                              "arithmetic/logic computation, branch target evaluation.",
                              "• \033[1;32m[MEM] Memory Access\033[0m    : Read (Load) or write "
                              "(Store) DRAM/D-Cache at calculated address.",
                              "• \033[1;32m[WB]  Write-Back\033[0m       : Write resulting value "
                              "into destination register rd.",
                              "", "\033[1;36m── Pipeline Hazards & Mitigations ──\033[0m",
                              "• \033[1;31mRAW (Read-After-Write)\033[0m : Instruction needs "
                              "result before previous instruction writes back.",
                              "  \033[1;32m→ Forwarding (Bypassing)\033[0m: Route ALU output "
                              "directly from EX/MEM stage to ID/EX stage.",
                              "  \033[1;33m→ Load-Use Stall\033[0m        : Load data is only "
                              "ready at end of MEM stage, requiring 1 bubble.",
                              "• \033[1;31mControl Hazard\033[0m         : Branch target unknown "
                              "during IF stage.",
                              "  \033[1;32m→ Branch Prediction\033[0m     : Speculatively fetch "
                              "next instruction; flush pipeline on mispredict.",
                              "• \033[1;31mStructural Hazard\033[0m      : Hardware resource "
                              "contention (e.g., single-port memory or busy divider)."}};
        case 2:  // Memory Hierarchy & Caches
            return {
                .title = "3. Memory Hierarchy & Caches",
                .subtitle = "L1 Caches, Multi-Way Set Associativity, Line Replacement",
                .lines = {"\033[1;36m── Cache Architecture & Address Breakdown ──\033[0m",
                          "• Physical address is split into: \033[1;33m[ Tag | Set Index | Block "
                          "Offset ]\033[0m.",
                          "  - \033[1;32mOffset\033[0m: Selects byte/word within cache block (e.g. "
                          "64-byte line = 6 bits).",
                          "  - \033[1;32mIndex\033[0m : Selects cache set (e.g. 16 sets = 4 bits).",
                          "  - \033[1;32mTag\033[0m   : Remaining upper bits verified against line "
                          "tags in the selected set.",
                          "", "\033[1;36m── Associativity & Hit/Miss Dynamics ──\033[0m",
                          "• \033[1;33mDirect-Mapped (1-Way)\033[0m: Exactly 1 line per set. Fast, "
                          "but high conflict misses.",
                          "• \033[1;33mN-Way Set-Associative\033[0m: N lines per set. Line "
                          "replaced using \033[1;32mLRU\033[0m (Least Recently Used).",
                          "• \033[1;33mFully Associative\033[0m    : Any block can go into any "
                          "line (used in TLBs).",
                          "", "\033[1;36m── Write Policies ──\033[0m",
                          "• \033[1;32mWrite-Back (with Dirty bit)\033[0m: Stores update cache "
                          "line; written to DRAM only on eviction.",
                          "• \033[1;32mWrite-Through\033[0m              : Stores update cache and "
                          "backing DRAM simultaneously."}};
        case 3:  // Virtual Memory & TLB
            return {
                .title = "4. Virtual Memory & Page Translation",
                .subtitle = "Sv39/Sv48 Paging, Translation Lookaside Buffer (TLB), Faults",
                .lines = {"\033[1;36m── RISC-V Sv39 Virtual Memory Architecture ──\033[0m",
                          "• 39-bit Virtual Address → 3-level page table hierarchy (VPN[2], "
                          "VPN[1], VPN[0]).",
                          "• Page size is \033[1;32m4 KiB (12-bit offset)\033[0m; Mega-pages (2 "
                          "MiB) and Giga-pages (1 GiB) supported.",
                          "• \033[1;33msatp (Supervisor Address Translation and Protection)\033[0m "
                          "CSR holds root page table PPN.",
                          "", "\033[1;36m── TLB (Translation Lookaside Buffer) ──\033[0m",
                          "• High-speed associative cache holding recent \033[1;32mVirtual Page → "
                          "Physical Page\033[0m mappings.",
                          "• \033[1;32mTLB Hit\033[0m : Instant single-cycle address translation.",
                          "• \033[1;31mTLB Miss\033[0m: Requires multi-cycle hardware Page Table "
                          "Walk (PTW).",
                          "", "\033[1;36m── Page Table Entry (PTE) Permission Bits ──\033[0m",
                          "• \033[1;33mV\033[0m: Valid, \033[1;33mR\033[0m: Readable, "
                          "\033[1;33mW\033[0m: Writable, \033[1;33mX\033[0m: Executable, "
                          "\033[1;33mU\033[0m: User accessible.",
                          "• \033[1;33mA\033[0m: Accessed (set on read/write), \033[1;33mD\033[0m: "
                          "Dirty (set on write access)."}};
        case 4:  // Branch Prediction
            return {
                .title = "5. Branch Prediction Mechanics",
                .subtitle =
                    "Dynamic Branch History, Direction Predictors, and Branch Target Buffers",
                .lines = {
                    "\033[1;36m── Branch Predictor Architectures ──\033[0m",
                    "• \033[1;33mStatic Predictors\033[0m    : Always predict Taken (AT) or Always "
                    "predict Not-Taken (ANT).",
                    "• \033[1;33m1-Bit Dynamic\033[0m        : Remembers last outcome (0: "
                    "Not-Taken, 1: Taken).",
                    "• \033[1;33m2-Bit Bimodal Counter\033[0m: 4-state saturating state machine:",
                    "  - \033[1;32m00: Strongly Not-Taken\033[0m | \033[1;33m01: Weakly "
                    "Not-Taken\033[0m",
                    "  - \033[1;33m10: Weakly Taken\033[0m     | \033[1;32m11: Strongly "
                    "Taken\033[0m",
                    "  - Prevents single loop exit anomaly from thrashing the predictor state.",
                    "• \033[1;33mGshare Predictor\033[0m     : XORs Global Branch History Register "
                    "(GBHR) with branch PC bits.",
                    "", "\033[1;36m── BTB (Branch Target Buffer) ──\033[0m",
                    "• Cache storing predicted target jump addresses so the IF stage can redirect "
                    "instantly."}};
        case 5:  // Privilege Modes & Traps
        default:
            return {
                .title = "6. Privilege Modes, CSRs & Traps",
                .subtitle = "M/S/U Modes, Exceptions, Interrupts, and Control Registers",
                .lines = {
                    "\033[1;36m── RISC-V Privilege Hierarchy ──\033[0m",
                    "• \033[1;31mMachine Mode (M-mode)\033[0m    : Highest privilege level; direct "
                    "hardware access, OpenSBI.",
                    "• \033[1;34mSupervisor Mode (S-mode)\033[0m : OS Kernel level; virtual memory "
                    "management (Linux).",
                    "• \033[1;32mUser Mode (U-mode)\033[0m       : Unprivileged user applications.",
                    "", "\033[1;36m── Core Control & Status Registers (CSRs) ──\033[0m",
                    "• \033[1;33mmstatus / sstatus\033[0m: Global interrupt enable (MIE/SIE) and "
                    "previous privilege mode (MPP/SPP).",
                    "• \033[1;33mmepc / sepc\033[0m      : Saved Program Counter where "
                    "exception/interrupt occurred.",
                    "• \033[1;33mmcause / scause\033[0m  : Numeric reason code (MSB=1 for "
                    "Interrupt, MSB=0 for Exception).",
                    "• \033[1;33mmtvec / stvec\033[0m    : Base trap handler entry vector address "
                    "(Direct or Vectored).",
                    "• \033[1;33mmedeleg / mideleg\033[0m: M-mode masks delegating specific "
                    "exceptions/interrupts to S-mode."}};
    }
}

}  // namespace

void GlossaryModal::open(int& topic_idx, int& scroll_offset) {
    topic_idx = 0;
    scroll_offset = 0;
}

void GlossaryModal::move_topic(int& topic_idx, int& scroll_offset, int delta) {
    constexpr int kTotalTopics = 6;
    topic_idx = (topic_idx + delta + kTotalTopics) % kTotalTopics;
    scroll_offset = 0;
}

void GlossaryModal::scroll_content(int& scroll_offset, int delta, int max_lines) {
    scroll_offset = std::clamp(scroll_offset + delta, 0, std::max(0, max_lines - 10));
}

void GlossaryModal::render(std::vector<std::string>& content_rows,
                           const std::function<void(const std::string&)>& add_row_cb, int topic_idx,
                           int scroll_offset, int term_height, int box_w) {
    (void)content_rows;
    (void)term_height;

    const auto style = get_active_theme_style();
    const bool is_ansi = (style == TuiThemeStyle::ClassicAnsi);
    auto const& glyphs = get_theme_glyphs(style);

    constexpr std::array<const char*, 6> kTopicNames = {"1.Regs",   "2.Pipe",  "3.Cache",
                                                        "4.VM/TLB", "5.BPred", "6.Priv/Trap"};

    std::string nav_bar = " ";
    for (size_t i = 0; i < kTopicNames.size(); ++i) {
        if (i > 0) nav_bar += std::format("{}{}\033[0m", kThemeBorder, glyphs.vert);
        if (static_cast<int>(i) == topic_idx) {
            nav_bar += std::format("\033[1;7m {} \033[0m", kTopicNames.at(i));
        } else {
            nav_bar += std::format(" {}{}\033[0m ", kThemeMuted, kTopicNames.at(i));
        }
    }
    add_row_cb(nav_bar);
    int const div_len = std::max(10, box_w - 4);
    add_row_cb(
        std::format("{}{}\033[0m", kThemeBorder, make_repeated_string(glyphs.horiz, div_len)));

    TopicData const data = get_topic_data(topic_idx);
    add_row_cb(std::format(" \033[1m{}\033[0m", data.title));
    add_row_cb(std::format(" {}{}\033[0m", kThemeMuted, data.subtitle));
    add_row_cb("");

    int const total_lines = static_cast<int>(data.lines.size());
    int const start_line = std::min(scroll_offset, std::max(0, total_lines - 1));
    int const wrap_w = std::max(10, box_w - 4);  // available chars inside the modal border

    for (int i = start_line; i < total_lines; ++i) {
        std::string line = data.lines.at(static_cast<std::size_t>(i));
        if (is_ansi) {
            size_t p = 0;
            while ((p = line.find("•")) != std::string::npos)
                line.replace(p, std::string("•").length(), "*");
            while ((p = line.find("──")) != std::string::npos)
                line.replace(p, std::string("──").length(), "--");
            while ((p = line.find("→")) != std::string::npos)
                line.replace(p, std::string("→").length(), "->");
        }
        if (line.empty()) {
            add_row_cb("");
            continue;
        }
        // ANSI-aware word-wrap: continuation lines indent by 2 to align under bullet text.
        for (const auto& wrapped : wrap_ansi_line(line, wrap_w)) {
            add_row_cb(" " + wrapped);
        }
    }

    add_row_cb("");
    if (is_ansi) {
        add_row_cb(
            std::format("{}Navigation: \033[1m[</>/Tab/1-6]\033[0m Select Topic | "
                        "\033[1m[^/v]\033[0m Scroll | \033[1m[Esc/?/q]\033[0m Close\033[0m",
                        kThemeMuted));
    } else {
        add_row_cb(
            std::format("{}Navigation: \033[1m[←/→/Tab/1-6]\033[0m Select Topic │ "
                        "\033[1m[↑/↓]\033[0m Scroll │ \033[1m[Esc/?/q]\033[0m Close\033[0m",
                        kThemeMuted));
    }
}

}  // namespace simrv::tui::modals
