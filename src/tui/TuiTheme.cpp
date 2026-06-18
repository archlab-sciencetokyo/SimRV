/**
 * @file TuiTheme.cpp
 * @brief Implements theme handling logic and palette configurations.
 */
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/VirtualTerminal.hpp"

namespace simrv::tui {

const char* g_theme_border = kAdaptiveBorder;
const char* g_theme_text   = kAdaptiveText;
const char* g_theme_val    = kAdaptiveVal;
const char* g_theme_muted  = kAdaptiveMuted;
const char* g_theme_mint   = kAdaptiveMint;
const char* g_theme_peach  = kAdaptivePeach;
const char* g_theme_coral  = kAdaptiveCoral;
const char* g_theme_sky    = kAdaptiveSky;
const char* g_theme_pink   = kAdaptivePink;

std::array<const char*, 16> g_theme_palette = kHighContrastPalette;
std::array<const char*, 16> g_theme_bg_palette = kHighContrastBgPalette;

bool g_high_contrast = false;
TuiTheme g_tui_theme = TuiTheme::Adaptive;

void set_tui_theme(TuiTheme theme) {
    g_tui_theme = theme;
    if (theme == TuiTheme::HighContrast) {
        g_high_contrast = true;
        g_theme_border  = kContrastBorder;
        g_theme_text    = kContrastText;
        g_theme_val     = kContrastVal;
        g_theme_muted   = kContrastMuted;
        g_theme_mint    = kContrastMint;
        g_theme_peach   = kContrastPeach;
        g_theme_coral   = kContrastCoral;
        g_theme_sky     = kContrastSky;
        g_theme_pink    = kContrastPink;
        g_theme_palette = kHighContrastPalette;
        g_theme_bg_palette = kHighContrastBgPalette;
    } else if (theme == TuiTheme::Adaptive) {
        g_high_contrast = false;
        g_theme_border  = kAdaptiveBorder;
        g_theme_text    = kAdaptiveText;
        g_theme_val     = kAdaptiveVal;
        g_theme_muted   = kAdaptiveMuted;
        g_theme_mint    = kAdaptiveMint;
        g_theme_peach   = kAdaptivePeach;
        g_theme_coral   = kAdaptiveCoral;
        g_theme_sky     = kAdaptiveSky;
        g_theme_pink    = kAdaptivePink;
        g_theme_palette = kHighContrastPalette;
        g_theme_bg_palette = kHighContrastBgPalette;
    } else { // TuiTheme::Sakura
        g_high_contrast = false;
        g_theme_border  = kSakuraBorderConst;
        g_theme_text    = kSakuraTextConst;
        g_theme_val     = kSakuraValConst;
        g_theme_muted   = kSakuraMutedConst;
        g_theme_mint    = kSakuraMintConst;
        g_theme_peach   = kSakuraPeachConst;
        g_theme_coral   = kSakuraCoralConst;
        g_theme_sky     = kSakuraSkyConst;
        g_theme_pink    = kSakuraPinkConst;
        g_theme_palette = kSakuraPalette;
        g_theme_bg_palette = kSakuraBgPalette;
    }
}

TuiTheme get_tui_theme() {
    return g_tui_theme;
}

void set_high_contrast(bool enable) {
    if (enable) {
        set_tui_theme(TuiTheme::HighContrast);
    } else {
        set_tui_theme(TuiTheme::Adaptive);
    }
}

bool is_high_contrast() { return g_high_contrast; }

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

}  // namespace simrv::tui
