/**
 * @file LogModal.hpp
 * @brief Interactive full-screen log viewer modal with severity filters and live search.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "simrv/core/Logger.hpp"

namespace simrv::tui {
class LogBuffer;
}

namespace simrv::tui::modals {

class LogModal {
   public:
    static void open(const LogBuffer& log_buffer);
    static void scroll(int delta, int total_lines, int visible_lines);
    static void scroll_page(int pages, int total_lines, int visible_lines);
    static void toggle_follow();
    static void set_filter_level(std::optional<simrv::log::Level> level);
    static void cycle_filter_level();
    static void set_search_query(std::string query);
    static void toggle_search_mode();
    [[nodiscard]] static auto is_search_mode() -> bool;
    [[nodiscard]] static auto search_query() -> const std::string&;
    [[nodiscard]] static auto filter_level() -> std::optional<simrv::log::Level>;
    [[nodiscard]] static auto is_following() -> bool;
    [[nodiscard]] static auto copy_text(const LogBuffer& log_buffer) -> std::string;

    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row,
                       const LogBuffer& log_buffer, int term_height, int term_width);

   private:
    static inline int scroll_offset_y_{0};
    static inline bool follow_tail_{true};
    static inline std::optional<simrv::log::Level> min_level_{std::nullopt};
    static inline std::string search_query_;
    static inline bool search_mode_{false};
};

}  // namespace simrv::tui::modals
