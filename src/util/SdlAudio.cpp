/**
 * @file SdlAudio.cpp
 * @brief Host-side SDL3 Audio output manager implementation.
 */
#include "simrv/util/SdlAudio.hpp"

#include <algorithm>
#include <array>
#include <fstream>

#include "simrv/core/Machine.hpp"
#include "simrv/core/Logger.hpp"

#ifdef HAVE_SDL3
#define TSF_IMPLEMENTATION
#define TML_IMPLEMENTATION
#include "simrv/util/tsf.h"
#include "simrv/util/tml.h"
#endif

namespace simrv::util {

SdlAudio::SdlAudio(simrv::core::Machine& machine) : machine_(machine) {}

SdlAudio::~SdlAudio() {
    shutdown_audio();
}

void SdlAudio::init_audio() {
#ifdef HAVE_SDL3
    if (!machine_.s_gui_mode) return;

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            simrv::log::error("[SdlAudio] Failed to initialize SDL3 Audio subsystem: {}",
                               SDL_GetError());
            return;
        }
    }

    device_id_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (device_id_ == 0) {
        simrv::log::error("[SdlAudio] Failed to open default playback device: {}", SDL_GetError());
        return;
    }

    audio_initialized_ = true;
#endif
}

void SdlAudio::shutdown_audio() {
#ifdef HAVE_SDL3
    if (audio_initialized_) {
        stop_music();

        for (int i = 0; i < 8; ++i) {
            if (streams_[static_cast<size_t>(i)]) {
                SDL_DestroyAudioStream(streams_[static_cast<size_t>(i)]);
                streams_[static_cast<size_t>(i)] = nullptr;
            }
        }
        if (device_id_ != 0) {
            SDL_CloseAudioDevice(device_id_);
            device_id_ = 0;
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audio_initialized_ = false;
    }
#endif
}

void SdlAudio::play_channel(int chan, Address phys_addr, Word length, Word rate, Word volume, Word panning) {
#ifdef HAVE_SDL3
    if (!audio_initialized_ || device_id_ == 0) return;

    stop_channel(chan);

    if (phys_addr < 0x80000000u || length == 0 || rate == 0) return;

    const auto* guest_src =
        reinterpret_cast<const uint8_t*>(machine_.mmem) + (phys_addr - 0x80000000u); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

    // Convert mono U8 → stereo U8 with panning
    std::vector<uint8_t> stereo_buf(static_cast<size_t>(length) * 2);
    float left_gain  = 1.0f;
    float right_gain = 1.0f;
    if (panning <= 128) right_gain = static_cast<float>(panning) / 128.0f;
    else            left_gain  = static_cast<float>(256 - panning) / 128.0f;

    for (Word i = 0; i < length; ++i) {
        int s = static_cast<int>(guest_src[i]) - 128;
        stereo_buf[static_cast<size_t>(i) * 2]     = static_cast<uint8_t>(std::clamp(static_cast<int>(left_gain  * static_cast<float>(s)) + 128, 0, 255));
        stereo_buf[static_cast<size_t>(i) * 2 + 1] = static_cast<uint8_t>(std::clamp(static_cast<int>(right_gain * static_cast<float>(s)) + 128, 0, 255));
    }

    SDL_AudioSpec spec{.format = SDL_AUDIO_U8, .channels = 2, .freq = static_cast<int>(rate)};
    const auto c_idx = static_cast<size_t>(chan);
    streams_[c_idx] = SDL_CreateAudioStream(&spec, nullptr);
    if (!streams_[c_idx]) return;

    if (!SDL_BindAudioStream(device_id_, streams_[c_idx])) {
        SDL_DestroyAudioStream(streams_[c_idx]);
        streams_[c_idx] = nullptr;
        return;
    }

    float gain = std::clamp(static_cast<float>(volume) / 127.0f, 0.0f, 1.0f);
    SDL_SetAudioStreamGain(streams_[c_idx], gain);
    SDL_PutAudioStreamData(streams_[c_idx], stereo_buf.data(),
                           static_cast<int>(stereo_buf.size()));
    SDL_ResumeAudioStreamDevice(streams_[c_idx]);
    SDL_ResumeAudioDevice(device_id_);
#endif
}

void SdlAudio::stop_channel(int chan) {
#ifdef HAVE_SDL3
    const auto c_idx = static_cast<size_t>(chan);
    if (streams_[c_idx]) {
        SDL_DestroyAudioStream(streams_[c_idx]);
        streams_[c_idx] = nullptr;
    }
#endif
}

void SdlAudio::update_channel_params(int chan, Word volume) {
#ifdef HAVE_SDL3
    const auto c_idx = static_cast<size_t>(chan);
    if (!audio_initialized_ || !streams_[c_idx]) return;
    float gain = std::clamp(static_cast<float>(volume) / 127.0f, 0.0f, 1.0f);
    SDL_SetAudioStreamGain(streams_[c_idx], gain);
#endif
}

void SdlAudio::play_music(Address music_addr, Word music_length, Word music_volume, Word music_looping) {
#ifdef HAVE_SDL3
    if (!audio_initialized_ || music_addr == 0 || music_length == 0) return;
    if (music_addr < 0x80000000u) return;

    stop_music();  // clean up any previous track

    // --- 1. Load MIDI from guest DRAM memory directly using tml ---
    const auto* midi_src =
        reinterpret_cast<const uint8_t*>(machine_.mmem) + (music_addr - 0x80000000u); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

    tml_message* midi = tml_load_memory(midi_src, static_cast<int>(music_length));
    if (!midi) {
        simrv::log::error("[SdlAudio] Music: Failed to parse MIDI data from memory.");
        return;
    }

    // --- 2. Find and load a SoundFont ---
    static const std::array<const char*, 6> sf2_paths = {
        "share/soundfonts/TimGM6mb.sf2",
        "./TimGM6mb.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/soundfonts/default.sf2",
        "/usr/share/generaluser-gs/GeneralUser-GS.sf2"
    };

    tsf* synth = nullptr;
    for (const char* path : sf2_paths) {
        if (std::ifstream(path).good()) {
            synth = tsf_load_filename(path);
            if (synth) {
                // simrv::log::info("[SdlAudio] Music: loaded SoundFont: {}", path);
                break;
            }
        }
    }

    if (!synth) {
        simrv::log::warn("[SdlAudio] Music: no SoundFont (SF2) found. Install one of:\n"
                         "  AlmaLinux/RHEL: sudo dnf install fluid-soundfont-gm\n"
                         "  Debian/Ubuntu:  sudo apt install fluid-soundfont-gm");
        tml_free(midi);
        return;
    }

    // --- 3. Render MIDI to stereo float PCM in RAM ---
    tsf_set_output(synth, TSF_STEREO_INTERLEAVED, 44100, 0.0f);

    unsigned int midi_len_ms = 0;
    tml_message* last = midi;
    while (last && last->next) {
        last = last->next;
    }
    if (last) {
        midi_len_ms = last->time;
    }

    double total_secs = static_cast<double>(midi_len_ms) / 1000.0;
    unsigned int sample_rate = 44100;
    const auto total_frames = static_cast<unsigned int>(total_secs * sample_rate);

    // Pre-allocate the float buffer (2 channels per frame)
    music_pcm_buf_.resize(static_cast<size_t>(total_frames) * 2);

    tml_message* curr = midi;
    unsigned int current_frame = 0;
    double msec = 0.0;

    while (current_frame < total_frames) {
        unsigned int block_size = 64;
        if (current_frame + block_size > total_frames) {
            block_size = total_frames - current_frame;
        }
        if (block_size == 0) break;

        double block_msec = (static_cast<double>(block_size) * 1000.0) / sample_rate;
        double next_msec = msec + block_msec;

        while (curr && curr->time <= next_msec) {
            switch (curr->type) {
                case TML_NOTE_ON:
                    tsf_channel_note_on(synth, curr->channel, curr->key, static_cast<float>(curr->velocity) / 127.0f); // NOLINT(cppcoreguidelines-pro-type-union-access)
                    break;
                case TML_NOTE_OFF:
                    tsf_channel_note_off(synth, curr->channel, curr->key); // NOLINT(cppcoreguidelines-pro-type-union-access)
                    break;
                case TML_PROGRAM_CHANGE:
                    tsf_channel_set_presetnumber(synth, curr->channel, curr->program, (curr->channel == 9)); // NOLINT(cppcoreguidelines-pro-type-union-access)
                    break;
                case TML_PITCH_BEND:
                    tsf_channel_set_pitchwheel(synth, curr->channel, curr->pitch_bend); // NOLINT(cppcoreguidelines-pro-type-union-access)
                    break;
                case TML_CONTROL_CHANGE:
                    tsf_channel_midi_control(synth, curr->channel, curr->control, curr->control_value); // NOLINT(cppcoreguidelines-pro-type-union-access)
                    break;
                default:
                    break;
            }
            curr = curr->next;
        }

        tsf_render_float(synth, &music_pcm_buf_[static_cast<size_t>(current_frame) * 2], static_cast<int>(block_size), 0);

        current_frame += block_size;
        msec = next_msec;
    }

    tml_free(midi);
    tsf_close(synth);

    // --- 4. Create an SDL3 audio stream and push the PCM ---
    SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = 2, .freq = 44100};
    music_stream_ = SDL_CreateAudioStream(&spec, nullptr);
    if (!music_stream_) {
        music_pcm_buf_.clear();
        return;
    }

    if (!SDL_BindAudioStream(device_id_, music_stream_)) {
        SDL_DestroyAudioStream(music_stream_);
        music_stream_ = nullptr;
        music_pcm_buf_.clear();
        return;
    }

    float gain = std::clamp(static_cast<float>(music_volume) / 127.0f, 0.0f, 1.0f);
    SDL_SetAudioStreamGain(music_stream_, gain);

    SDL_PutAudioStreamData(music_stream_, music_pcm_buf_.data(),
                           static_cast<int>(music_pcm_buf_.size() * sizeof(float)));
    SDL_ResumeAudioStreamDevice(music_stream_);

    // simrv::log::info("[SdlAudio] Music playing ({} KB synthesized PCM, looping={})",
    //                   (music_pcm_buf_.size() * sizeof(float)) / 1024, music_looping);

    // --- 5. Background thread: re-queue data when stream runs dry (looping) ---
    if (music_looping) {
        music_stop_flag_ = false;
        music_thread_ = std::thread([this]() -> void {
            while (!music_stop_flag_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (!music_stream_ || music_pcm_buf_.empty()) break;
                int queued = SDL_GetAudioStreamQueued(music_stream_);
                if (static_cast<size_t>(queued) < music_pcm_buf_.size() * sizeof(float) / 4) {
                    SDL_PutAudioStreamData(music_stream_, music_pcm_buf_.data(),
                                           static_cast<int>(music_pcm_buf_.size() * sizeof(float)));
                }
            }
        });
    }
#endif
}

void SdlAudio::stop_music() {
#ifdef HAVE_SDL3
    music_stop_flag_ = true;
    if (music_thread_.joinable()) music_thread_.join();

    if (music_stream_) {
        SDL_DestroyAudioStream(music_stream_);
        music_stream_ = nullptr;
    }
    music_pcm_buf_.clear();
#endif
}

void SdlAudio::update_music_volume(Word volume) {
#ifdef HAVE_SDL3
    if (music_stream_) {
        float g = std::clamp(static_cast<float>(volume) / 127.0f, 0.0f, 1.0f);
        SDL_SetAudioStreamGain(music_stream_, g);
    }
#endif
}

}  // namespace simrv::util
