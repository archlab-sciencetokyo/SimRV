/**
 * @file Text.hpp
 * @brief ANSI- and UTF-8-aware terminal text operations.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace simrv::tui::framework {

[[nodiscard]] auto display_width(std::string_view text) -> int;
[[nodiscard]] auto fit_to_width(std::string_view text, int width) -> std::string;
[[nodiscard]] auto crop_columns(std::string_view text, int offset, int width) -> std::string;
[[nodiscard]] auto wrap_text(std::string_view text, int width, int continuation_indent = 0)
    -> std::vector<std::string>;
[[nodiscard]] auto overlay(std::string_view base, std::string_view value, int start_column,
                           int width) -> std::string;

}  // namespace simrv::tui::framework
