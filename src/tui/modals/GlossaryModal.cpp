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
#include "simrv/tui/modals/ModalComponents.hpp"

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
                    "## General-purpose registers (x0–x31)",
                    "• \033[1;33mx0 (zero)\033[0m : Hardwired to 0; writes are discarded.",
                    "• \033[1;33mx1 (ra)\033[0m   : Return address for function calls "
                    "(Caller-saved).",
                    "• \033[1;33mx2 (sp)\033[0m   : Stack pointer; kept 16-byte aligned at standard "
                    "ABI procedure-call boundaries.",
                    "• \033[1;33mx3 (gp)\033[0m   : Global pointer for fast small-data access.",
                    "• \033[1;33mx4 (tp)\033[0m   : Thread pointer for thread-local storage (TLS).",
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
                    "## Floating-point registers (f0–f31)",
                    "• \033[1;33mfa0..fa7 (f10..f17)\033[0m: FP Arguments and return values.",
                    "• \033[1;33mfs0..fs11\033[0m          : Conditionally callee-saved under the "
                    "hardware floating-point calling convention.",
                    "• \033[1;33mft0..ft11\033[0m          : FP Temporaries (Caller-saved).",
                    "",
                    "## Vector registers (v0–v31)",
                    "• Scalable register size governed by \033[1;32mVLEN\033[0m (e.g. 128, 256 "
                    "bits).",
                    "• Element width is selected by \033[1;32mSEW\033[0m; register grouping is "
                    "selected by \033[1;32mLMUL\033[0m through vector-configuration instructions."}};
        case 1:  // Pipeline & Hazards
            return {.title = "2. Pipeline Stages & Hazards",
                    .subtitle = "Instruction Pipelining, Dataflow, Forwarding, and Stalls",
                    .lines = {"## Classic five-stage pipeline",
                              "• \033[1;32mIF \033[0m · Fetch the instruction at PC from the "
                              "instruction cache or memory.",
                              "• \033[1;32mID \033[0m · Decode opcode and operands; read rs1 "
                              "and rs2 from the register file.",
                              "• \033[1;32mEX \033[0m · Execute arithmetic or logic; calculate "
                              "addresses and resolve branches.",
                              "• \033[1;32mMEM\033[0m · Access the data cache or memory for "
                              "loads and stores.",
                              "• \033[1;32mWB \033[0m · Write an instruction result to rd when "
                              "the instruction produces one.",
                              "",
                              "## Hazards and mitigations",
                              "• \033[1;31mRAW\033[0m · A consumer needs a value before its "
                              "producer reaches write-back.",
                              "  ◦ \033[1;32mForwarding\033[0m · Bypass a result from a later stage "
                              "directly to the dependent instruction.",
                              "  ◦ \033[1;33mLoad-use interlock\033[0m · A dependent instruction "
                              "typically waits one bubble because load data arrives after MEM.",
                              "• \033[1;31mControl\033[0m · The correct next PC is not yet known "
                              "after a branch or jump enters the pipeline.",
                              "  ◦ \033[1;32mPrediction\033[0m · Fetch speculatively; redirect and flush "
                              "younger work when the prediction is wrong.",
                              "• \033[1;31mStructural\033[0m · Concurrent instructions require the "
                              "same unavailable hardware resource.",
                              "  ◦ \033[1;32mArbitration or stalling\033[0m · Serialize access until "
                              "the resource becomes available."}};
        case 2:  // Memory Hierarchy & Caches
            return {
                .title = "3. Memory Hierarchy & Caches",
                .subtitle = "L1 Caches, Multi-Way Set Associativity, Line Replacement",
                .lines = {"## Cache address breakdown",
                          "• A cache address is divided into \033[1;33mTag · Set index · Block "
                          "offset\033[0m.",
                          "  - \033[1;32mOffset\033[0m: Selects byte/word within cache block (e.g. "
                          "64-byte line = 6 bits).",
                          "  - \033[1;32mIndex\033[0m : Selects cache set (e.g. 16 sets = 4 bits).",
                          "  - \033[1;32mTag\033[0m   : Remaining upper bits verified against line "
                          "tags in the selected set.",
                          "", "## Associativity and misses",
                          "• \033[1;33mDirect-Mapped (1-Way)\033[0m: Exactly 1 line per set. Fast, "
                          "but high conflict misses.",
                          "• \033[1;33mN-Way Set-Associative\033[0m: N lines per set. Line "
                          "replacement policy chooses a victim on a miss (often LRU or an approximation).",
                          "• \033[1;33mFully Associative\033[0m    : Any block can go into any "
                          "line (used in TLBs).",
                          "", "## Write policies",
                          "• \033[1;32mWrite-Back (with Dirty bit)\033[0m: Stores update cache "
                          "line; propagated to the next memory level when the dirty line is evicted.",
                          "• \033[1;32mWrite-Through\033[0m              : Stores update cache and "
                          "next memory level as part of the store."}};
        case 3:  // Virtual Memory & TLB
            return {
                .title = "4. Virtual Memory & Page Translation",
                .subtitle = "Sv39/Sv48 Paging, Translation Lookaside Buffer (TLB), Faults",
                .lines = {"## Sv39 virtual memory",
                          "• Sv39 uses 39 translated address bits in a sign-extended 64-bit virtual "
                          "address and a three-level page table (VPN[2:0]).",
                          "• Page size is \033[1;32m4 KiB (12-bit offset)\033[0m; Mega-pages (2 "
                          "MiB) and Giga-pages (1 GiB) supported.",
                          "• \033[1;33msatp (Supervisor Address Translation and Protection)\033[0m "
                          "CSR holds root page table PPN.",
                          "", "## Translation lookaside buffer (TLB)",
                          "• High-speed associative cache holding recent \033[1;32mVirtual Page → "
                          "Physical Page\033[0m mappings.",
                          "• \033[1;32mTLB hit\033[0m : Translation is supplied without a page-table walk.",
                          "• \033[1;31mTLB Miss\033[0m: Requires multi-cycle hardware Page Table "
                          "Walk (PTW).",
                          "", "## Page-table entry (PTE) bits",
                          "• \033[1;33mV\033[0m: Valid, \033[1;33mR\033[0m: Readable, "
                          "\033[1;33mW\033[0m: Writable, \033[1;33mX\033[0m: Executable, "
                          "\033[1;33mU\033[0m: User accessible.",
                          "• \033[1;33mA\033[0m: Accessed; \033[1;33mD\033[0m: Dirty. Depending on the "
                          "implemented scheme, hardware updates them or faults for software handling."}};
        case 4:  // Branch Prediction
            return {
                .title = "5. Branch Prediction Mechanics",
                .subtitle =
                    "Dynamic Branch History, Direction Predictors, and Branch Target Buffers",
                .lines = {
                    "## Direction prediction",
                    "• \033[1;33mStatic Predictors\033[0m    : Always predict Taken (AT) or Always "
                    "predict Not-Taken (ANT).",
                    "• \033[1;33m1-Bit Dynamic\033[0m        : Remembers last outcome (0: "
                    "Not-Taken, 1: Taken).",
                    "• \033[1;33m2-Bit Bimodal Counter\033[0m: 4-state saturating state machine:",
                    "  - \033[1;32m00: Strongly Not-Taken\033[0m · \033[1;33m01: Weakly "
                    "Not-Taken\033[0m",
                    "  - \033[1;33m10: Weakly Taken\033[0m · \033[1;32m11: Strongly "
                    "Taken\033[0m",
                    "  - Prevents single loop exit anomaly from thrashing the predictor state.",
                    "• \033[1;33mGshare Predictor\033[0m     : XORs Global Branch History Register "
                    "(GBHR) with branch PC bits.",
                    "", "## Branch target buffer (BTB)",
                    "• Cache of predicted control-flow targets that lets fetch redirect without "
                    "waiting for target calculation."}};
        case 5:  // Privilege Modes & Traps
        default:
            return {
                .title = "6. Privilege Modes, CSRs & Traps",
                .subtitle = "M/S/U Modes, Exceptions, Interrupts, and Control Registers",
                .lines = {
                    "## Privilege hierarchy",
                    "• \033[1;31mMachine Mode (M-mode)\033[0m    : Highest privilege level; direct "
                    "hardware access, OpenSBI.",
                    "• \033[1;34mSupervisor Mode (S-mode)\033[0m : OS Kernel level; virtual memory "
                    "management (Linux).",
                    "• \033[1;32mUser Mode (U-mode)\033[0m       : Unprivileged user applications.",
                    "", "## Core control and status registers (CSRs)",
                    "• \033[1;33mmstatus / sstatus\033[0m: Global interrupt enable (MIE/SIE) and "
                    "previous privilege mode (MPP/SPP).",
                    "• \033[1;33mmepc / sepc\033[0m      : Saved Program Counter where "
                    "exception/interrupt occurred.",
                    "• \033[1;33mmcause / scause\033[0m  : Numeric reason code (MSB=1 for "
                    "Interrupt, MSB=0 for Exception).",
                    "• \033[1;33mmtvec / stvec\033[0m    : Base trap handler entry vector address "
                    "(Direct or Vectored).",
                    "• \033[1;33mmedeleg / mideleg\033[0m: Bitmaps selecting exceptions and "
                    "interrupts delegated from M-mode to S-mode."}};
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

    const auto style = get_active_theme_style();
    const bool is_ansi = (style == TuiThemeStyle::ClassicAnsi);
    static constexpr std::array<std::string_view, 6> kTopicNames = {
        "Regs", "Pipe", "Cache", "VM/TLB", "Branch", "Priv"};

    add_row_cb(build_modal_tab_bar(kTopicNames, static_cast<std::size_t>(topic_idx)));

    TopicData const data = get_topic_data(topic_idx);
    add_row_cb(std::format(" \033[1m{}\033[0m", data.title));
    add_row_cb(std::format(" {}{}\033[0m", kThemeMuted, data.subtitle));
    add_row_cb("");

    int const wrap_w = std::max(10, box_w - 4);  // available chars inside the modal border
    std::vector<std::string> body_rows;
    auto add_body_row = [&](std::string row) { body_rows.push_back(std::move(row)); };

    for (std::string line : data.lines) {
        if (is_ansi) {
            size_t p = 0;
            while ((p = line.find("•")) != std::string::npos)
                line.replace(p, std::string("•").length(), "*");
            while ((p = line.find("◦")) != std::string::npos)
                line.replace(p, std::string("◦").length(), "-");
            while ((p = line.find("──")) != std::string::npos)
                line.replace(p, std::string("──").length(), "--");
            while ((p = line.find("→")) != std::string::npos)
                line.replace(p, std::string("→").length(), "->");
        }
        if (line.empty()) {
            add_body_row("");
            continue;
        }
        if (line.starts_with("## ")) {
            add_body_row(build_section_divider(std::string_view(line).substr(3), kThemeSky));
            continue;
        }
        int const continuation_indent = line.starts_with("  ") ? 6 : (line.starts_with("•") ? 4 : 2);
        for (const auto& wrapped : wrap_ansi_line(line, wrap_w, continuation_indent)) {
            add_body_row(" " + wrapped);
        }
    }

    // Keep modal geometry independent of scrolling. Tabs/title/subtitle and the footer consume six
    // content rows; the remaining rows form a stable body viewport. Previously, dropping rows
    // before modal measurement made Up/Down visibly resize the box.
    int const body_capacity = std::max(1, term_height - 12);
    int const viewport_rows = std::min(static_cast<int>(body_rows.size()), body_capacity);
    int const max_scroll = std::max(0, static_cast<int>(body_rows.size()) - viewport_rows);
    int const start_row = std::clamp(scroll_offset, 0, max_scroll);
    for (int row = 0; row < viewport_rows; ++row) {
        int const source_row = start_row + row;
        add_row_cb(source_row < static_cast<int>(body_rows.size())
                       ? body_rows.at(static_cast<std::size_t>(source_row))
                       : std::string{});
    }

    add_row_cb("");
    bool const scrollable = static_cast<int>(body_rows.size()) > viewport_rows;
    if (scrollable) {
        add_row_cb(build_modal_footer(
            is_ansi ? std::initializer_list<ModalActionHint>{{"<", "Previous"},
                                                             {">", "Next"},
                                                             {"^", "Up"},
                                                             {"v", "Down"},
                                                             {"Esc/?/q", "Close"}}
                    : std::initializer_list<ModalActionHint>{{"←", "Previous"},
                                                             {"→", "Next"},
                                                             {"↑", "Up"},
                                                             {"↓", "Down"},
                                                             {"Esc/?/q", "Close"}}));
    } else {
        add_row_cb(build_modal_footer(
            is_ansi ? std::initializer_list<ModalActionHint>{{"<", "Previous"},
                                                             {">", "Next"},
                                                             {"Esc/?/q", "Close"}}
                    : std::initializer_list<ModalActionHint>{{"←", "Previous"},
                                                             {"→", "Next"},
                                                             {"Esc/?/q", "Close"}}));
    }
}

}  // namespace simrv::tui::modals
