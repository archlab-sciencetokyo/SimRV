/**
 * @file TuiComponents.hpp
 * @brief OOP Widgets for the TUI system.
 */
#pragma once

#include <string>
#include <vector>
#include <chrono>

#include "simrv/tui/TuiWidget.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

// Theme Enum
enum class TuiTheme {
    Adaptive,
    Sakura,
    HighContrast
};

// Sakura Pastel Theme Colors (static constants)
inline constexpr const char* kSakuraBorderConst = "\033[38;5;218m";  // Soft pink borders
inline constexpr const char* kSakuraTextConst   = "\033[38;5;254m";  // Warm white text
inline constexpr const char* kSakuraValConst    = "\033[38;5;183m";  // Pastel lavender for register names/values
inline constexpr const char* kSakuraMutedConst  = "\033[38;5;245m";  // Soft gray for details/dashes
inline constexpr const char* kSakuraMintConst   = "\033[38;5;121m";  // Soft mint green for counts/speeds
inline constexpr const char* kSakuraPeachConst  = "\033[38;5;223m";  // Soft peach/yellow for changed values
inline constexpr const char* kSakuraCoralConst  = "\033[38;5;210m";  // Soft coral red for stalls
inline constexpr const char* kSakuraSkyConst    = "\033[38;5;117m";  // Soft sky blue for auxiliary/page details
inline constexpr const char* kSakuraPinkConst   = "\033[38;5;211m";  // Sakura Pink accent (progress bars)

// High Contrast Theme Colors (static constants)
inline constexpr const char* kContrastBorder = "\033[1;37m";  // Bold White
inline constexpr const char* kContrastText   = "\033[1;37m";  // Bold White
inline constexpr const char* kContrastVal    = "\033[1;36m";  // Bold Cyan
inline constexpr const char* kContrastMuted  = "\033[0;37m";  // Standard White/Gray
inline constexpr const char* kContrastMint   = "\033[1;32m";  // Bold Green
inline constexpr const char* kContrastPeach  = "\033[1;33m";  // Bold Yellow
inline constexpr const char* kContrastCoral  = "\033[1;31m";  // Bold Red
inline constexpr const char* kContrastSky    = "\033[1;36m";  // Bold Cyan
inline constexpr const char* kContrastPink   = "\033[1;35m";  // Bold Magenta

// Adaptive Theme Colors (static constants using theme-adaptive ANSI colors)
inline constexpr const char* kAdaptiveBorder = "\033[34m";  // Standard Blue
inline constexpr const char* kAdaptiveText   = "\033[39m";  // Default foreground
inline constexpr const char* kAdaptiveVal    = "\033[36m";  // Standard Cyan
inline constexpr const char* kAdaptiveMuted  = "\033[90m";  // Dark Gray
inline constexpr const char* kAdaptiveMint   = "\033[32m";  // Standard Green
inline constexpr const char* kAdaptivePeach  = "\033[33m";  // Standard Yellow
inline constexpr const char* kAdaptiveCoral  = "\033[31m";  // Standard Red
inline constexpr const char* kAdaptiveSky    = "\033[36m";  // Standard Cyan
inline constexpr const char* kAdaptivePink   = "\033[35m";  // Standard Magenta

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

extern std::array<const char*, 16> g_theme_palette;
extern std::array<const char*, 16> g_theme_bg_palette;

// Map place names to dynamic theme variables for cleaner software decoupling
#define kSakuraBorder g_theme_border
#define kSakuraText g_theme_text
#define kSakuraVal g_theme_val
#define kSakuraMuted g_theme_muted
#define kSakuraMint g_theme_mint
#define kSakuraPeach g_theme_peach
#define kSakuraCoral g_theme_coral
#define kSakuraSky g_theme_sky
#define kSakuraPink g_theme_pink

extern TuiTheme g_tui_theme;
void set_tui_theme(TuiTheme theme);
TuiTheme get_tui_theme();

void set_high_contrast(bool enable);
bool is_high_contrast();


