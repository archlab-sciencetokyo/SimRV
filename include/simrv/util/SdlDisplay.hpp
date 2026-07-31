/**
 * @file SdlDisplay.hpp
 * @brief Manages host-side SDL3 windowing, events, and rendering for SimRV.
 */
#pragma once

#include <chrono>

#ifdef HAVE_SDL3
#include <SDL3/SDL.h>
#endif

namespace simrv::core {
class Machine;
}

namespace simrv::util {

class SdlDisplay {
   public:
    explicit SdlDisplay(simrv::core::Machine& machine);
    ~SdlDisplay();

    void init();
    void shutdown();
    void update(uint64_t cycles);
    void update_gui_only();

    [[nodiscard]] auto is_enabled() const -> bool { return gui_enabled_; }

   private:
    void process_sdl_events();
    void render_sdl_frame();
    void recreate_sdl_properties();

    simrv::core::Machine& machine_;
    bool gui_enabled_ = false;
    bool mouse_grabbed_ = false;

    // Mouse input state & accumulators
    uint8_t mouse_buttons_ = 0;
    float accumulated_x_ = 0.0f;
    float accumulated_y_ = 0.0f;

    // Tick/render rate controls
    uint64_t last_tick_cycles_ = 0;
    std::chrono::steady_clock::time_point last_render_time_;

#ifdef HAVE_SDL3
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
#endif
};

}  // namespace simrv::util
