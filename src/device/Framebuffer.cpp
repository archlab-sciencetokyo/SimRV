/**
 * @file Framebuffer.cpp
 * @brief Memory-mapped Framebuffer device with SDL3 graphics and TUI fallbacks.
 */
#include "simrv/device/Framebuffer.hpp"

#include <cstring>
#include <format>
#include <utility>

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/InputDevice.hpp"

namespace simrv::device {

Framebuffer::Framebuffer(simrv::core::Machine& /*machine*/) : fb_mem_(kSize - 0x1000, 0) {}

Framebuffer::~Framebuffer() = default;

auto Framebuffer::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    resp.error = false;
    resp.data = 0;

    const bool is_write = (req.opcode == memory::TlOpcodeA::PutFullData ||
                           req.opcode == memory::TlOpcodeA::PutPartialData);

    const Address offset = req.address - kBaseAddress;

    if (is_write) {
        if (offset < 0x1000) {
            // Control registers (0x30000000 - 0x30000FFF)
            switch (static_cast<FramebufferRegister>(offset)) {
                case FramebufferRegister::Width: {
                    const int new_w = static_cast<int>(req.data);
                    if (new_w != width_ && new_w > 0 && new_w <= 2048) {
                        width_ = new_w;
                        recreate_display_resources_ = true;
                    }
                } break;
                case FramebufferRegister::Height: {
                    const int new_h = static_cast<int>(req.data);
                    if (new_h != height_ && new_h > 0 && new_h <= 2048) {
                        height_ = new_h;
                        recreate_display_resources_ = true;
                    }
                } break;
                case FramebufferRegister::Format: {
                    const int new_fmt = static_cast<int>(req.data);
                    if (new_fmt != format_ && (new_fmt == 0 || new_fmt == 1)) {
                        format_ = new_fmt;
                        recreate_display_resources_ = true;
                    }
                } break;
                case FramebufferRegister::Flush: {
                    const Word val = req.data;
                    if (val == 1) {
                        dirty_ = true;
                        tui_dirty_ = true;
                    }
                } break;
                default:
                    break;
            }
        } else if (offset >= 0x1000 && offset < kSize) {
            // Framebuffer memory write
            const size_t fb_offset = offset - 0x1000;
            const size_t write_size = 1ULL << req.size;
            if (fb_offset + write_size <= fb_mem_.size()) {
                std::memcpy(&fb_mem_[fb_offset], &req.data, write_size);
                dirty_ = true;
                tui_dirty_ = true;
            }
        } else {
            resp.error = true;
        }
    } else if (req.opcode == memory::TlOpcodeA::Get) {
        if (offset < 0x1000) {
            switch (static_cast<FramebufferRegister>(offset)) {
                case FramebufferRegister::Width:
                    resp.data = static_cast<Word>(width_);
                    break;
                case FramebufferRegister::Height:
                    resp.data = static_cast<Word>(height_);
                    break;
                case FramebufferRegister::Format:
                    resp.data = static_cast<Word>(format_);
                    break;
                case FramebufferRegister::Flush:
                    resp.data = 0;
                    break;
                default:
                    resp.data = 0;
            }
        } else if (offset >= 0x1000 && offset < kSize) {
            // Framebuffer memory read
            const size_t fb_offset = offset - 0x1000;
            const size_t read_size = 1ULL << req.size;
            if (fb_offset + read_size <= fb_mem_.size()) {
                std::memcpy(&resp.data, &fb_mem_[fb_offset], read_size);
            }
        } else {
            resp.error = true;
        }
    }

    return true;
}

namespace {

inline void append_uint8(std::string& s, uint8_t val) {
    if (val >= 100) {
        s.push_back(static_cast<char>('0' + (val / 100)));
        s.push_back(static_cast<char>('0' + ((val / 10) % 10)));
        s.push_back(static_cast<char>('0' + (val % 10)));
    } else if (val >= 10) {
        s.push_back(static_cast<char>('0' + (val / 10)));
        s.push_back(static_cast<char>('0' + (val % 10)));
    } else {
        s.push_back(static_cast<char>('0' + val));
    }
}

inline void append_color_escape(std::string& s, bool is_fg, uint8_t r, uint8_t g, uint8_t b) {
    s.append(is_fg ? "\033[38;2;" : "\033[48;2;");
    append_uint8(s, r);
    s.push_back(';');
    append_uint8(s, g);
    s.push_back(';');
    append_uint8(s, b);
    s.push_back('m');
}

}  // namespace

