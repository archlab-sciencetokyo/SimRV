/**
 * @file SdlAudio.hpp
 * @brief Host-side SDL3 Audio output manager.
 */
#pragma once

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

#ifdef HAVE_SDL3
#include <SDL3/SDL.h>
#endif

namespace simrv::core {
class Machine;
}

namespace simrv::util {

class SdlAudio {
   public:
    explicit SdlAudio(simrv::core::Machine& machine);
    ~SdlAudio();

    void init_audio();
    void shutdown_audio();

    void play_channel(int chan, Address phys_addr, Word length, Word rate, Word volume, Word panning);
    void stop_channel(int chan);
    void update_channel_params(int chan, Word volume);

    void play_music(Address music_addr, Word music_length, Word music_volume, Word music_looping);
    void stop_music();
    void update_music_volume(Word volume);

   private:
    simrv::core::Machine& machine_;
    bool audio_initialized_ = false;

#ifdef HAVE_SDL3
    SDL_AudioDeviceID              device_id_ = 0;
    std::array<SDL_AudioStream*, 8> streams_{{nullptr}};

    // Music runtime state
    SDL_AudioStream*          music_stream_  = nullptr;
    std::vector<float>        music_pcm_buf_;
    std::thread               music_thread_;
    std::atomic<bool>         music_stop_flag_{false};
#endif
};

}  // namespace simrv::util
