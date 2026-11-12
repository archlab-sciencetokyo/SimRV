/**
 * @file ScreenBuffer.hpp
 * @brief 2D cell grid screen buffer and canvas rendering primitives.
 */
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/framework/ScreenCell.hpp"
#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

enum class BoxBorderKind : std::uint8_t {
    Single,
    Double,
    Rounded,
    Ascii,
};

class Canvas;

class ScreenBuffer {
   public:
    ScreenBuffer() = default;
    ScreenBuffer(int width, int height, ScreenCell fill = {});

    void resize(int width, int height, ScreenCell fill = {});
    void clear(Style style = {});

    [[nodiscard]] auto width() const noexcept -> int { return width_; }
    [[nodiscard]] auto height() const noexcept -> int { return height_; }
    [[nodiscard]] auto in_bounds(int x, int y) const noexcept -> bool {
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    [[nodiscard]] auto cell(int x, int y) -> ScreenCell&;
    [[nodiscard]] auto cell(int x, int y) const -> const ScreenCell&;
    void set_cell(int x, int y, ScreenCell cell);

    void mark_graphic_passthrough(ScreenRect rect);

    void load_from_lines(std::span<const std::string> lines, int width, int height);
    [[nodiscard]] auto to_lines() const -> std::vector<std::string>;

    [[nodiscard]] auto make_canvas() -> Canvas;
    [[nodiscard]] auto make_canvas(ScreenRect rect) -> Canvas;

   private:
    int width_{0};
    int height_{0};
    std::vector<ScreenCell> cells_;
    static inline const ScreenCell kEmptyCell{};
};

class Canvas {
   public:
    Canvas(ScreenBuffer& buffer, ScreenRect bounds);

    [[nodiscard]] auto bounds() const noexcept -> ScreenRect { return bounds_; }
    [[nodiscard]] auto width() const noexcept -> int { return bounds_.size.width; }
    [[nodiscard]] auto height() const noexcept -> int { return bounds_.size.height; }

    void draw_text(int x, int y, std::string_view utf8_text, Style style = {});
    void draw_char(int x, int y, char32_t codepoint, Style style = {});
    void fill_rect(ScreenRect rect, ScreenCell cell);
    void draw_box(ScreenRect rect, BoxBorderKind border_kind, Style style = {});
    void draw_hline(int x, int y, int length, char32_t ch, Style style = {});
    void draw_vline(int x, int y, int length, char32_t ch, Style style = {});

    [[nodiscard]] auto sub_canvas(ScreenRect sub_bounds) -> Canvas;

   private:
    ScreenBuffer& buffer_;
    ScreenRect bounds_;
};

}  // namespace simrv::tui::framework
