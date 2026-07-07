/**
 * @file SdlDisplay.cpp
 * @brief Decoupled SDL3 graphics and window event processor.
 */
#include "simrv/util/SdlDisplay.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Framebuffer.hpp"
#include "simrv/device/InputDevice.hpp"
#include "simrv/core/Logger.hpp"

#include <cctype>

namespace simrv::util {

SdlDisplay::SdlDisplay(simrv::core::Machine& machine)
    : machine_(machine) {}

SdlDisplay::~SdlDisplay() {
    try {
        shutdown();
    } catch (...) {} // NOLINT(bugprone-empty-catch)
}

void SdlDisplay::init() {
#ifdef HAVE_SDL3
    if (!machine_.framebuffer) return;
    gui_enabled_ = true;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        simrv::log::error("[SdlDisplay] Failed to initialize SDL3: {}", SDL_GetError());
        gui_enabled_ = false;
        return;
    }

    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "1");

    const int width = machine_.framebuffer->get_width();
    const int height = machine_.framebuffer->get_height();

    // Scale window by 4 for modern displays and make resizable
    window_ = SDL_CreateWindow("SimRV Framebuffer", width * 4, height * 4, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        simrv::log::error("[SdlDisplay] Failed to create SDL3 window: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        gui_enabled_ = false;
        return;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        simrv::log::error("[SdlDisplay] Failed to create SDL3 renderer: {}", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        gui_enabled_ = false;
        return;
    }
    SDL_SetRenderVSync(renderer_, 0);

    const int format = machine_.framebuffer->get_format();
    const auto sdl_format = (format == 0) ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_XRGB8888;
    texture_ = SDL_CreateTexture(renderer_, sdl_format, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture_) {
        simrv::log::error("[SdlDisplay] Failed to create SDL3 texture: {}", SDL_GetError());
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        gui_enabled_ = false;
        return;
    }

    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
    simrv::log::info("[SdlDisplay] SDL3 graphics initialized ({}x{}, format: {}).", width, height, format);
#else
    simrv::log::warn("[SdlDisplay] SimRV compiled without SDL3 support. GUI window is disabled.");
#endif
}

void SdlDisplay::shutdown() {
#ifdef HAVE_SDL3
    if (gui_enabled_) {
        if (texture_) SDL_DestroyTexture(texture_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        texture_ = nullptr;
        renderer_ = nullptr;
        window_ = nullptr;
        gui_enabled_ = false;
        simrv::log::info("[SdlDisplay] SDL3 graphics shut down.");
    }
#endif
}

void SdlDisplay::update(uint64_t cycles) {
#ifdef HAVE_SDL3
    if (!gui_enabled_ || !machine_.framebuffer) return;

    if (cycles - last_tick_cycles_ < 500000) {
        return;
    }
    last_tick_cycles_ = cycles;

    process_sdl_events();

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_render_time_).count();

    if (machine_.framebuffer->needs_recreate()) {
        recreate_sdl_properties();
        machine_.framebuffer->clear_needs_recreate();
    }

    if (machine_.framebuffer->is_dirty() && elapsed >= 16) {  // ~60 FPS
        render_sdl_frame();
        last_render_time_ = now;
        machine_.framebuffer->set_dirty(false);
    }
#else
    (void)cycles;
#endif
}

void SdlDisplay::update_gui_only() {
#ifdef HAVE_SDL3
    if (!gui_enabled_ || !machine_.framebuffer) return;
    process_sdl_events();

    if (machine_.framebuffer->needs_recreate()) {
        recreate_sdl_properties();
        machine_.framebuffer->clear_needs_recreate();
    }
    if (machine_.framebuffer->is_dirty()) {
        render_sdl_frame();
        machine_.framebuffer->set_dirty(false);
    }
#endif
}

void SdlDisplay::recreate_sdl_properties() {
#ifdef HAVE_SDL3
    if (!gui_enabled_ || !machine_.framebuffer) return;

    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }

    const int width = machine_.framebuffer->get_width();
    const int height = machine_.framebuffer->get_height();
    const int format = machine_.framebuffer->get_format();

    if (window_) {
        SDL_SetWindowSize(window_, width * 4, height * 4);
    }

    const auto sdl_format = (format == 0) ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_XRGB8888;
    texture_ = SDL_CreateTexture(renderer_, sdl_format, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture_) {
        simrv::log::error("[SdlDisplay] Failed to recreate SDL3 texture: {}", SDL_GetError());
    } else {
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
        simrv::log::info("[SdlDisplay] SDL3 texture recreated ({}x{}, format: {}).", width, height, format);
    }
#endif
}

void SdlDisplay::render_sdl_frame() {
#ifdef HAVE_SDL3
    if (!renderer_ || !texture_ || !machine_.framebuffer) return;

    const int width = machine_.framebuffer->get_width();
    const int format = machine_.framebuffer->get_format();
    const int pitch = width * (format == 0 ? 2 : 4);

    SDL_UpdateTexture(texture_, nullptr, machine_.framebuffer->get_fb_ptr(), pitch);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
#endif
}

