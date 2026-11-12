/**
 * @file VirtioPciNet.cpp
 * @brief Implementation of VirtIO-PCI Network Adapter Endpoint.
 */
#include "simrv/device/pci/VirtioPciNet.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <slirp/libslirp.h>

#include <chrono>
#include <climits>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/virtio/VirtioNetQueue.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {

struct virtio::NetBackend::SlirpState {
    virtio::NetBackend* backend{};
    Slirp* stack{};
    SlirpCb callbacks{};
    std::vector<pollfd> poll_fds;
    struct Timer {
        SlirpTimerCb callback{};
        void* callback_opaque{};
        int64_t expires_ms{};
        bool armed{false};
    };
    std::vector<std::unique_ptr<Timer>> timers;

    ~SlirpState() {
        if (stack != nullptr) slirp_cleanup(stack);
    }

    static auto receive_packet(const void* packet, std::size_t size, void* opaque) -> ssize_t {
        auto& state = *static_cast<SlirpState*>(opaque);
        state.backend->push_rx_packet(
            std::span<const uint8_t>(static_cast<const uint8_t*>(packet), size));
        return static_cast<ssize_t>(size);
    }

    static void guest_error(const char* message, void*) {
        simrv::log::warn("libslirp rejected guest network packet: {}", message);
    }

    static auto clock_get_ns(void*) -> int64_t {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    static void notify(void*) {
        // The simulator polls libslirp on each runner cycle, so no host thread needs waking.
    }

#if SLIRP_CHECK_VERSION(4, 9, 0)
    static void register_poll_socket(slirp_os_socket, void*) {}
    static void unregister_poll_socket(slirp_os_socket, void*) {}
#else
    static void register_poll_fd(int, void*) {}
    static void unregister_poll_fd(int, void*) {}
#endif

    static auto timer_new(SlirpTimerCb callback, void* callback_opaque, void* opaque) -> void* {
        auto& timers = static_cast<SlirpState*>(opaque)->timers;
        auto timer = std::make_unique<Timer>();
        timer->callback = callback;
        timer->callback_opaque = callback_opaque;
        auto* result = timer.get();
        timers.push_back(std::move(timer));
        return result;
    }

    static void timer_free(void* timer, void* opaque) {
        auto& timers = static_cast<SlirpState*>(opaque)->timers;
        std::erase_if(timers, [timer](const auto& entry) { return entry.get() == timer; });
    }

    static void timer_mod(void* timer, int64_t expires_ms, void*) {
        auto& entry = *static_cast<Timer*>(timer);
        entry.expires_ms = expires_ms;
        entry.armed = true;
    }

    void run_timers() {
        const int64_t now_ms = clock_get_ns(nullptr) / 1'000'000;
        for (;;) {
            const auto ready = std::ranges::find_if(timers, [now_ms](const auto& timer) {
                return timer->armed && timer->expires_ms <= now_ms;
            });
            if (ready == timers.end()) return;
            auto* timer = ready->get();
            timer->armed = false;
            const auto callback = timer->callback;
            void* const callback_opaque = timer->callback_opaque;
            if (callback != nullptr) callback(callback_opaque);
        }
    }

    static auto add_poll_fd(int fd, int events, void* opaque) -> int {
        auto& fds = static_cast<SlirpState*>(opaque)->poll_fds;
        short poll_events = 0;
        if ((events & SLIRP_POLL_IN) != 0) poll_events |= POLLIN;
        if ((events & SLIRP_POLL_OUT) != 0) poll_events |= POLLOUT;
        if ((events & SLIRP_POLL_PRI) != 0) poll_events |= POLLPRI;
        fds.push_back(pollfd{.fd = fd, .events = poll_events, .revents = 0});
        return static_cast<int>(fds.size() - 1);
    }

    static auto get_poll_events(int index, void* opaque) -> int {
        const auto& fds = static_cast<SlirpState*>(opaque)->poll_fds;
        if (index < 0 || static_cast<std::size_t>(index) >= fds.size()) return SLIRP_POLL_ERR;
        const short events = fds[static_cast<std::size_t>(index)].revents;
        int result = 0;
        if ((events & POLLIN) != 0) result |= SLIRP_POLL_IN;
        if ((events & POLLOUT) != 0) result |= SLIRP_POLL_OUT;
        if ((events & POLLPRI) != 0) result |= SLIRP_POLL_PRI;
        if ((events & POLLERR) != 0) result |= SLIRP_POLL_ERR;
        if ((events & POLLHUP) != 0) result |= SLIRP_POLL_HUP;
        return result;
    }
};

virtio::NetBackend::NetBackend(Mode mode) : mode_(mode) {}

virtio::NetBackend::~NetBackend() = default;

virtio::NetBackend::NetBackend(NetBackend&& other) noexcept
    : mode_(other.mode_),
      mac_(other.mac_),
      rx_queue_(std::move(other.rx_queue_)),
      tx_history_(std::move(other.tx_history_)),
      pending_rx_packets_(other.pending_rx_packets_),
      slirp_(std::move(other.slirp_)) {
    if (slirp_ != nullptr) slirp_->backend = this;
    other.mode_ = Mode::None;
    other.pending_rx_packets_ = 0;
}

auto virtio::NetBackend::operator=(NetBackend&& other) noexcept -> NetBackend& {
    if (this == &other) return *this;
    mode_ = other.mode_;
    mac_ = other.mac_;
    rx_queue_ = std::move(other.rx_queue_);
    tx_history_ = std::move(other.tx_history_);
    pending_rx_packets_ = other.pending_rx_packets_;
    slirp_ = std::move(other.slirp_);
    if (slirp_ != nullptr) slirp_->backend = this;
    other.mode_ = Mode::None;
    other.pending_rx_packets_ = 0;
    return *this;
}

void virtio::NetBackend::set_mode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    if (mode_ == Mode::None) slirp_.reset();
}