/**
 * @class RegisterPane
 * @brief Renders architectural state (GPR, FPR, VEC, Pipeline).
 */

#include <vector>
#include <array>
#include <vector>



class RegisterPane : public TuiWidget {
   public:
    explicit RegisterPane(simrv::core::Machine& machine) : machine_(machine) {
        cached_gpr_.fill(0);
        cached_fpr_.fill(0);
        cached_vec_.fill(0);
    }
    ~RegisterPane() override = default;

    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;

    void set_page(TuiRegPage page) { page_ = page; }
    [[nodiscard]] auto get_page() const -> TuiRegPage { return page_; }
    
    void update_cache();
    void set_kips(uint64_t kips) { kips_ = kips; }
    void set_kips_history(const std::vector<uint64_t>& history) { kips_history_ = history; }
    void set_paused(bool paused) { paused_ = paused; }
    void set_visible_rows(int rows) { visible_rows_ = rows; }
    void set_active_runtime(double secs) { active_runtime_ = secs; }
    void scroll(int lines);
    void reset_scroll() { scroll_offset_ = 0; }
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_offset_; }
    
   private:
    [[nodiscard]] auto get_sparkline_string(int width) -> std::string;
    [[nodiscard]] auto get_row_uncached(int row_idx, int pane_width) -> std::string;
    simrv::core::Machine& machine_;
    TuiRegPage page_ = TuiRegPage::GPR;
    bool paused_ = true;
    
    std::array<uint32_t, 32> cached_gpr_;
    std::array<uint64_t, 32> cached_fpr_;
    std::array<uint64_t, 32> cached_vec_; // Or whatever type it uses
    std::array<std::string, 40> cached_left_rows_;
    int last_width_ = 0;
    
    uint64_t kips_ = 0;
    std::vector<uint64_t> kips_history_;
    int visible_rows_ = 25;
    int scroll_offset_ = 0;
    double active_runtime_ = 0.0;
};


/**
 * @class ConsolePane
 * @brief Renders standard log output with scrollback.
 */

class ConsolePane : public TuiWidget {
   public:
    ConsolePane() = default;
    ~ConsolePane() override = default;

    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;
    
    void set_lines(const std::vector<std::string>& lines) { lines_ = lines; }
    void set_scroll_offset(int offset) { scroll_offset_ = offset; }
    
   private:
    std::vector<std::string> lines_;
    int scroll_offset_ = 0;
};


/**
 * @class StatusBar
 * @brief Renders the top and bottom header lines.
 */
class StatusBar : public TuiWidget {
   public:
    explicit StatusBar(simrv::core::Machine& machine);

    void set_paused(bool paused) { paused_ = paused; }
    void set_status_override(const std::string& status) { status_override_ = status; }
    void update_kips(uint64_t current_kips);

    void set_active_page(TuiRegPage page) { active_page_ = page; }
    void set_scroll_offset(int offset) { scroll_offset_ = offset; }
    void set_layout(TuiLayout layout) { layout_ = layout; }
    void set_pane_widths(int left, int right) { left_width_ = left; right_width_ = right; }
    void set_right_panel_mode(TuiRightPanelMode mode) { right_panel_mode_ = mode; }
    void set_trace_enabled(bool enabled) { trace_enabled_ = enabled; }

    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;

   private:
    [[nodiscard]] auto get_sparkline_string(int width) -> std::string;

    simrv::core::Machine& machine_;
    bool paused_ = true;
    bool trace_enabled_ = false;
    std::string status_override_;
    TuiRegPage active_page_ = TuiRegPage::GPR;
    TuiLayout layout_ = TuiLayout::Split;
    TuiRightPanelMode right_panel_mode_ = TuiRightPanelMode::Terminal;
    int scroll_offset_ = 0;
    int left_width_ = 0;
    int right_width_ = 0;

    uint64_t last_icount_ = 0;
    uint64_t kips_ = 0;
};

}  // namespace simrv::tui
