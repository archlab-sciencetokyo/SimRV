/**
 * @file ScreenBuffer.cpp
 * @brief 2D cell grid screen buffer, ANSI string loader/exporter, and Canvas primitives.
 */
#include "simrv/tui/framework/ScreenBuffer.hpp"

#include <algorithm>
#include <charconv>

namespace simrv::tui::framework {

namespace {

[[nodiscard]] constexpr auto in_range(char32_t cp, char32_t first, char32_t last) noexcept -> bool {
    return cp >= first && cp <= last;
}

[[nodiscard]] constexpr auto codepoint_cell_width(char32_t cp) noexcept -> int {
    if (cp == 0 || cp < 0x20U || in_range(cp, 0x7FU, 0x9FU)) return 0;
    if (in_range(cp, 0x0300U, 0x036FU) || in_range(cp, 0x1AB0U, 0x1AFFU) ||
        in_range(cp, 0x1DC0U, 0x1DFFU) || in_range(cp, 0x20D0U, 0x20FFU) ||
        in_range(cp, 0xFE00U, 0xFE0FU) || in_range(cp, 0xFE20U, 0xFE2FU) ||
        in_range(cp, 0xE0100U, 0xE01EFU)) {
        return 0;
    }
    if (in_range(cp, 0x1100U, 0x115FU) || in_range(cp, 0x2329U, 0x232AU) ||
        in_range(cp, 0x2E80U, 0xA4CFU) || in_range(cp, 0xAC00U, 0xD7A3U) ||
        in_range(cp, 0xF900U, 0xFAFFU) || in_range(cp, 0xFE10U, 0xFE19U) ||
        in_range(cp, 0xFE30U, 0xFE6FU) || in_range(cp, 0xFF00U, 0xFF60U) ||
        in_range(cp, 0xFFE0U, 0xFFE6U) || in_range(cp, 0x1F000U, 0x1FAFFU) ||
        in_range(cp, 0x20000U, 0x3FFFD)) {
        return 2;
    }
    return 1;
}

auto decode_next_codepoint(std::string_view text, std::size_t& offset) noexcept -> char32_t {
    if (offset >= text.size()) return 0;
    const auto lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80U) {
        ++offset;
        return lead;
    }

    std::size_t count = 0;
    char32_t codepoint = 0;
    char32_t minimum = 0;
    if ((lead & 0xE0U) == 0xC0U) {
        count = 2;
        codepoint = lead & 0x1FU;
        minimum = 0x80U;
    } else if ((lead & 0xF0U) == 0xE0U) {
        count = 3;
        codepoint = lead & 0x0FU;
        minimum = 0x800U;
    } else if ((lead & 0xF8U) == 0xF0U) {
        count = 4;
        codepoint = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        ++offset;
        return 0xFFFDU;
    }

    if (offset + count > text.size()) {
        offset = text.size();
        return 0xFFFDU;
    }

    for (std::size_t i = 1; i < count; ++i) {
        const auto cont = static_cast<unsigned char>(text[offset + i]);
        if ((cont & 0xC0U) != 0x80U) {
            ++offset;
            return 0xFFFDU;
        }
        codepoint = (codepoint << 6U) | (cont & 0x3FU);
    }
    offset += count;

