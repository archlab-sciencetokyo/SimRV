/**
 * @file Framebuffer.cpp
 * @brief Memory-mapped Framebuffer device with SDL3 graphics and TUI fallbacks.
 */
#include "simrv/device/Framebuffer.hpp"
#include "simrv/device/InputDevice.hpp"

#include <cstring>
#include <format>
#include <fstream>
#include <iostream>

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::device {

Framebuffer::Framebuffer(simrv::core::Machine& machine)
    : machine_(machine), fb_mem_(kSize - 0x1000, 0) {}

Framebuffer::~Framebuffer() {}

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
                case FramebufferRegister::Width:
                {
                    const int new_w = static_cast<int>(req.data);
                    if (new_w != width_ && new_w > 0 && new_w <= 2048) {
                        width_ = new_w;
                        recreate_display_resources_ = true;
                    }
                } break;
                case FramebufferRegister::Height:
                {
                    const int new_h = static_cast<int>(req.data);
                    if (new_h != height_ && new_h > 0 && new_h <= 2048) {
                        height_ = new_h;
                        recreate_display_resources_ = true;
                    }
                } break;
                case FramebufferRegister::Format:
                {
                    const int new_fmt = static_cast<int>(req.data);
                    if (new_fmt != format_ && (new_fmt == 0 || new_fmt == 1)) {
                        format_ = new_fmt;
                        recreate_display_resources_ = true;
                    }
                } break;
                case FramebufferRegister::Flush:
                {
                    const Word val = req.data;
                    if (val == 1) {
                        dirty_ = true;
                        tui_dirty_ = true;
                        if (machine_.sdl_display && !multithreaded_.load(std::memory_order_relaxed)) {
                            machine_.sdl_display->update_gui_only();
                        }
                    } else if (val == 2) {
                        dump_ppm("screenshot.ppm");
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



void Framebuffer::dump_ppm(const std::string& filename) {
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) {
        simrv::log::error("[Framebuffer] Failed to write PPM screenshot to '{}'.", filename);
        return;
    }

    out << "P6\n" << width_ << " " << height_ << "\n255\n";

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            uint8_t r = 0, g = 0, b = 0;
            if (format_ == 0) {
                // RGB565
                const size_t offset = (static_cast<size_t>(y) * width_ + x) * 2;
                if (offset + 1 < fb_mem_.size()) {
                    uint16_t pixel = fb_mem_[offset] | (fb_mem_[offset + 1] << 8);
                    r = ((pixel >> 11) & 0x1F) * 255 / 31;
                    g = ((pixel >> 5) & 0x3F) * 255 / 63;
                    b = (pixel & 0x1F) * 255 / 31;
                }
            } else {
                // BGRA8888 format written by guest
                const size_t offset = (static_cast<size_t>(y) * width_ + x) * 4;
                if (offset + 2 < fb_mem_.size()) {
                    r = fb_mem_[offset + 2];
                    g = fb_mem_[offset + 1];
                    b = fb_mem_[offset];
                }
            }
            out.put(static_cast<char>(r));
            out.put(static_cast<char>(g));
            out.put(static_cast<char>(b));
        }
    }
    simrv::log::info("[Framebuffer] Saved screenshot to '{}'.", filename);
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

} // namespace

auto Framebuffer::get_tui_rows(int term_w, int term_h) -> std::vector<std::string> {
    if (term_w <= 0 || term_h <= 0) return {};

    if (!tui_dirty_.load(std::memory_order_relaxed) && term_w == last_tui_w_ && term_h == last_tui_h_ && !cached_tui_rows_.empty()) {
        return cached_tui_rows_;
    }

    std::vector<std::string> rows;
    rows.reserve(static_cast<size_t>(term_h));

    // A single terminal cell renders two vertical pixels (top/bottom) using the '▀' character.
    // Thus, the target resolution is term_w x (term_h * 2).
    const int target_height = term_h * 2;
    const size_t sz_w = static_cast<size_t>(width_);

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

                const size_t sz_x = static_cast<size_t>(x);

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

                if (static_cast<int>(r_top) != prev_fg_r ||
                    static_cast<int>(g_top) != prev_fg_g ||
                    static_cast<int>(b_top) != prev_fg_b) {
                    append_color_escape(line, true, r_top, g_top, b_top);
                    prev_fg_r = r_top;
                    prev_fg_g = g_top;
                    prev_fg_b = b_top;
                }

                if (static_cast<int>(r_bot) != prev_bg_r ||
                    static_cast<int>(g_bot) != prev_bg_g ||
                    static_cast<int>(b_bot) != prev_bg_b) {
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

                const size_t sz_x = static_cast<size_t>(x);

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

                if (static_cast<int>(r_top) != prev_fg_r ||
                    static_cast<int>(g_top) != prev_fg_g ||
                    static_cast<int>(b_top) != prev_fg_b) {
                    append_color_escape(line, true, r_top, g_top, b_top);
                    prev_fg_r = r_top;
                    prev_fg_g = g_top;
                    prev_fg_b = b_top;
                }

                if (static_cast<int>(r_bot) != prev_bg_r ||
                    static_cast<int>(g_bot) != prev_bg_g ||
                    static_cast<int>(b_bot) != prev_bg_b) {
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

}  // namespace simrv::device
