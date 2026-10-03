/**
 * @file HostAudioSink.cpp
 * @brief Dynamic host audio sink implementation using runtime dlopen of PulseAudio simple API.
 */
#include "simrv/device/virtio/HostAudioSink.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace simrv::device::virtio {

namespace {

typedef struct pa_simple pa_simple;
typedef enum pa_sample_format { PA_SAMPLE_S16LE = 3 } pa_sample_format_t;
typedef struct pa_sample_spec {
    pa_sample_format_t format;
    uint32_t rate;
    uint8_t channels;
} pa_sample_spec;
typedef enum pa_stream_direction { PA_STREAM_PLAYBACK = 1 } pa_stream_direction_t;

using pa_simple_new_fn = pa_simple* (*)(const char*, const char*, pa_stream_direction_t,
                                        const char*, const char*, const pa_sample_spec*,
                                        const void*, const void*, int*);
using pa_simple_write_fn = int (*)(pa_simple*, const void*, std::size_t, int*);
using pa_simple_free_fn = void (*)(pa_simple*);

static pa_simple_new_fn s_pa_simple_new = nullptr;
static pa_simple_write_fn s_pa_simple_write = nullptr;
static pa_simple_free_fn s_pa_simple_free = nullptr;

}  // namespace

auto HostAudioSink::instance() noexcept -> HostAudioSink& {
    static HostAudioSink sink;
    return sink;
}

HostAudioSink::HostAudioSink() {
    running_.store(true);
    worker_thread_ = std::thread(&HostAudioSink::playback_worker, this);
}

HostAudioSink::~HostAudioSink() {
    stop();
    running_.store(false);
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    close_backend();
}

auto HostAudioSink::init_backend() -> bool {
    if (pulse_s_ != nullptr) return true;

    if (pulse_handle_ == nullptr) {
        pulse_handle_ = dlopen("libpulse-simple.so.0", RTLD_LAZY);
        if (pulse_handle_ != nullptr) {
            s_pa_simple_new =
                reinterpret_cast<pa_simple_new_fn>(dlsym(pulse_handle_, "pa_simple_new"));
            s_pa_simple_write =
                reinterpret_cast<pa_simple_write_fn>(dlsym(pulse_handle_, "pa_simple_write"));
            s_pa_simple_free =
                reinterpret_cast<pa_simple_free_fn>(dlsym(pulse_handle_, "pa_simple_free"));
        }
    }

    if (s_pa_simple_new != nullptr) {
        pa_sample_spec ss{};
        ss.format = PA_SAMPLE_S16LE;
        ss.rate = sample_rate_.load();
        ss.channels = channels_.load();

        int error = 0;
        pulse_s_ = s_pa_simple_new(nullptr, "SimRV", PA_STREAM_PLAYBACK, nullptr,
                                   "Guest Audio Playback", &ss, nullptr, nullptr, &error);
        if (pulse_s_ != nullptr) return true;
    }

    return false;
}

void HostAudioSink::close_backend() {
    if (pulse_s_ != nullptr && s_pa_simple_free != nullptr) {
        s_pa_simple_free(static_cast<pa_simple*>(pulse_s_));
        pulse_s_ = nullptr;
    }
    if (pulse_handle_ != nullptr) {
        dlclose(pulse_handle_);
        pulse_handle_ = nullptr;
        s_pa_simple_new = nullptr;
        s_pa_simple_write = nullptr;
        s_pa_simple_free = nullptr;
    }
}

void HostAudioSink::start(uint32_t sample_rate, uint8_t channels) {
    const std::lock_guard lock(queue_mutex_);
    sample_rate_.store(sample_rate);
    channels_.store(channels);
    active_.store(true);
    init_backend();
}

void HostAudioSink::stop() {
    const std::lock_guard lock(queue_mutex_);
    active_.store(false);
    ring_buffer_.clear();
    if (pulse_s_ != nullptr && s_pa_simple_free != nullptr) {
        s_pa_simple_free(static_cast<pa_simple*>(pulse_s_));
        pulse_s_ = nullptr;
    }
}

void HostAudioSink::write_samples(const uint8_t* data, std::size_t bytes) {
    if (data == nullptr || bytes == 0) return;

    total_bytes_.fetch_add(bytes, std::memory_order_relaxed);

    const std::lock_guard lock(queue_mutex_);
    if (!active_.load()) return;

    const bool is_mute = muted_.load();
    const float vol = volume_.load();

    std::vector<uint8_t> adjusted(bytes);
    if (is_mute) {
        std::fill(adjusted.begin(), adjusted.end(), 0);
    } else if (std::abs(vol - 1.0f) > 0.01f) {
        const auto* src16 = reinterpret_cast<const int16_t*>(data);
        auto* dst16 = reinterpret_cast<int16_t*>(adjusted.data());
        const std::size_t num_samples = bytes / sizeof(int16_t);
        for (std::size_t i = 0; i < num_samples; ++i) {
            float sample = static_cast<float>(src16[i]) * vol;
            sample = std::clamp(sample, -32768.0f, 32767.0f);
            dst16[i] = static_cast<int16_t>(sample);
        }
    } else {
        std::memcpy(adjusted.data(), data, bytes);
    }

    if (ring_buffer_.size() + adjusted.size() > kMaxBufferSize) {
        const std::size_t drop = (ring_buffer_.size() + adjusted.size()) - kMaxBufferSize;
        ring_buffer_.erase(ring_buffer_.begin(), ring_buffer_.begin() + drop);
    }
    ring_buffer_.insert(ring_buffer_.end(), adjusted.begin(), adjusted.end());
    cv_.notify_one();
}

void HostAudioSink::playback_worker() {
    std::vector<uint8_t> chunk;
    chunk.reserve(4096);

    while (running_.load()) {
        {
            std::unique_lock lock(queue_mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(20),
                         [this] { return !running_.load() || !ring_buffer_.empty(); });

            if (!running_.load()) break;

            if (ring_buffer_.empty()) continue;

            const std::size_t fetch_bytes =
                std::min(ring_buffer_.size(), static_cast<std::size_t>(4096));
            chunk.assign(ring_buffer_.begin(), ring_buffer_.begin() + fetch_bytes);
            ring_buffer_.erase(ring_buffer_.begin(), ring_buffer_.begin() + fetch_bytes);
        }

        if (!chunk.empty()) {
            if (pulse_s_ != nullptr && s_pa_simple_write != nullptr) {
                int error = 0;
                s_pa_simple_write(static_cast<pa_simple*>(pulse_s_), chunk.data(), chunk.size(),
                                  &error);
            }
            chunk.clear();
        }
    }
}

}  // namespace simrv::device::virtio