    if (codepoint < minimum || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        return 0xFFFDU;
    }
    return codepoint;
}

void parse_sgr_params(std::string_view seq, Style& style) {
    if (seq.empty() || seq == "0") {
        style = {};
        return;
    }

    std::vector<int> params;
    std::size_t start = 0;
    while (start < seq.size()) {
        const std::size_t sep = seq.find(';', start);
        const std::string_view token =
            (sep == std::string_view::npos) ? seq.substr(start) : seq.substr(start, sep - start);
        int val = 0;
        if (auto [_, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
            ec == std::errc{}) {
            params.push_back(val);
        }
        if (sep == std::string_view::npos) break;
        start = sep + 1;
    }

    for (std::size_t i = 0; i < params.size(); ++i) {
        const int p = params[i];
        if (p == 0) {
            style = {};
        } else if (p == 1) {
            style.attr.bold = 1;
        } else if (p == 2) {
            style.attr.dim = 1;
        } else if (p == 3) {
            style.attr.italic = 1;
        } else if (p == 4) {
            style.attr.underline = 1;
        } else if (p == 7) {
            style.attr.reverse = 1;
        } else if (p == 22) {
            style.attr.bold = 0;
            style.attr.dim = 0;
        } else if (p == 23) {
            style.attr.italic = 0;
        } else if (p == 24) {
            style.attr.underline = 0;
        } else if (p == 27) {
            style.attr.reverse = 0;
        } else if (p >= 30 && p <= 37) {
            style.fg = Color::ansi16(static_cast<std::uint8_t>(p - 30));
        } else if (p == 39) {
            style.fg = Color::default_color();
        } else if (p >= 40 && p <= 47) {
            style.bg = Color::ansi16(static_cast<std::uint8_t>(p - 40));
        } else if (p == 49) {
            style.bg = Color::default_color();
        } else if (p >= 90 && p <= 97) {
            style.fg = Color::ansi16(static_cast<std::uint8_t>(p - 90 + 8));
        } else if (p >= 100 && p <= 107) {
            style.bg = Color::ansi16(static_cast<std::uint8_t>(p - 100 + 8));
        } else if (p == 38 && i + 2 < params.size() && params[i + 1] == 5) {
            style.fg = Color::ansi256(static_cast<std::uint8_t>(params[i + 2]));
            i += 2;
        } else if (p == 38 && i + 4 < params.size() && params[i + 1] == 2) {
            style.fg = Color::rgb(static_cast<std::uint8_t>(params[i + 2]),
                                  static_cast<std::uint8_t>(params[i + 3]),
                                  static_cast<std::uint8_t>(params[i + 4]));
            i += 4;
        } else if (p == 48 && i + 2 < params.size() && params[i + 1] == 5) {
            style.bg = Color::ansi256(static_cast<std::uint8_t>(params[i + 2]));
            i += 2;
        } else if (p == 48 && i + 4 < params.size() && params[i + 1] == 2) {
            style.bg = Color::rgb(static_cast<std::uint8_t>(params[i + 2]),
                                  static_cast<std::uint8_t>(params[i + 3]),
                                  static_cast<std::uint8_t>(params[i + 4]));
            i += 4;
        }
    }
}

void encode_utf8_codepoint(char32_t cp, std::string& out) {
    if (cp < 0x80U) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

}  // namespace

ScreenBuffer::ScreenBuffer(int width, int height, ScreenCell fill) { resize(width, height, fill); }

void ScreenBuffer::resize(int width, int height, ScreenCell fill) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    cells_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), fill);
}

void ScreenBuffer::clear(Style style) {
    std::fill(cells_.begin(), cells_.end(), ScreenCell{.codepoint = U' ', .style = style});
}

auto ScreenBuffer::cell(int x, int y) -> ScreenCell& {
    if (!in_bounds(x, y)) {
        static ScreenCell dummy{};
        dummy = kEmptyCell;
        return dummy;
    }
    return cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                  static_cast<std::size_t>(x)];
}

auto ScreenBuffer::cell(int x, int y) const -> const ScreenCell& {
    if (!in_bounds(x, y)) return kEmptyCell;
    return cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                  static_cast<std::size_t>(x)];
}

void ScreenBuffer::set_cell(int x, int y, ScreenCell c) {
    if (in_bounds(x, y)) {
        cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
               static_cast<std::size_t>(x)] = c;
    }
}

void ScreenBuffer::mark_graphic_passthrough(ScreenRect rect) {
    const int start_x = std::max(0, rect.origin.x);
    const int end_x = std::min(width_, rect.origin.x + rect.size.width);
    const int start_y = std::max(0, rect.origin.y);
    const int end_y = std::min(height_, rect.origin.y + rect.size.height);

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                   static_cast<std::size_t>(x)]
                .kind = CellKind::GraphicPassthrough;
        }
    }
}