void virtio::NetBackend::initialize_user_mode_network() {
    slirp_ = std::make_unique<SlirpState>();
    slirp_->backend = this;

    SlirpConfig config{};
#if SLIRP_CHECK_VERSION(4, 9, 0)
    config.version = SLIRP_CONFIG_VERSION_MAX;
#else
    config.version = 4;
#endif
    slirp_->callbacks.send_packet = &SlirpState::receive_packet;
    slirp_->callbacks.guest_error = &SlirpState::guest_error;
    slirp_->callbacks.clock_get_ns = &SlirpState::clock_get_ns;
    slirp_->callbacks.notify = &SlirpState::notify;
    slirp_->callbacks.timer_new = &SlirpState::timer_new;
    slirp_->callbacks.timer_free = &SlirpState::timer_free;
    slirp_->callbacks.timer_mod = &SlirpState::timer_mod;
#if SLIRP_CHECK_VERSION(4, 9, 0)
    slirp_->callbacks.register_poll_socket = &SlirpState::register_poll_socket;
    slirp_->callbacks.unregister_poll_socket = &SlirpState::unregister_poll_socket;
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    slirp_->callbacks.register_poll_fd = &SlirpState::register_poll_fd;
    slirp_->callbacks.unregister_poll_fd = &SlirpState::unregister_poll_fd;
#pragma GCC diagnostic pop
#endif
    config.in_enabled = true;
    config.vnetwork.s_addr = htonl(0x0a000200U);
    config.vnetmask.s_addr = htonl(0xffffff00U);
    config.vhost.s_addr = htonl(0x0a000202U);
    config.vdhcp_start.s_addr = htonl(0x0a00020fU);
    config.vnameserver.s_addr = htonl(0x0a000203U);
    config.in6_enabled = false;
    // libslirp retains the callbacks pointer for the lifetime of the stack.
    slirp_->stack = slirp_new(&config, &slirp_->callbacks, slirp_.get());
    if (slirp_->stack == nullptr) {
        simrv::log::error("VirtIO user-mode networking could not initialize libslirp");
        slirp_.reset();
    }
}

auto virtio::NetBackend::host_backend_available() const noexcept -> bool {
    return mode_ == Mode::User && slirp_ != nullptr && slirp_->stack != nullptr;
}

auto virtio::NetBackend::poll_host_rx() -> std::size_t {
    if (!host_backend_available()) return 0;

    slirp_->run_timers();
    slirp_->poll_fds.clear();
    uint32_t timeout = 0;
#if SLIRP_CHECK_VERSION(4, 9, 0)
    slirp_pollfds_fill_socket(slirp_->stack, &timeout, &SlirpState::add_poll_fd, slirp_.get());
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    slirp_pollfds_fill(slirp_->stack, &timeout, &SlirpState::add_poll_fd, slirp_.get());
#pragma GCC diagnostic pop
#endif
    const int result = ::poll(slirp_->poll_fds.data(), slirp_->poll_fds.size(), 0);
    if (result < 0 && errno != EINTR) {
        slirp_pollfds_poll(slirp_->stack, 1, &SlirpState::get_poll_events, slirp_.get());
    } else {
        slirp_pollfds_poll(slirp_->stack, 0, &SlirpState::get_poll_events, slirp_.get());
    }
    slirp_->run_timers();
    return std::exchange(pending_rx_packets_, 0);
}

