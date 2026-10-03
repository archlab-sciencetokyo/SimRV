/**
 * @file VirtioSoundBackend.hpp
 * @brief Common backend processing for VirtIO 1.2 Sound control and PCM streams.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "simrv/device/virtio/HostAudioSink.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"
#include "simrv/device/virtio/VirtioSoundCore.hpp"

namespace simrv::device::virtio {

class VirtioSoundBackend {
   public:
    using DmaReadFn = std::function<bool(uint64_t addr, void* dst, std::size_t len)>;
    using DmaWriteFn = std::function<bool(uint64_t addr, const void* src, std::size_t len)>;

    VirtioSoundBackend() = default;

    auto read_config(uint64_t offset, std::size_t size) const -> uint32_t {
        VirtioSndConfig cfg{
            .jacks = 1,
            .streams = 1,
            .chmaps = 1,
            .controls = 0,
        };
        if (offset + size <= sizeof(cfg)) {
            uint32_t val = 0;
            std::memcpy(&val, reinterpret_cast<const uint8_t*>(&cfg) + offset,
                        std::min(size, sizeof(uint32_t)));
            return val;
        }
        return 0;
    }

    void process_controlq(QueueState& q, const DmaReadFn& dma_read, const DmaWriteFn& dma_write,
                          const std::function<void()>& trigger_irq);

    void process_txq(QueueState& q, const DmaReadFn& dma_read, const DmaWriteFn& dma_write,
                     const std::function<void()>& trigger_irq);

    [[nodiscard]] auto is_active() const noexcept -> bool {
        return HostAudioSink::instance().is_active();
    }
    [[nodiscard]] auto sample_rate() const noexcept -> uint32_t {
        return HostAudioSink::instance().sample_rate();
    }
    [[nodiscard]] auto channels() const noexcept -> uint8_t {
        return HostAudioSink::instance().channels();
    }
};

}  // namespace simrv::device::virtio