void ScreenBuffer::load_from_lines(std::span<const std::string> lines, int width, int height) {
    resize(width, height);
    for (int y = 0; y < height_; ++y) {
        if (static_cast<std::size_t>(y) >= lines.size()) {
            for (int x = 0; x < width_; ++x) {
                set_cell(x, y, ScreenCell{.codepoint = U' ', .style = {}});
            }
            continue;
        }

        std::string_view line = lines[static_cast<std::size_t>(y)];
        Style current_style{};
        std::size_t offset = 0;
        int col = 0;

        while (offset < line.size() && col < width_) {
            if (line[offset] == '\033') {
                std::size_t esc_end = offset + 1;
                if (esc_end < line.size() && line[esc_end] == '[') {
                    ++esc_end;
                    std::size_t param_start = esc_end;
                    while (esc_end < line.size() &&
                           !(line[esc_end] >= '@' && line[esc_end] <= '~')) {
                        ++esc_end;
                    }
                    if (esc_end < line.size()) {
                        const char final_ch = line[esc_end];
                        if (final_ch == 'm') {
                            parse_sgr_params(line.substr(param_start, esc_end - param_start),
                                             current_style);
                        }
                        ++esc_end;
                    }
                } else if (esc_end < line.size()) {
                    ++esc_end;
                }
                offset = esc_end;
                continue;
            }

            const char32_t cp = decode_next_codepoint(line, offset);
            const int w = codepoint_cell_width(cp);
            if (w == 0) continue;

            if (w == 2) {
                if (col + 1 < width_) {
                    set_cell(
                        col, y,
                        ScreenCell{
                            .codepoint = cp, .style = current_style, .kind = CellKind::WideLead});
                    set_cell(
                        col + 1, y,
                        ScreenCell{
                            .codepoint = U' ', .style = current_style, .kind = CellKind::WideTail});
                    col += 2;
                } else {
                    set_cell(
                        col, y,
                        ScreenCell{
                            .codepoint = U' ', .style = current_style, .kind = CellKind::Standard});
                    col += 1;
                }
            } else {
                set_cell(col, y,
                         ScreenCell{
                             .codepoint = cp, .style = current_style, .kind = CellKind::Standard});
                col += 1;
            }
        }

        while (col < width_) {
            set_cell(col++, y,
                     ScreenCell{.codepoint = U' ', .style = {}, .kind = CellKind::Standard});
        }
    }
}

auto ScreenBuffer::to_lines() const -> std::vector<std::string> {
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(height_));

    for (int y = 0; y < height_; ++y) {
        std::string line;
        Style active_style{};
        bool style_active = false;

        for (int x = 0; x < width_; ++x) {
            const auto& c = cell(x, y);
            if (c.kind == CellKind::WideTail) continue;

            if (!style_active || !(c.style == active_style)) {
                if (style_active) line += "\033[0m";
                if (!c.style.attr.is_none() || !c.style.fg.is_default() ||
                    !c.style.bg.is_default()) {
                    if (c.style.attr.bold) line += "\033[1m";
                    if (c.style.attr.dim) line += "\033[2m";
                    if (c.style.attr.italic) line += "\033[3m";
                    if (c.style.attr.underline) line += "\033[4m";
                    if (c.style.attr.reverse) line += "\033[7m";
                    if (!c.style.fg.is_default()) line += c.style.fg.to_fg_ansi(true);
                    if (!c.style.bg.is_default()) line += c.style.bg.to_bg_ansi(true);
                    style_active = true;
                } else {
                    style_active = false;
                }
                active_style = c.style;
            }

            encode_utf8_codepoint(c.codepoint, line);
        }
        if (style_active) line += "\033[0m";
        result.push_back(std::move(line));
    }
    return result;
}

auto ScreenBuffer::make_canvas() -> Canvas {
    return Canvas(*this, ScreenRect{.origin = {.x = 0, .y = 0},
                                    .size = {.width = width_, .height = height_}});
}

auto ScreenBuffer::make_canvas(ScreenRect rect) -> Canvas { return Canvas(*this, rect); }

Canvas::Canvas(ScreenBuffer& buffer, ScreenRect bounds) : buffer_(buffer), bounds_(bounds) {}

void Canvas::draw_char(int x, int y, char32_t codepoint, Style style) {
    if (x < 0 || x >= bounds_.size.width || y < 0 || y >= bounds_.size.height) return;
    const int bx = bounds_.origin.x + x;
    const int by = bounds_.origin.y + y;
    const int w = codepoint_cell_width(codepoint);
    if (w == 2) {
        if (x + 1 < bounds_.size.width) {
            buffer_.set_cell(
                bx, by,
                ScreenCell{.codepoint = codepoint, .style = style, .kind = CellKind::WideLead});
            buffer_.set_cell(
                bx + 1, by,
                ScreenCell{.codepoint = U' ', .style = style, .kind = CellKind::WideTail});
        }
    } else {
        buffer_.set_cell(
            bx, by, ScreenCell{.codepoint = codepoint, .style = style, .kind = CellKind::Standard});
    }
}