auto Framebuffer::get_tui_rows(int term_w, int term_h) -> std::vector<std::string> {
    if (term_w <= 0 || term_h <= 0) return {};

    if (!tui_dirty_.load(std::memory_order_relaxed) && term_w == last_tui_w_ &&
        term_h == last_tui_h_ && !cached_tui_rows_.empty()) {
        return cached_tui_rows_;
    }

    std::vector<std::string> rows;
    rows.reserve(static_cast<size_t>(term_h));

    // A single terminal cell renders two vertical pixels (top/bottom) using the '▀' character.
    // Thus, the target resolution is term_w x (term_h * 2).
    const int target_height = term_h * 2;
    const auto sz_w = static_cast<size_t>(width_);

    if (format_ == 0) {
        // RGB565 format
        for (int row = 0; row < term_h; ++row) {
            std::string line;
            line.reserve(static_cast<size_t>(term_w) * 40ULL);
            int prev_fg_r = -1, prev_fg_g = -1, prev_fg_b = -1;
            int prev_bg_r = -1, prev_bg_g = -1, prev_bg_b = -1;

            for (int col = 0; col < term_w; ++col) {
                const int x = col * width_ / term_w;
                const int y_top = (row * 2) * height_ / target_height;
                const int y_bottom = (row * 2 + 1) * height_ / target_height;

                uint8_t r_top = 0, g_top = 0, b_top = 0;
                uint8_t r_bot = 0, g_bot = 0, b_bot = 0;

                const auto sz_x = static_cast<size_t>(x);

                const size_t offset_top = (static_cast<size_t>(y_top) * sz_w + sz_x) * 2;
                if (offset_top + 1 < fb_mem_.size()) {
                    uint16_t pixel = fb_mem_[offset_top] | (fb_mem_[offset_top + 1] << 8);
                    r_top = ((pixel >> 11) & 0x1F) * 255 / 31;
                    g_top = ((pixel >> 5) & 0x3F) * 255 / 63;
                    b_top = (pixel & 0x1F) * 255 / 31;
                }

                const size_t offset_bot = (static_cast<size_t>(y_bottom) * sz_w + sz_x) * 2;
                if (offset_bot + 1 < fb_mem_.size()) {
                    uint16_t pixel = fb_mem_[offset_bot] | (fb_mem_[offset_bot + 1] << 8);
                    r_bot = ((pixel >> 11) & 0x1F) * 255 / 31;
                    g_bot = ((pixel >> 5) & 0x3F) * 255 / 63;
                    b_bot = (pixel & 0x1F) * 255 / 31;
                }

                if (std::cmp_not_equal(r_top, prev_fg_r) || std::cmp_not_equal(g_top, prev_fg_g) ||
                    std::cmp_not_equal(b_top, prev_fg_b)) {
                    append_color_escape(line, true, r_top, g_top, b_top);
                    prev_fg_r = r_top;
                    prev_fg_g = g_top;
                    prev_fg_b = b_top;
                }

                if (std::cmp_not_equal(r_bot, prev_bg_r) || std::cmp_not_equal(g_bot, prev_bg_g) ||
                    std::cmp_not_equal(b_bot, prev_bg_b)) {
                    append_color_escape(line, false, r_bot, g_bot, b_bot);
                    prev_bg_r = r_bot;
                    prev_bg_g = g_bot;
                    prev_bg_b = b_bot;
                }

                line.append("▀");
            }
            line.append("\033[0m");
            rows.push_back(line);
        }
    } else {
        // BGRA8888 format
        for (int row = 0; row < term_h; ++row) {
            std::string line;
            line.reserve(static_cast<size_t>(term_w) * 40ULL);
            int prev_fg_r = -1, prev_fg_g = -1, prev_fg_b = -1;
            int prev_bg_r = -1, prev_bg_g = -1, prev_bg_b = -1;

            for (int col = 0; col < term_w; ++col) {
                const int x = col * width_ / term_w;
                const int y_top = (row * 2) * height_ / target_height;
                const int y_bottom = (row * 2 + 1) * height_ / target_height;

                uint8_t r_top = 0, g_top = 0, b_top = 0;
                uint8_t r_bot = 0, g_bot = 0, b_bot = 0;

                const auto sz_x = static_cast<size_t>(x);

                const size_t offset_top = (static_cast<size_t>(y_top) * sz_w + sz_x) * 4;
                if (offset_top + 2 < fb_mem_.size()) {
                    r_top = fb_mem_[offset_top + 2];
                    g_top = fb_mem_[offset_top + 1];
                    b_top = fb_mem_[offset_top];
                }

                const size_t offset_bot = (static_cast<size_t>(y_bottom) * sz_w + sz_x) * 4;
                if (offset_bot + 2 < fb_mem_.size()) {
                    r_bot = fb_mem_[offset_bot + 2];
                    g_bot = fb_mem_[offset_bot + 1];
                    b_bot = fb_mem_[offset_bot];
                }

                if (std::cmp_not_equal(r_top, prev_fg_r) || std::cmp_not_equal(g_top, prev_fg_g) ||
                    std::cmp_not_equal(b_top, prev_fg_b)) {
                    append_color_escape(line, true, r_top, g_top, b_top);
                    prev_fg_r = r_top;
                    prev_fg_g = g_top;
                    prev_fg_b = b_top;
                }

                if (std::cmp_not_equal(r_bot, prev_bg_r) || std::cmp_not_equal(g_bot, prev_bg_g) ||
                    std::cmp_not_equal(b_bot, prev_bg_b)) {
                    append_color_escape(line, false, r_bot, g_bot, b_bot);
                    prev_bg_r = r_bot;
                    prev_bg_g = g_bot;
                    prev_bg_b = b_bot;
                }

                line.append("▀");
            }
            line.append("\033[0m");
            rows.push_back(line);
        }
    }

    cached_tui_rows_ = rows;
    last_tui_w_ = term_w;
    last_tui_h_ = term_h;
    tui_dirty_.store(false, std::memory_order_relaxed);

    return rows;
}

