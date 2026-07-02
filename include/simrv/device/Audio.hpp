/**
 * @file Audio.hpp
 * @brief Memory-mapped Audio device model for SimRV simulation.
 */
#pragma once

#include <array>
#include "simrv/Define.hpp"
#include "simrv/memory/TileLinkNode.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::util {
class SdlAudio;
}

namespace simrv::device {

class Audio : public memory::TileLinkNode {
   public:
    explicit Audio(simrv::core::Machine& machine);
    ~Audio() override;

    static constexpr Address kBaseAddress = 0x30200000u;
    static constexpr Address kSize        = 0x00010000u;  // 64KB

    // --- TileLinkNode Interface ---
    [[nodiscard]] auto name() const -> const char* override { return "audio"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp)
        -> bool override;

   private:
    // Playback control delegates
    void play_channel(int chan);
    void stop_channel(int chan);
    void update_channel_params(int chan);

    void play_music();
    void stop_music();

    simrv::core::Machine& machine_;

    // ---- SFX Hardware Registers (8 channels) ----
    int current_channel_ = 0;
    std::array<Address, 8> sample_address_{{0}};
    std::array<Word, 8>    sample_length_{{0}};
    std::array<Word, 8>    sample_rate_{{0}};
    std::array<Word, 8>    volume_{{0}};
    std::array<Word, 8>    panning_{{0}};

    // ---- Music Hardware Registers (MMIO offset 0x100+) ----
    Address music_address_ = 0;
    Word    music_length_  = 0;
    Word    music_volume_  = 100;
    Word    music_looping_ = 0;
};

}  // namespace simrv::device
