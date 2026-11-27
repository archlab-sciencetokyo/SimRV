/**
 * @file HostAudioSink.hpp
 * @brief Dynamic host audio sink with zero static compile dependencies.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace simrv::device::virtio {

class HostAudioSink {
   public:
    static auto instance() noexcept -> HostAudioSink&;

    HostAudioSink();
    ~HostAudioSink();

    HostAudioSink(const HostAudioSink&) = delete;
    auto operator=(const HostAudioSink&) -> HostAudioSink& = delete;
    HostAudioSink(HostAudioSink&&) = delete;
    auto operator=(HostAudioSink&&) -> HostAudioSink& = delete;

    void start(uint32_t sample_rate = 48000, uint8_t channels = 2);
    void stop();

    void write_samples(const uint8_t* data, std::size_t bytes);

    [[nodiscard]] auto is_active() const noexcept -> bool { return active_.load(); }
    [[nodiscard]] auto sample_rate() const noexcept -> uint32_t { return sample_rate_.load(); }
    [[nodiscard]] auto channels() const noexcept -> uint8_t { return channels_.load(); }
    [[nodiscard]] auto total_bytes() const noexcept -> uint64_t { return total_bytes_.load(); }
    [[nodiscard]] auto volume() const noexcept -> float { return volume_.load(); }
    void set_volume(float vol) noexcept { volume_.store(vol); }
    [[nodiscard]] auto is_muted() const noexcept -> bool { return muted_.load(); }
    void set_muted(bool m) noexcept { muted_.store(m); }

   private:
    void playback_worker();
    auto init_backend() -> bool;
    void close_backend();

    std::atomic<bool> running_{false};
    std::atomic<bool> active_{false};
    std::atomic<uint32_t> sample_rate_{48000};
    std::atomic<uint8_t> channels_{2};
    std::atomic<uint64_t> total_bytes_{0};
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> muted_{false};

    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::vector<uint8_t> ring_buffer_;
    static constexpr std::size_t kMaxBufferSize = 192000;

    std::thread worker_thread_;

    void* pulse_handle_{nullptr};
    void* pulse_s_{nullptr};
};

}  // namespace simrv::device::virtio
