/**
 * @file Framebuffer.hpp
 * @brief Memory-mapped Framebuffer device with host-agnostic states and TUI fallbacks.
 */
#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/TileLinkNode.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

enum class FramebufferRegister : Address {
    Width = 0x0,
    Height = 0x4,
    Format = 0x8,
    Flush = 0xC,
};

class Framebuffer : public memory::TileLinkNode {
   public:
    explicit Framebuffer(simrv::core::Machine& machine);
    ~Framebuffer() override;

    static constexpr Address kBaseAddress = simrv::mmio::kFramebufferBaseAddress;
    static constexpr Address kSize = simrv::mmio::kFramebufferSize;

    [[nodiscard]] auto name() const -> const char* override { return "framebuffer"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }
    [[nodiscard]] auto contains(Address addr) const -> bool override {
        if (addr >= kBaseAddress && addr < (kBaseAddress + kSize)) {
            const Address offset = addr - kBaseAddress;
            if (offset >= 0x10 && offset < 0x20) {
                return false;
            }
            return true;
        }
        return false;
    }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override;

    // --- Public API ---
    void set_multithreaded(bool m) { multithreaded_.store(m, std::memory_order_relaxed); }

    // Fast-path direct pixel access
    [[nodiscard]] auto get_fb_ptr() -> uint8_t* { return fb_mem_.data(); }
    void set_dirty(bool d) {
        if (dirty_.load(std::memory_order_relaxed) != d) {
            dirty_.store(d, std::memory_order_relaxed);
        }
        if (d) {
            tui_dirty_.store(true, std::memory_order_relaxed);
        }
    }
    [[nodiscard]] auto is_dirty() const -> bool { return dirty_; }
    [[nodiscard]] auto is_tui_dirty() const -> bool {
        return tui_dirty_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] auto get_width() const -> int { return width_; }
    [[nodiscard]] auto get_height() const -> int { return height_; }
    [[nodiscard]] auto get_format() const -> int { return format_; }

    [[nodiscard]] auto needs_recreate() const -> bool { return recreate_display_resources_; }
    void clear_needs_recreate() { recreate_display_resources_ = false; }

    // Generates the TUI-rendered display lines for ncurses
    auto get_tui_rows(int term_w, int term_h) -> std::vector<std::string>;
    auto get_sixel_escape(int target_w, int target_h) -> std::string;
    auto get_active_bounds(int& w, int& h) -> bool;

   private:
    simrv::core::Machine& machine_;
    bool recreate_display_resources_ = false;

    // Framebuffer dimensions and format
    int width_ = 320;
    int height_ = 200;
    int format_ = 0;  // 0 = RGB565, 1 = RGBA8888

    // Backing store (2MB total capacity)
    std::vector<uint8_t> fb_mem_;

    // Performance & render control
    std::atomic<bool> dirty_{false};
    uint64_t last_tick_cycles_ = 0;
    std::atomic<bool> tui_dirty_{true};
    int last_tui_w_ = -1;
    int last_tui_h_ = -1;
    std::vector<std::string> cached_tui_rows_;
    std::atomic<bool> multithreaded_{false};
};

}  // namespace simrv::device
