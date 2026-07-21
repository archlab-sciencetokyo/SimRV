/**
 * @file TuiTheme.cpp
 * @brief Implements theme handling logic and palette configurations.
 */
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/VirtualTerminal.hpp"

namespace simrv::tui {

const char* g_theme_border = kAdaptiveBorder; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_text   = kAdaptiveText; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_val    = kAdaptiveVal; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_muted  = kAdaptiveMuted; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_mint   = kAdaptiveMint; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_peach  = kAdaptivePeach; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_coral  = kAdaptiveCoral; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_sky    = kAdaptiveSky; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_pink   = kAdaptivePink; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_modal_bg = "\033[48;5;236m"; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

std::array<const char*, 16> g_theme_palette = kHighContrastPalette; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::array<const char*, 16> g_theme_bg_palette = kHighContrastBgPalette; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

bool g_high_contrast = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
TuiTheme g_tui_theme = TuiTheme::Adaptive; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

auto set_tui_theme(TuiTheme theme) -> void {
    g_tui_theme = theme;
    switch (theme) {
        case TuiTheme::HighContrast:
            g_high_contrast = true;
            g_theme_border   = kContrastBorder;
            g_theme_text     = kContrastText;
            g_theme_val      = kContrastVal;
            g_theme_muted    = kContrastMuted;
            g_theme_mint     = kContrastMint;
            g_theme_peach    = kContrastPeach;
            g_theme_coral    = kContrastCoral;
            g_theme_sky      = kContrastSky;
            g_theme_pink     = kContrastPink;
            g_theme_modal_bg = "\033[40m";
            g_theme_palette  = kHighContrastPalette;
            g_theme_bg_palette = kHighContrastBgPalette;
            break;
        case TuiTheme::Adaptive:
            g_high_contrast = false;
            g_theme_border   = kAdaptiveBorder;
            g_theme_text     = kAdaptiveText;
            g_theme_val      = kAdaptiveVal;
            g_theme_muted    = kAdaptiveMuted;
            g_theme_mint     = kAdaptiveMint;
            g_theme_peach    = kAdaptivePeach;
            g_theme_coral    = kAdaptiveCoral;
            g_theme_sky      = kAdaptiveSky;
            g_theme_pink     = kAdaptivePink;
            g_theme_modal_bg = "\033[48;5;236m";
            g_theme_palette  = kHighContrastPalette;
            g_theme_bg_palette = kHighContrastBgPalette;
            break;
        case TuiTheme::Sakura:
        default:
            g_high_contrast = false;
            g_theme_border   = kSakuraBorderConst;
            g_theme_text     = kSakuraTextConst;
            g_theme_val      = kSakuraValConst;
            g_theme_muted    = kSakuraMutedConst;
            g_theme_mint     = kSakuraMintConst;
            g_theme_peach    = kSakuraPeachConst;
            g_theme_coral    = kSakuraCoralConst;
            g_theme_sky      = kSakuraSkyConst;
            g_theme_pink     = kSakuraPinkConst;
            g_theme_modal_bg = "\033[48;5;53m";
            g_theme_palette  = kSakuraPalette;
            g_theme_bg_palette = kSakuraBgPalette;
            break;
    }
}

auto get_tui_theme() -> TuiTheme {
    return g_tui_theme;
}

auto set_high_contrast(bool enable) -> void {
    if (enable) {
        set_tui_theme(TuiTheme::HighContrast);
    } else {
        set_tui_theme(TuiTheme::Adaptive);
    }
}

auto is_high_contrast() -> bool { return g_high_contrast; }

auto make_repeated_string(const std::string& pattern, int count) -> std::string {
    std::string s;
    for (int i = 0; i < count; ++i) {
        s += pattern;
    }
    return s;
}

auto get_display_width(const std::string& s) -> int {
    int len = 0;
    bool in_esc = false;
    bool in_csi = false;
    for (std::size_t i = 0; i < s.length(); ++i) {
        if (s.at(i) == '\033') {
            in_esc = true;
            in_csi = false;
        } else if (in_esc) {
            if (s.at(i) == '[') {
                in_csi = true;
            } else if (in_csi) {
                if (s.at(i) >= 0x40 && s.at(i) <= 0x7E) {
                    in_esc = false;
                    in_csi = false;
                }
            } else {
                if (s.at(i) == '(' || s.at(i) == ')') {
                    // keep in_esc
                } else {
                    in_esc = false;
                }
            }
        } else {
            const auto c = static_cast<unsigned char>(s.at(i));
            if (c < 0x80 || c >= 0xC0) {
                len++;
            }
        }
    }
    return len;
}

auto format_to_width(const std::string& colored_str, int target_width) -> std::string {
    int current_width = 0;
    std::string result;
    bool in_esc = false;
    bool in_csi = false;
    bool skipping = false;

    for (std::size_t i = 0; i < colored_str.length(); ++i) {
        if (colored_str.at(i) == '\033') {
            in_esc = true;
            in_csi = false;
            result += colored_str.at(i);
        } else if (in_esc) {
            result += colored_str.at(i);
            if (colored_str.at(i) == '[') {
                in_csi = true;
            } else if (in_csi) {
                if (colored_str.at(i) >= 0x40 && colored_str.at(i) <= 0x7E) {
                    in_esc = false;
                    in_csi = false;
                }
            } else {
                if (colored_str.at(i) == '(' || colored_str.at(i) == ')') {
                    // keep in_esc
                } else {
                    in_esc = false;
                }
            }
        } else {
            const auto c = static_cast<unsigned char>(colored_str.at(i));
            bool is_lead = (c < 0x80 || c >= 0xC0);
            if (is_lead) {
                if (current_width >= target_width) {
                    skipping = true;
                } else {
                    skipping = false;
                    current_width++;
                }
            }
            if (!skipping) {
                result += colored_str.at(i);
            }
        }
    }

    if (current_width < target_width) {
        result += std::string(static_cast<std::size_t>(target_width - current_width), ' ');
    }

    return result + "\033[0m";
}

auto overlay_string(const std::string& base_line, const std::string& overlay_line, int start_x, int box_w) -> std::string {
    std::string result;
    int col = 0;
    bool in_esc = false;
    bool in_csi = false;
    std::string current_esc;
    std::string active_style;

    std::size_t i = 0;
    while (i < base_line.length() && col < start_x) {
        char ch = base_line[i];
        if (ch == '\033') {
            in_esc = true;
            in_csi = false;
            current_esc.clear();
            current_esc += ch;
            result += ch;
            i++;
        } else if (in_esc) {
            current_esc += ch;
            result += ch;
            if (ch == '[') {
                in_csi = true;
            } else if (in_csi) {
                if (ch >= 0x40 && ch <= 0x7E) {
                    in_esc = false;
                    in_csi = false;
                    if (current_esc.back() == 'm') {
                        if (current_esc == "\033[0m") {
                            active_style.clear();
                        } else {
                            active_style += current_esc;
                        }
                    }
                }
            } else {
                if (ch != '(' && ch != ')') {
                    in_esc = false;
                }
            }
            i++;
        } else {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 0x80 || c >= 0xC0) {
                col++;
            }
            result += ch;
            i++;
        }
    }

    if (col < start_x) {
        result += std::string(static_cast<std::size_t>(start_x - col), ' ');
    }

    result += overlay_line;

    int target_end = start_x + box_w;
    while (i < base_line.length() && col < target_end) {
        char ch = base_line[i];
        if (ch == '\033') {
            in_esc = true;
            in_csi = false;
            current_esc.clear();
            i++;
        } else if (in_esc) {
            current_esc += ch;
            if (ch == '[') {
                in_csi = true;
            } else if (in_csi) {
                if (ch >= 0x40 && ch <= 0x7E) {
                    in_esc = false;
                    in_csi = false;
                    if (current_esc.back() == 'm') {
                        if (current_esc == "\033[0m") {
                            active_style.clear();
                        } else {
                            active_style += current_esc;
                        }
                    }
                }
            } else {
                if (ch != '(' && ch != ')') {
                    in_esc = false;
                }
            }
            i++;
        } else {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 0x80 || c >= 0xC0) {
                col++;
            }
            i++;
        }
    }

    if (!active_style.empty()) {
        result += active_style;
    }
    while (i < base_line.length()) {
        result += base_line[i];
        i++;
    }

    return result;
}

}  // namespace simrv::tui