auto Framebuffer::get_sixel_escape(int target_w, int target_h) -> std::string {
    if (target_w <= 0 || target_h <= 0) return {};

    int active_w = width_;
    int active_h = height_;

    const auto sz_w = static_cast<size_t>(width_);

    struct SixelColor {
        uint8_t r, g, b;
        bool operator==(const SixelColor&) const = default;
    };

    std::vector<SixelColor> palette;
    palette.reserve(256);
    palette.push_back({0, 0, 0});

    static thread_local std::array<int16_t, 262144> lookup;
    lookup.fill(-1);
    lookup[0] = 0;
    auto get_palette_index = [&](uint8_t r, uint8_t g, uint8_t b) -> int {
        r = (r >> 2) << 2;
        g = (g >> 2) << 2;
        b = (b >> 2) << 2;
        uint32_t key = ((static_cast<uint32_t>(r) >> 2) << 12) |
                       ((static_cast<uint32_t>(g) >> 2) << 6) | (static_cast<uint32_t>(b) >> 2);
        if (lookup[key] != -1) {
            return lookup[key];
        }
        SixelColor c{r, g, b};
        for (size_t i = 0; i < palette.size(); ++i) {
            if (palette[i] == c) {
                lookup[key] = static_cast<int16_t>(i);
                return static_cast<int>(i);
            }
        }
        if (palette.size() < 256) {
            palette.push_back(c);
            int idx = static_cast<int>(palette.size() - 1);
            lookup[key] = static_cast<int16_t>(idx);
            return idx;
        }
        int best_idx = 0;
        int min_dist = 1000000;
        for (size_t i = 0; i < palette.size(); ++i) {
            int dr = static_cast<int>(r) - palette[i].r;
            int dg = static_cast<int>(g) - palette[i].g;
            int db = static_cast<int>(b) - palette[i].b;
            int dist = dr * dr + dg * dg + db * db;
            if (dist < min_dist) {
                min_dist = dist;
                best_idx = static_cast<int>(i);
            }
        }
        lookup[key] = static_cast<int16_t>(best_idx);
        return best_idx;
    };

    std::vector<int> pixel_indices(static_cast<size_t>(target_w * target_h), 0);

    for (int y = 0; y < target_h; ++y) {
        const int src_y = y * active_h / target_h;
        for (int x = 0; x < target_w; ++x) {
            const int src_x = x * active_w / target_w;
            uint8_t r = 0, g = 0, b = 0;

            if (format_ == 0) {
                const size_t offset =
                    (static_cast<size_t>(src_y) * sz_w + static_cast<size_t>(src_x)) * 2;
                if (offset + 1 < fb_mem_.size()) {
                    uint16_t pixel = fb_mem_[offset] | (fb_mem_[offset + 1] << 8);
                    r = ((pixel >> 11) & 0x1F) * 255 / 31;
                    g = ((pixel >> 5) & 0x3F) * 255 / 63;
                    b = (pixel & 0x1F) * 255 / 31;
                }
            } else {
                const size_t offset =
                    (static_cast<size_t>(src_y) * sz_w + static_cast<size_t>(src_x)) * 4;
                if (offset + 2 < fb_mem_.size()) {
                    r = fb_mem_[offset + 2];
                    g = fb_mem_[offset + 1];
                    b = fb_mem_[offset];
                }
            }
            pixel_indices[static_cast<size_t>(y * target_w + x)] = get_palette_index(r, g, b);
        }
    }

    std::string sixel = std::format("\033P7;2;0q\"1;1;{};{}", target_w, target_h);
    for (size_t i = 0; i < palette.size(); ++i) {
        int r_pct = static_cast<int>(palette[i].r) * 100 / 255;
        int g_pct = static_cast<int>(palette[i].g) * 100 / 255;
        int b_pct = static_cast<int>(palette[i].b) * 100 / 255;
        sixel += std::format("#{};2;{};{};{}", i, r_pct, g_pct, b_pct);
    }

    for (int y_band = 0; y_band < target_h; y_band += 6) {
        std::vector<bool> color_present(palette.size(), false);
        for (int x = 0; x < target_w; ++x) {
            for (int dy = 0; dy < 6; ++dy) {
                int y = y_band + dy;
                if (y >= target_h) break;
                int color_idx = pixel_indices[static_cast<size_t>(y * target_w + x)];
                color_present[static_cast<size_t>(color_idx)] = true;
            }
        }

        bool first_color = true;
        for (size_t c = 0; c < palette.size(); ++c) {
            if (!color_present[c]) continue;

            if (!first_color) {
                sixel += "$";
            }
            first_color = false;

            sixel += std::format("#{}", c);

            int run_len = 0;
            char last_char = 0;
            auto flush_run = [&]() {
                if (run_len > 0) {
                    if (run_len > 3) {
                        sixel += std::format("!{}{}", run_len, last_char);
                    } else {
                        sixel.append(static_cast<size_t>(run_len), last_char);
                    }
                    run_len = 0;
                }
            };

            for (int x = 0; x < target_w; ++x) {
                uint8_t sixel_val = 0;
                for (int dy = 0; dy < 6; ++dy) {
                    int y = y_band + dy;
                    if (y >= target_h) break;
                    if (pixel_indices[static_cast<size_t>(y * target_w + x)] ==
                        static_cast<int>(c)) {
                        sixel_val |= (1 << dy);
                    }
                }
                char ch = static_cast<char>(63 + sixel_val);
                if (ch == last_char) {
                    run_len++;
                } else {
                    flush_run();
                    last_char = ch;
                    run_len = 1;
                }
            }
            flush_run();
        }
        sixel += "-";
    }
    sixel += "\033\\";
    tui_dirty_.store(false, std::memory_order_relaxed);
    return sixel;
}