void SdlDisplay::process_sdl_events() {
#ifdef HAVE_SDL3
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            simrv::log::info("[SdlDisplay] SDL window closed. Shutting down simulation.");
            machine_.stop();
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            if (window_ && !mouse_grabbed_) {
                SDL_SetWindowMouseGrab(window_, true);
                mouse_grabbed_ = true;
                simrv::log::info("[SdlDisplay] Mouse joystick mode active. Press Ctrl+Alt to release.");
            }
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            if (window_ && mouse_grabbed_) {
                SDL_SetWindowMouseGrab(window_, false);
                mouse_grabbed_ = false;
                simrv::log::info("[SdlDisplay] Mouse joystick mode released due to focus loss.");
            }
        } else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            const bool pressed = (event.type == SDL_EVENT_KEY_DOWN);

            // Release grab on Ctrl+Alt
            if (pressed && (event.key.mod & SDL_KMOD_CTRL) && (event.key.mod & SDL_KMOD_ALT)) {
                if (window_ && mouse_grabbed_) {
                    SDL_SetWindowMouseGrab(window_, false);
                    mouse_grabbed_ = false;
                    simrv::log::info("[SdlDisplay] Mouse joystick mode released.");
                }
            }

            Word ascii = 0;
            const auto key = event.key.key;

            if (key >= SDLK_A && key <= SDLK_Z) {
                ascii = 'a' + (key - SDLK_A);
                if (event.key.mod & SDL_KMOD_SHIFT) {
                    ascii = std::toupper(static_cast<unsigned char>(ascii));
                }
            } else if (key >= SDLK_0 && key <= SDLK_9) {
                ascii = '0' + (key - SDLK_0);
            } else {
                switch (key) {
                    case SDLK_RETURN:
                        ascii = '\r';
                        break;
                    case SDLK_ESCAPE:
                        ascii = 27;
                        break;
                    case SDLK_BACKSPACE:
                        ascii = 8;
                        break;
                    case SDLK_SPACE:
                        ascii = ' ';
                        break;
                    case SDLK_UP:
                        ascii = 'w';
                        break;  // WASD mappings for Doom
                    case SDLK_DOWN:
                        ascii = 's';
                        break;
                    case SDLK_LEFT:
                        ascii = 'a';
                        break;
                    case SDLK_RIGHT:
                        ascii = 'd';
                        break;
                    default:
                        if (key < 128) {
                            ascii = static_cast<Word>(key);
                        }
                        break;
                }
            }

            // Pack pressed bit at bit 31, ASCII in low byte
            Word pressed_bit = pressed ? 1ULL : 0ULL;
            Word packed_key = (pressed_bit << 31) | (static_cast<Word>(key) << 8) | (ascii & 0xFFULL);

            if (packed_key != 0 && machine_.input_device) {
                machine_.input_device->push_key(packed_key);
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            const bool pressed = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);

            // Grab mouse on click inside the window
            if (pressed && window_ && !mouse_grabbed_) {
                SDL_RaiseWindow(window_);
                SDL_SetWindowMouseGrab(window_, true);
                mouse_grabbed_ = true;
                simrv::log::info("[SdlDisplay] Mouse joystick mode active. Press Ctrl+Alt to release.");
            }

            const auto btn = event.button.button;
            uint8_t bit = 0;
            if (btn == SDL_BUTTON_LEFT) bit = 1;
            else if (btn == SDL_BUTTON_RIGHT) bit = 2;
            else if (btn == SDL_BUTTON_MIDDLE) bit = 4;

            if (bit != 0) {
                if (pressed) {
                    mouse_buttons_ |= bit;
                } else {
                    mouse_buttons_ &= ~bit;
                }
                if (machine_.input_device) {
                    machine_.input_device->push_mouse(0, 0, mouse_buttons_);
                }
            }
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            if (mouse_grabbed_ && machine_.input_device) {
                // Accumulate relative movement from SDL events (works under X11/WSLg without warping)
                accumulated_x_ += static_cast<float>(event.motion.xrel) * static_cast<float>(machine_.s_mouse_sensitivity);
                accumulated_y_ += static_cast<float>(event.motion.yrel) * static_cast<float>(machine_.s_mouse_sensitivity);
                int dx = static_cast<int>(accumulated_x_);
                int dy = static_cast<int>(accumulated_y_);
                accumulated_x_ -= static_cast<float>(dx);
                accumulated_y_ -= static_cast<float>(dy);
                if (dx != 0 || dy != 0) {
                    machine_.input_device->push_mouse(-dx, -dy, mouse_buttons_);
                }
            }
        }
    }
#endif
}

} // namespace simrv::util
