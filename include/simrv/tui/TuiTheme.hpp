/**
 * @file TuiTheme.hpp
 * @brief Declares TUI themes and color palettes.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace simrv::tui {

// Theme Style Enum
enum class TuiThemeStyle : std::uint8_t { ModernUnicode, ClassicAnsi, SakuraPastel };

// Theme Enum for color palette mapping
enum class TuiTheme : std::uint8_t { Adaptive, Sakura, HighContrast, ClassicAnsi };

struct ThemeGlyphs {
    const char* top_left;
    const char* top_right;
    const char* bot_left;
    const char* bot_right;
    const char* horiz;
    const char* vert;
    const char* tee_left;
    const char* tee_right;
    const char* tee_top;
    const char* tee_bot;
    const char* cross;
    const char* double_horiz;
    const char* double_vert;
    const char* bullet;
    const char* arrow_up;
    const char* arrow_down;
    const char* arrow_left;
    const char* arrow_right;
    const char* icon_settings;
    const char* icon_help;
    const char* icon_theme;
    const char* icon_power;
    const char* icon_warn;
    const char* icon_error;
};

[[nodiscard]] auto get_theme_glyphs(TuiThemeStyle style) -> const ThemeGlyphs&;
[[nodiscard]] auto get_active_theme_style() -> TuiThemeStyle;
void set_theme_style(TuiThemeStyle style);
void cycle_theme_style();

// Sakura Pastel Theme Colors (static constants)
inline constexpr const char* kSakuraBorderConst = "\033[38;5;218m";  // Soft pink borders
inline constexpr const char* kSakuraTextConst = "\033[38;5;254m";    // Warm white text
inline constexpr const char* kSakuraValConst =
    "\033[38;5;183m";  // Pastel lavender for register names/values
inline constexpr const char* kSakuraMutedConst = "\033[38;5;245m";  // Soft gray for details/dashes
inline constexpr const char* kSakuraMintConst =
    "\033[38;5;121m";  // Soft mint green for counts/speeds
inline constexpr const char* kSakuraPeachConst =
    "\033[38;5;223m";  // Soft peach/yellow for changed values
inline constexpr const char* kSakuraCoralConst = "\033[38;5;210m";  // Soft coral red for stalls
inline constexpr const char* kSakuraSkyConst =
    "\033[38;5;117m";  // Soft sky blue for auxiliary/page details
inline constexpr const char* kSakuraPinkConst =
    "\033[38;5;211m";  // Sakura Pink accent (progress bars)

// High Contrast Theme Colors (static constants)
inline constexpr const char* kContrastBorder = "\033[1;37m";  // Bold White
inline constexpr const char* kContrastText = "\033[1;37m";    // Bold White
inline constexpr const char* kContrastVal = "\033[1;36m";     // Bold Cyan
inline constexpr const char* kContrastMuted = "\033[0;37m";   // Standard White/Gray
inline constexpr const char* kContrastMint = "\033[1;32m";    // Bold Green
inline constexpr const char* kContrastPeach = "\033[1;33m";   // Bold Yellow
inline constexpr const char* kContrastCoral = "\033[1;31m";   // Bold Red
inline constexpr const char* kContrastSky = "\033[1;36m";     // Bold Cyan
inline constexpr const char* kContrastPink = "\033[1;35m";    // Bold Magenta

// Adaptive Theme Colors (static constants using theme-adaptive ANSI colors)
inline constexpr const char* kAdaptiveBorder = "\033[34m";       // Standard Blue
inline constexpr const char* kAdaptiveText = "\033[39m";         // Default foreground
inline constexpr const char* kAdaptiveVal = "\033[36m";          // Standard Cyan
inline constexpr const char* kAdaptiveMuted = "\033[38;5;246m";  // Light Slate Gray (high-contrast)
inline constexpr const char* kAdaptiveMint = "\033[32m";         // Standard Green
inline constexpr const char* kAdaptivePeach = "\033[33m";        // Standard Yellow
inline constexpr const char* kAdaptiveCoral = "\033[31m";        // Standard Red
inline constexpr const char* kAdaptiveSky = "\033[36m";          // Standard Cyan
inline constexpr const char* kAdaptivePink = "\033[35m";         // Standard Magenta

// Classic ANSI Theme Colors (Pure standard 8-color ANSI)
inline constexpr const char* kClassicAnsiBorder = "\033[37m";  // Standard White
inline constexpr const char* kClassicAnsiText = "\033[37m";    // Standard White
inline constexpr const char* kClassicAnsiVal = "\033[36m";     // Cyan
inline constexpr const char* kClassicAnsiMuted = "\033[90m";   // Dark Gray
inline constexpr const char* kClassicAnsiMint = "\033[32m";    // Green
inline constexpr const char* kClassicAnsiPeach = "\033[33m";   // Yellow
inline constexpr const char* kClassicAnsiCoral = "\033[31m";   // Red
inline constexpr const char* kClassicAnsiSky = "\033[36m";     // Cyan
inline constexpr const char* kClassicAnsiPink = "\033[35m";    // Magenta

// Theme Variables (pointers to active color definitions)
extern const char* g_theme_border;
extern const char* g_theme_text;
extern const char* g_theme_val;
extern const char* g_theme_muted;
extern const char* g_theme_mint;
extern const char* g_theme_peach;
extern const char* g_theme_coral;
extern const char* g_theme_sky;
extern const char* g_theme_pink;
extern const char* g_theme_modal_bg;

extern std::array<const char*, 16> g_theme_palette;
extern std::array<const char*, 16> g_theme_bg_palette;

// Map place names to dynamic theme variables for cleaner software decoupling
#define kThemeBorder g_theme_border
#define kThemeText g_theme_text
#define kThemeVal g_theme_val
#define kThemeMuted g_theme_muted
#define kThemeMint g_theme_mint
#define kThemePeach g_theme_peach
#define kThemeCoral g_theme_coral
#define kThemeSky g_theme_sky
#define kThemePink g_theme_pink
#define kThemeModalBg g_theme_modal_bg

extern TuiTheme g_tui_theme;
void set_tui_theme(TuiTheme theme);
auto get_tui_theme() -> TuiTheme;

void set_high_contrast(bool enable);
auto is_high_contrast() -> bool;

auto make_repeated_string(const std::string& pattern, int count) -> std::string;
auto get_display_width(std::string_view s) -> int;
auto format_to_width(const std::string& colored_str, int target_width) -> std::string;
auto overlay_string(const std::string& base_line, const std::string& overlay_line, int start_x,
                    int box_w) -> std::string;

}  // namespace simrv::tui
