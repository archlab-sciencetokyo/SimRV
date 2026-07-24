/**
 * @file MisaModal.cpp
 * @brief Implementation of MISA extension configuration modal dialog.
 */
#include "simrv/tui/modals/MisaModal.hpp"

#include <array>
#include <format>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/xlen/Helpers.hpp"

namespace simrv::tui::modals {

namespace {

struct SettingItem {
    const char* key;
    const char* name;
    std::string val;
};

}  // namespace

void MisaModal::open(MisaDraft& draft, int& cursor, const simrv::core::Machine& machine) {
    cursor = 0;
    const uint64_t misa = machine.cpu.state().misa;
    draft.xlen_bits = static_cast<uint8_t>(machine.cpu.state().regs.xlen);
    draft.ext_a = (misa & (1ULL << ('a' - 'a'))) != 0;
    draft.ext_b = (misa & (1ULL << ('b' - 'a'))) != 0;
    draft.ext_c = (misa & (1ULL << ('c' - 'a'))) != 0;
    draft.ext_d = (misa & (1ULL << ('d' - 'a'))) != 0;
    draft.ext_f = (misa & (1ULL << ('f' - 'a'))) != 0;
    draft.ext_i = (misa & (1ULL << ('i' - 'a'))) != 0;
    draft.ext_m = (misa & (1ULL << ('m' - 'a'))) != 0;
    draft.ext_s = (misa & (1ULL << ('s' - 'a'))) != 0;
    draft.ext_u = (misa & (1ULL << ('u' - 'a'))) != 0;
    draft.ext_v = (misa & (1ULL << ('v' - 'a'))) != 0;
}

void MisaModal::move_cursor(int& cursor, int delta) {
    constexpr int kNumMisaItems = 13;
    cursor = (cursor + delta + kNumMisaItems) % kNumMisaItems;
}

void MisaModal::apply_profile(MisaDraft& draft, int profile_idx) {
    switch (profile_idx) {
        case 0:  // Base (I)
            draft.ext_a = false;
            draft.ext_b = false;
            draft.ext_c = false;
            draft.ext_d = false;
            draft.ext_f = false;
            draft.ext_i = true;
            draft.ext_m = false;
            draft.ext_s = false;
            draft.ext_u = false;
            draft.ext_v = false;
            break;
        case 1:  // IMAC
            draft.ext_a = true;
            draft.ext_b = false;
            draft.ext_c = true;
            draft.ext_d = false;
            draft.ext_f = false;
            draft.ext_i = true;
            draft.ext_m = true;
            draft.ext_s = true;
            draft.ext_u = true;
            draft.ext_v = false;
            break;
        case 2:  // GC (General)
            draft.ext_a = true;
            draft.ext_b = false;
            draft.ext_c = true;
            draft.ext_d = true;
            draft.ext_f = true;
            draft.ext_i = true;
            draft.ext_m = true;
            draft.ext_s = true;
            draft.ext_u = true;
            draft.ext_v = false;
            break;
        default:
            break;
    }
}

void MisaModal::toggle_item(MisaDraft& draft, int index) {
    switch (index) {
        case 0:
            draft.xlen_bits = (draft.xlen_bits == 32) ? 64 : 32;
            break;
        case 1: {
            bool curr = (draft.ext_s || draft.ext_u);
            draft.ext_s = !curr;
            draft.ext_u = !curr;
            break;
        }
        case 2:
            draft.ext_a = !draft.ext_a;
            break;
        case 3:
            draft.ext_b = !draft.ext_b;
            break;
        case 4:
            draft.ext_c = !draft.ext_c;
            break;
        case 5:
            draft.ext_d = !draft.ext_d;
            break;
        case 6:
            draft.ext_f = !draft.ext_f;
            break;
        case 7:
            draft.ext_i = !draft.ext_i;
            break;
        case 8:
            draft.ext_m = !draft.ext_m;
            break;
        case 9:
            draft.ext_v = !draft.ext_v;
            break;
        case 10:
            apply_profile(draft, 0);
            break;
        case 11:
            apply_profile(draft, 1);
            break;
        case 12:
            apply_profile(draft, 2);
            break;
        default:
            break;
    }
}

auto MisaModal::submit(const MisaDraft& draft, simrv::core::Machine& machine,
                       const std::function<void(const std::string&)>& set_status_override_cb)
    -> bool {
    uint64_t new_misa = draft.to_misa_val();
    machine.cpu.state().misa = new_misa;
    machine.s_misa_profile = new_misa;
    machine.s_misa_override = true;
    machine.s_misa_xlen = draft.xlen_bits;
    machine.cpu.state().update_xlen();

    machine.request_reboot();

    std::string misa_str = draft.to_misa_string();
    set_status_override_cb(
        std::format("Updated MISA CSR: {} (0x{:016x}) — reloading simulator", misa_str, new_misa));
    return true;
}

void MisaModal::render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb,
                       const MisaDraft& draft, int cursor, const simrv::core::Machine& machine) {
    (void)content_rows;
    std::string live_misa = simrv::xlen::resolve_misa_string(machine.cpu.state().misa);
    std::string draft_misa = draft.to_misa_string();

    add_row_cb(std::format("{}Current Live MISA  : \033[1;36m{}\033[0m", kThemeText, live_misa));
    add_row_cb(
        std::format("{}Draft Preview MISA : \033[1;33m{}\033[0m  \033[90m(Changes applied "
                    "ONLY on submit)\033[0m",
                    kThemeText, draft_misa));
    add_row_cb("");

    const auto misa_items = std::to_array<SettingItem>({
        {.key = " 0",
         .name = "Base XLEN Mode",
         .val = (draft.xlen_bits == 32) ? "\033[1;33m[RV32]\033[0m" : "\033[1;36m[RV64]\033[0m"},
        {.key = " 8",
         .name = "Privilege Modes (S & U)",
         .val = (draft.ext_s && draft.ext_u) ? "\033[1;32m[ON (S+U)]\033[0m"
                                             : "\033[90m[OFF (M-Only)]\033[0m"},
        {.key = " 1",
         .name = "Extension A (Atomic)",
         .val = draft.ext_a ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {.key = " 2",
         .name = "Extension B (Bitmanip)",
         .val = draft.ext_b ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {.key = " 3",
         .name = "Extension C (Compressed)",
         .val = draft.ext_c ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {.key = " 4",
         .name = "Extension D (Double FP)",
         .val = draft.ext_d ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {.key = " 5",
         .name = "Extension F (Single FP)",
         .val = draft.ext_f ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {.key = " 6",
         .name = "Extension I (Base Int)",
         .val = draft.ext_i ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {.key = " 7",
         .name = "Extension M (Mul/Div)",
         .val = draft.ext_m ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {.key = " a",
         .name = "Extension V (Vector)",
         .val = draft.ext_v ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {.key = " p", .name = "Preset: Base (I)", .val = "\033[1;34m[Set Profile: Base]\033[0m"},
        {.key = " i", .name = "Preset: IMAC", .val = "\033[1;34m[Set Profile: IMAC]\033[0m"},
        {.key = " g", .name = "Preset: GC (General)", .val = "\033[1;34m[Set Profile: GC]\033[0m"},
    });

    for (std::size_t i = 0; i < misa_items.size(); ++i) {
        if (i == 0) {
            add_row_cb(std::format("{}\033[1;35m── Base Architecture & Privilege Modes ──\033[0m",
                                   kThemeText));
        } else if (i == 2) {
            add_row_cb("");
            add_row_cb(std::format("{}\033[1;35m── Standard ISA Extensions ──\033[0m", kThemeText));
        } else if (i == 10) {
            add_row_cb("");
            add_row_cb(std::format("{}\033[1;35m── Quick Presets ──\033[0m", kThemeText));
        }

        bool is_sel = (static_cast<int>(i) == cursor);
        std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
        std::string num_key =
            std::format("{}[{}]\033[0m", is_sel ? kThemeMint : kThemeSky, misa_items[i].key);
        std::string name_str =
            std::format("{}{:<27}\033[0m", is_sel ? "\033[1;37m" : kThemeText, misa_items[i].name);
        add_row_cb(std::format("{}{} {} : {}", prefix, num_key, name_str, misa_items[i].val));
    }
    add_row_cb("");
    add_row_cb(
        std::format("{}Press \033[1m[Enter]\033[0m to apply MISA CSR, \033[1m[Esc]\033[0m "
                    "or \033[1m[q]\033[0m to cancel\033[0m",
                    kThemeMuted));
}

}  // namespace simrv::tui::modals