auto Framebuffer::get_active_bounds(int& w, int& h) -> bool {
    int max_x = 0;
    int max_y = 0;
    bool found_x = false;

    size_t expected_size = static_cast<size_t>(width_ * height_) * (format_ == 0 ? 2 : 4);
    if (fb_mem_.size() < expected_size) {
        w = width_;
        h = height_;
        return false;
    }

    for (int x = width_ - 1; x >= 0; --x) {
        for (int y = 0; y < height_; ++y) {
            size_t offset = (static_cast<size_t>(y) * width_ + x) * (format_ == 0 ? 2 : 4);
            if (format_ == 0) {
                uint16_t pixel = fb_mem_[offset] | (fb_mem_[offset + 1] << 8);
                if (pixel != 0) {
                    max_x = x;
                    found_x = true;
                    break;
                }
            } else {
                if (fb_mem_[offset] != 0 || fb_mem_[offset + 1] != 0 || fb_mem_[offset + 2] != 0) {
                    max_x = x;
                    found_x = true;
                    break;
                }
            }
        }
        if (found_x) break;
    }

    bool found_y = false;
    for (int y = height_ - 1; y >= 0; --y) {
        for (int x = 0; x < width_; ++x) {
            size_t offset = (static_cast<size_t>(y) * width_ + x) * (format_ == 0 ? 2 : 4);
            if (format_ == 0) {
                uint16_t pixel = fb_mem_[offset] | (fb_mem_[offset + 1] << 8);
                if (pixel != 0) {
                    max_y = y;
                    found_y = true;
                    break;
                }
            } else {
                if (fb_mem_[offset] != 0 || fb_mem_[offset + 1] != 0 || fb_mem_[offset + 2] != 0) {
                    max_y = y;
                    found_y = true;
                    break;
                }
            }
        }
        if (found_y) break;
    }

    if (!found_x && !found_y) {
        // Framebuffer is entirely empty
        w = width_;
        h = height_;
        return false;
    }

    w = (max_x > 0) ? (max_x + 1) : width_;
    h = (max_y > 0) ? (max_y + 1) : height_;
    return true;
}

}  // namespace simrv::device
