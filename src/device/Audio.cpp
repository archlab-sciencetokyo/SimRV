/**
 * @file Audio.cpp
 * @brief Memory-mapped Audio device model.
 */
#include "simrv/device/Audio.hpp"

#include <algorithm>

#include "simrv/core/Machine.hpp"

namespace simrv::device {

Audio::Audio(simrv::core::Machine& /*machine*/) {
    for (int i = 0; i < 8; ++i) {
        volume_[i] = 127;   // Max volume
        panning_[i] = 128;  // Center panning
    }
}

Audio::~Audio() = default;

auto Audio::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    const Address offset = req.address - kBaseAddress;
    const bool is_write = (req.opcode == memory::TlOpcodeA::PutFullData ||
                           req.opcode == memory::TlOpcodeA::PutPartialData);

    resp.opcode = is_write ? memory::TlOpcodeD::AccessAck : memory::TlOpcodeD::AccessAckData;
    resp.size = req.size;
    resp.source = req.source;
    resp.error = false;

    if (is_write) {
        // ---- SFX registers (0x00 – 0xFF) ----
        if (offset < 0x100u) {
            switch (offset) {
                case 0x00:  // Channel select
                    current_channel_ = std::clamp(static_cast<int>(req.data), 0, 7);
                    break;
                case 0x04:  // Sample Address
                    sample_address_[static_cast<size_t>(current_channel_)] =
                        static_cast<Address>(req.data);
                    break;
                case 0x08:  // Sample Length
                    sample_length_[static_cast<size_t>(current_channel_)] =
                        static_cast<Word>(req.data);
                    break;
                case 0x0C:  // Sample Rate
                    sample_rate_[static_cast<size_t>(current_channel_)] =
                        static_cast<Word>(req.data);
                    break;
                case 0x10:  // Volume
                    volume_[static_cast<size_t>(current_channel_)] = static_cast<Word>(req.data);
                    break;
                case 0x14:  // Panning
                    panning_[static_cast<size_t>(current_channel_)] = static_cast<Word>(req.data);
                    break;
                case 0x18: {  // Command
                    Word cmd = static_cast<Word>(req.data);
                    switch (cmd) {
                        case 1:
                            play_channel(current_channel_);
                            break;
                        case 2:
                            stop_channel(current_channel_);
                            break;
                        case 3:
                            update_channel_params(current_channel_);
                            break;
                        default:
                            break;
                    }
                } break;
                default:
                    break;
            }
        }
        // ---- Music registers (0x100 – 0x11F) ----
        else if (offset < 0x120u) {
            switch (offset) {
                case 0x100:  // MIDI data address
                    music_address_ = static_cast<Address>(req.data);
                    break;
                case 0x104:  // MIDI data length
                    music_length_ = static_cast<Word>(req.data);
                    break;
                case 0x108: {  // Command
                    Word cmd = static_cast<Word>(req.data);
                    switch (cmd) {
                        case 1:
                            play_music();
                            break;
                        case 2:
                            stop_music();
                            break;
                        case 3:
                            break;
                        default:
                            break;
                    }
                } break;
                case 0x10C:  // Volume
                    music_volume_ = static_cast<Word>(req.data);
                    break;
                case 0x110:  // Looping flag
                    music_looping_ = static_cast<Word>(req.data);
                    break;
                default:
                    break;
            }
        }
    } else {
        // ---- SFX reads ----
        if (offset < 0x100u) {
            switch (offset) {
                case 0x00:
                    resp.data = static_cast<Word>(current_channel_);
                    break;
                case 0x04:
                    resp.data = sample_address_[static_cast<size_t>(current_channel_)];
                    break;
                case 0x08:
                    resp.data = sample_length_[static_cast<size_t>(current_channel_)];
                    break;
                case 0x0C:
                    resp.data = sample_rate_[static_cast<size_t>(current_channel_)];
                    break;
                case 0x10:
                    resp.data = volume_[static_cast<size_t>(current_channel_)];
                    break;
                case 0x14:
                    resp.data = panning_[static_cast<size_t>(current_channel_)];
                    break;
                default:
                    resp.data = 0;
                    break;
            }
        }
        // ---- Music reads ----
        else if (offset < 0x120u) {
            switch (offset) {
                case 0x100:
                    resp.data = static_cast<Word>(music_address_);
                    break;
                case 0x104:
                    resp.data = music_length_;
                    break;
                case 0x10C:
                    resp.data = music_volume_;
                    break;
                case 0x110:
                    resp.data = music_looping_;
                    break;
                default:
                    resp.data = 0;
                    break;
            }
        } else {
            resp.data = 0;
        }
    }

    return true;
}

void Audio::play_channel([[maybe_unused]] int chan) {}

void Audio::stop_channel([[maybe_unused]] int chan) {}

void Audio::update_channel_params([[maybe_unused]] int chan) {}

void Audio::play_music() {}

void Audio::stop_music() {}

}  // namespace simrv::device