void virtio::NetBackend::send_tx_packet(std::span<const uint8_t> data) {
    tx_history_.emplace_back(data.begin(), data.end());
    if (mode_ == Mode::User && data.size() <= INT_MAX) {
        if (slirp_ == nullptr) initialize_user_mode_network();
        if (slirp_ == nullptr || slirp_->stack == nullptr) return;
        slirp_input(slirp_->stack, data.data(), static_cast<int>(data.size()));
    }
}

#include "simrv/device/virtio/VirtioNetQueue.hpp"

VirtioPciNet::VirtioPciNet(virtio::NetBackend::Mode mode)
    : VirtioPciDevice(PciSubsystemId{static_cast<uint16_t>(virtio::kDevIdNet)},
                      PciClassCode{0x020000}, 2),
      backend_(mode) {}

void VirtioPciNet::poll_backend() {
    if (backend_.poll_host_rx() != 0) on_queue_notify(0);
}

auto VirtioPciNet::get_device_features(virtio::VirtioFeatureWord select)
    -> virtio::VirtioFeatureWord {
    if (select == 0) {
        return static_cast<virtio::VirtioFeatureWord>(virtio::kVirtioNetFMac |
                                                      virtio::kVirtioNetFStatus);
    }
    if (select == 1) {
        return virtio::kVirtioFVersion1FeatureWord;
    }
    return 0;
}

auto VirtioPciNet::read_device_config(PciBarOffset offset, PciAccessBytes size) -> PciMmioData {
    const auto& mac = backend_.get_mac();
    PciMmioData value = 0;
    for (uint8_t byte = 0; byte < size && byte < sizeof(value); ++byte) {
        const auto config_offset = offset + byte;
        uint8_t value_byte = 0;
        if (config_offset < mac.size()) {
            value_byte = mac[config_offset.raw()];
        } else if (config_offset == 6) {
            value_byte = 1;  // VIRTIO_NET_S_LINK_UP
        }
        value |= static_cast<PciMmioData>(value_byte) << (byte * 8);
    }
    return value;
}

void VirtioPciNet::on_queue_notify(virtio::VirtioQueueIndex queue_index) {
    if (queue_index >= queues_.size()) return;
    auto& q = queues_[queue_index];
    core::Machine* machine = root_complex_ != nullptr ? root_complex_->machine() : nullptr;
    const bool defer_completion = machine != nullptr && machine->runtime_profile.is_cycle_mode() &&
                                  machine->dma_engine().is_enabled();

    auto dma_read_cb = [this](uint64_t addr, void* buf, size_t size) {
        return dma_read_wire_address(addr, buf, size);
    };
    auto dma_write_cb = [this](uint64_t addr, const void* buf, size_t size) {
        return dma_write_wire_address(addr, buf, size);
    };
    auto schedule_deferred = [this, machine, queue_index](size_t transfer_bytes,
                                                          uint16_t head_desc_idx, uint32_t len) {
        const auto current_cycle = machine->memory().system_bus().cycle();
        const auto generation = queues_[queue_index].generation;
        machine->dma_engine().schedule_transfer(
            transfer_bytes, current_cycle,
            [this, queue_index, head_desc_idx, len, generation]() {
                auto& q = queues_[queue_index];
                if (q.generation != generation || !q.usable()) return;
                uint16_t used_idx = 0;
                if (!dma_read_wire_address(q.device_addr + 2, &used_idx, 2)) return;
                virtio::VirtqUsedElem elem{.id = head_desc_idx, .len = len};
                dma_write_wire_address(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem,
                                       sizeof(elem));
                used_idx++;
                dma_write_wire_address(q.device_addr + 2, &used_idx, 2);
                isr_status_ |= 0x1;
                trigger_irq();
            },
            "virtio-pci-net");
    };
    auto notify_complete = [this]() {
        isr_status_ |= 0x1;
        trigger_irq();
    };

    virtio::process_net_queue(queue_index, q, backend_, defer_completion, dma_read_cb, dma_write_cb,
                              schedule_deferred, notify_complete);
}

}  // namespace simrv::device
