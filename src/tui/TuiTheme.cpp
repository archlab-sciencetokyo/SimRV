/**
 * @file TuiTheme.cpp
 * @brief Implements theme handling logic and palette configurations.
 */
#include "simrv/tui/TuiTheme.hpp"

#include "simrv/tui/VirtualTerminal.hpp"

namespace simrv::tui {

const char* g_theme_border =
    kAdaptiveBorder;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_text =
    kAdaptiveText;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_val =
    kAdaptiveVal;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_muted =
    kAdaptiveMuted;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_mint =
    kAdaptiveMint;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_peach =
    kAdaptivePeach;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_coral =
    kAdaptiveCoral;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_sky =
    kAdaptiveSky;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_pink =
    kAdaptivePink;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const char* g_theme_modal_bg =
    "\033[48;5;236m";  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

std::array<const char*, 16> g_theme_palette =
    kHighContrastPalette;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::array<const char*, 16> g_theme_bg_palette =
    kHighContrastBgPalette;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

bool g_high_contrast = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
TuiTheme g_tui_theme =
    TuiTheme::Adaptive;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

auto set_tui_theme(TuiTheme theme) -> void {
    g_tui_theme = theme;
    switch (theme) {
        case TuiTheme::HighContrast:
            g_high_contrast = true;
            g_theme_border = kContrastBorder;
            g_theme_text = kContrastText;
            g_theme_val = kContrastVal;
            g_theme_muted = kContrastMuted;
            g_theme_mint = kContrastMint;
            g_theme_peach = kContrastPeach;
            g_theme_coral = kContrastCoral;
            g_theme_sky = kContrastSky;
            g_theme_pink = kContrastPink;
            g_theme_modal_bg = "\033[40m";
            g_theme_palette = kHighContrastPalette;
            g_theme_bg_palette = kHighContrastBgPalette;
            break;
        case TuiTheme::Adaptive:
            g_high_contrast = false;
            g_theme_border = kAdaptiveBorder;
            g_theme_text = kAdaptiveText;
            g_theme_val = kAdaptiveVal;
            g_theme_muted = kAdaptiveMuted;
            g_theme_mint = kAdaptiveMint;
            g_theme_peach = kAdaptivePeach;
            g_theme_coral = kAdaptiveCoral;
            g_theme_sky = kAdaptiveSky;
            g_theme_pink = kAdaptivePink;
            g_theme_modal_bg = "\033[48;5;236m";
            g_theme_palette = kHighContrastPalette;
            g_theme_bg_palette = kHighContrastBgPalette;
            break;
        case TuiTheme::Sakura:
        default:
            g_high_contrast = false;
            g_theme_border = kSakuraBorderConst;
            g_theme_text = kSakuraTextConst;
            g_theme_val = kSakuraValConst;
            g_theme_muted = kSakuraMutedConst;
            g_theme_mint = kSakuraMintConst;
            g_theme_peach = kSakuraPeachConst;
            g_theme_coral = kSakuraCoralConst;
            g_theme_sky = kSakuraSkyConst;
            g_theme_pink = kSakuraPinkConst;
            g_theme_modal_bg = "\033[48;5;235m";
            g_theme_palette = kSakuraPalette;
            g_theme_bg_palette = kSakuraBgPalette;
            break;
    }
}

auto get_tui_theme() -> TuiTheme { return g_tui_theme; }

auto set_high_contrast(bool enable) -> void {
    if (enable) {
        set_tui_theme(TuiTheme::HighContrast);
    } else if (g_tui_theme == TuiTheme::HighContrast) {
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

static auto get_esc_seq_len(const std::string& str, std::size_t start) -> std::size_t {
    if (start >= str.length() || str[start] != '\033') return 0;
    std::size_t i = start + 1;
    if (i >= str.length()) return 1;

    char c = str[i];
    if (c == '[') {
        i++;
        while (i < str.length()) {
            char ch = str[i++];
            if (ch >= 0x40 && ch <= 0x7E) break;
        }
        return i - start;
    }
    if (c == 'P' || c == ']' || c == '_' || c == '^') {
        // DCS (Sixel graphics), OSC, APC, PM sequences — terminated by ST (\033\) or BEL (\007)
        i++;
        while (i < str.length()) {
            if (str[i] == '\007') {
                i++;
                break;
            }
            if (str[i] == '\033' && i + 1 < str.length() && str[i + 1] == '\\') {
                i += 2;
                break;
            }
            i++;
        }
        return i - start;
    }
    if (c == '(' || c == ')') {
        return std::min(start + 3, str.length()) - start;
    }
    return std::min(start + 2, str.length()) - start;
}

static auto get_utf8_char_len(const std::string& str, std::size_t start) -> std::size_t {
    if (start >= str.length()) return 0;
    const auto c = static_cast<unsigned char>(str[start]);
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

auto overlay_string(const std::string& base_line, const std::string& overlay_line, int start_x,
                    int box_w) -> std::string {
    std::string result;
    result.reserve(base_line.length() + overlay_line.length() + 32);
    int col = 0;
    std::string active_style;

    std::size_t i = 0;
    while (i < base_line.length() && col < start_x) {
        if (base_line[i] == '\033') {
            std::size_t esc_len = get_esc_seq_len(base_line, i);
            std::string_view esc(&base_line[i], esc_len);
            result.append(esc);
            if (esc.ends_with('m')) {
                if (esc == "\033[0m") {
                    active_style.clear();
                } else {
                    active_style.assign(esc);
                }
            }
            i += esc_len;
        } else {
            std::size_t char_len = get_utf8_char_len(base_line, i);
            result.append(&base_line[i], char_len);
            col++;
            i += char_len;
        }
    }

    if (col < start_x) {
        result.append(static_cast<std::size_t>(start_x - col), ' ');
        col = start_x;
    }

    result += overlay_line;

    int target_end = start_x + box_w;
    while (i < base_line.length() && col < target_end) {
        if (base_line[i] == '\033') {
            std::size_t esc_len = get_esc_seq_len(base_line, i);
            std::string_view esc(&base_line[i], esc_len);
            if (esc.ends_with('m')) {
                if (esc == "\033[0m") {
                    active_style.clear();
                } else {
                    active_style.assign(esc);
                }
            }
            i += esc_len;
        } else {
            std::size_t char_len = get_utf8_char_len(base_line, i);
            col++;
            i += char_len;
        }
    }

    if (!active_style.empty()) {
        result += active_style;
    }
    if (i < base_line.length()) {
        result.append(&base_line[i], base_line.length() - i);
    }

    return result;
}

}  // namespace simrv::tui