void Canvas::draw_text(int x, int y, std::string_view utf8_text, Style style) {
    if (y < 0 || y >= bounds_.size.height) return;
    Style current_style = style;
    std::size_t offset = 0;
    int col = x;

    while (offset < utf8_text.size() && col < bounds_.size.width) {
        if (utf8_text[offset] == '\033') {
            std::size_t esc_end = offset + 1;
            if (esc_end < utf8_text.size() && utf8_text[esc_end] == '[') {
                ++esc_end;
                std::size_t param_start = esc_end;
                while (esc_end < utf8_text.size() &&
                       !(utf8_text[esc_end] >= '@' && utf8_text[esc_end] <= '~')) {
                    ++esc_end;
                }
                if (esc_end < utf8_text.size()) {
                    if (utf8_text[esc_end] == 'm') {
                        parse_sgr_params(utf8_text.substr(param_start, esc_end - param_start),
                                         current_style);
                    }
                    ++esc_end;
                }
            } else if (esc_end < utf8_text.size()) {
                ++esc_end;
            }
            offset = esc_end;
            continue;
        }

        const char32_t cp = decode_next_codepoint(utf8_text, offset);
        const int w = codepoint_cell_width(cp);
        if (w == 0) continue;

        if (col >= 0) {
            draw_char(col, y, cp, current_style);
        }
        col += (w == 2) ? 2 : 1;
    }
}

void Canvas::fill_rect(ScreenRect rect, ScreenCell cell) {
    const int start_x = std::max(0, rect.origin.x);
    const int end_x = std::min(bounds_.size.width, rect.origin.x + rect.size.width);
    const int start_y = std::max(0, rect.origin.y);
    const int end_y = std::min(bounds_.size.height, rect.origin.y + rect.size.height);

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            buffer_.set_cell(bounds_.origin.x + x, bounds_.origin.y + y, cell);
        }
    }
}

void Canvas::draw_hline(int x, int y, int length, char32_t ch, Style style) {
    if (y < 0 || y >= bounds_.size.height) return;
    const int start = std::max(0, x);
    const int end = std::min(bounds_.size.width, x + length);
    for (int col = start; col < end; ++col) {
        draw_char(col, y, ch, style);
    }
}

void Canvas::draw_vline(int x, int y, int length, char32_t ch, Style style) {
    if (x < 0 || x >= bounds_.size.width) return;
    const int start = std::max(0, y);
    const int end = std::min(bounds_.size.height, y + length);
    for (int row = start; row < end; ++row) {
        draw_char(x, row, ch, style);
    }
}

void Canvas::draw_box(ScreenRect rect, BoxBorderKind border_kind, Style style) {
    if (rect.size.width < 2 || rect.size.height < 2) return;

    char32_t tl = U'┌';
    char32_t tr = U'┐';
    char32_t bl = U'└';
    char32_t br = U'┘';
    char32_t h = U'─';
    char32_t v = U'│';

    switch (border_kind) {
        case BoxBorderKind::Double:
            tl = U'╔';
            tr = U'╗';
            bl = U'╚';
            br = U'╝';
            h = U'═';
            v = U'║';
            break;
        case BoxBorderKind::Rounded:
            tl = U'╭';
            tr = U'╮';
            bl = U'╰';
            br = U'╯';
            h = U'─';
            v = U'│';
            break;
        case BoxBorderKind::Ascii:
            tl = tr = bl = br = U'+';
            h = U'-';
            v = U'|';
            break;
        case BoxBorderKind::Single:
        default:
            break;
    }

    const int x = rect.origin.x;
    const int y = rect.origin.y;
    const int w = rect.size.width;
    const int h_len = rect.size.height;

    draw_char(x, y, tl, style);
    draw_char(x + w - 1, y, tr, style);
    draw_char(x, y + h_len - 1, bl, style);
    draw_char(x + w - 1, y + h_len - 1, br, style);

    draw_hline(x + 1, y, w - 2, h, style);
    draw_hline(x + 1, y + h_len - 1, w - 2, h, style);
    draw_vline(x, y + 1, h_len - 2, v, style);
    draw_vline(x + w - 1, y + 1, h_len - 2, v, style);
}

auto Canvas::sub_canvas(ScreenRect sub_bounds) -> Canvas {
    const int sub_x = bounds_.origin.x + sub_bounds.origin.x;
    const int sub_y = bounds_.origin.y + sub_bounds.origin.y;
    const int max_w = std::max(0, bounds_.origin.x + bounds_.size.width - sub_x);
    const int max_h = std::max(0, bounds_.origin.y + bounds_.size.height - sub_y);
    const int actual_w = std::min(sub_bounds.size.width, max_w);
    const int actual_h = std::min(sub_bounds.size.height, max_h);

    return Canvas(buffer_, ScreenRect{.origin = {.x = sub_x, .y = sub_y},
                                      .size = {.width = actual_w, .height = actual_h}});
}

}  // namespace simrv::tui::framework
