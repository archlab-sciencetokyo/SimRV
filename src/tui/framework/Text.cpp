#include "simrv/tui/framework/Text.hpp"

#include <algorithm>
#include <string>

#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::framework {

auto display_width(std::string_view text) -> int { return get_display_width(text); }

auto fit_to_width(std::string_view text, int width) -> std::string {
    return format_to_width(std::string(text), std::max(0, width));
}

auto crop_columns(std::string_view text, int offset, int width) -> std::string {
    offset = std::max(0, offset);
    width = std::max(0, width);
    std::string result;
    int column = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\033') {
            std::size_t end = i + 1;
            if (end < text.size() && text[end] == '[') {
                ++end;
                while (end < text.size() && !(text[end] >= '@' && text[end] <= '~')) ++end;
                if (end < text.size()) ++end;
            }
            result.append(text.substr(i, end - i));
            i = end;
            continue;
        }
        std::size_t bytes = 1;
        auto const lead = static_cast<unsigned char>(text[i]);
        if ((lead & 0xE0U) == 0xC0U)
            bytes = 2;
        else if ((lead & 0xF0U) == 0xE0U)
            bytes = 3;
        else if ((lead & 0xF8U) == 0xF0U)
            bytes = 4;
        bytes = std::min(bytes, text.size() - i);
        auto const glyph = text.substr(i, bytes);
        int const glyph_width = display_width(glyph);
        if (column >= offset && column + glyph_width <= offset + width) result.append(glyph);
        column += glyph_width;
        i += bytes;
        if (column >= offset + width) break;
    }
    result += "\033[0m";
    return fit_to_width(result, width);
}

auto wrap_text(std::string_view text, int width, int continuation_indent)
    -> std::vector<std::string> {
    width = std::max(1, width);
    continuation_indent = std::clamp(continuation_indent, 0, width - 1);
    std::vector<std::string> rows;
    std::string current;
    std::string active_style;
    int columns = 0;
    bool continuation = false;
    bool row_has_content = false;
    auto begin_row = [&] {
        current.assign(continuation ? static_cast<std::size_t>(continuation_indent) : 0U, ' ');
        current += active_style;
        columns = continuation ? continuation_indent : 0;
        row_has_content = false;
    };
    auto finish_row = [&] {
        current += "\033[0m";
        rows.push_back(fit_to_width(current, width));
        continuation = true;
        begin_row();
    };
    auto sequence_end = [&](std::size_t start) {
        std::size_t end = start + 1;
        if (end < text.size() && text[end] == '[') {
            ++end;
            while (end < text.size() && !(text[end] >= '@' && text[end] <= '~')) ++end;
            if (end < text.size()) ++end;
        }
        return end;
    };
    auto utf8_end = [](std::string_view source, std::size_t start) {
        auto const lead = static_cast<unsigned char>(source[start]);
        std::size_t bytes = 1;
        if ((lead & 0xE0U) == 0xC0U)
            bytes = 2;
        else if ((lead & 0xF0U) == 0xE0U)
            bytes = 3;
        else if ((lead & 0xF8U) == 0xF0U)
            bytes = 4;
        return start + std::min(bytes, source.size() - start);
    };
    auto glyph_end = [&](std::size_t start) { return utf8_end(text, start); };
    auto append_word = [&](std::string_view word) {
        for (std::size_t i = 0; i < word.size();) {
            if (word[i] == '\033') {
                std::size_t end = i + 1;
                if (end < word.size() && word[end] == '[') {
                    ++end;
                    while (end < word.size() && !(word[end] >= '@' && word[end] <= '~')) ++end;
                    if (end < word.size()) ++end;
                }
                std::string const sequence(word.substr(i, end - i));
                current += sequence;
                if (sequence == "\033[0m")
                    active_style.clear();
                else if (sequence.ends_with('m'))
                    active_style = sequence;
                i = end;
                continue;
            }
            auto const end = utf8_end(word, i);
            auto const glyph = word.substr(i, end - i);
            int const glyph_width = display_width(glyph);
            if (columns + glyph_width > width && row_has_content) finish_row();
            current.append(glyph);
            columns += glyph_width;
            row_has_content = true;
            i = end;
        }
    };
    begin_row();
    bool pending_space = false;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\n') {
            finish_row();
            pending_space = false;
            ++i;
            continue;
        }
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '\r') {
            pending_space = row_has_content;
            ++i;
            continue;
        }

        const std::size_t word_start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' &&
               text[i] != '\n') {
            i = text[i] == '\033' ? sequence_end(i) : glyph_end(i);
        }
        const auto word = text.substr(word_start, i - word_start);
        const int separator_width = pending_space && row_has_content ? 1 : 0;
        if (row_has_content && columns + separator_width + display_width(word) > width) {
            finish_row();
        } else if (separator_width > 0) {
            current.push_back(' ');
            ++columns;
        }
        append_word(word);
        pending_space = false;
    }
    if (row_has_content || rows.empty()) {
        current += "\033[0m";
        rows.push_back(fit_to_width(current, width));
    }
    return rows;
}

auto overlay(std::string_view base, std::string_view value, int start_column, int width)
    -> std::string {
    return overlay_string(std::string(base), std::string(value), start_column, width);
}

}  // namespace simrv::tui::framework
