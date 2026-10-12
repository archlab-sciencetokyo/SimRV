/**
 * @file VirtioPciNet.cpp
 * @brief Implementation of VirtIO-PCI Network Adapter Endpoint.
 */
#include "simrv/device/pci/VirtioPciNet.hpp"

#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/virtio/VirtioNetQueue.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {

virtio::NetBackend::NetBackend(Mode mode) : mode_(mode) {
    if (mode_ != Mode::Tap) return;

    const char* requested = std::getenv("SIMRV_TAP_IFACE");
    const char* interface_name = (requested && *requested) ? requested : "simrv0";
    const int fd = ::open("/dev/net/tun", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        simrv::log::warn("VirtIO TAP backend unavailable: cannot open /dev/net/tun");
        return;
    }

    ifreq ifr{};
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, interface_name, IFNAMSIZ - 1);
    if (::ioctl(fd, TUNSETIFF, &ifr) < 0) {
        ::close(fd);
        simrv::log::warn("VirtIO TAP backend unavailable: TUNSETIFF failed for {}", interface_name);
        return;
    }
    host_fd_ = util::UniqueFd(fd);
}

virtio::NetBackend::~NetBackend() = default;

auto virtio::NetBackend::poll_host_rx() -> std::size_t {
    if (mode_ != Mode::Tap || !host_fd_) return 0;
    std::size_t received = 0;
    std::array<uint8_t, 65536> frame{};
    for (;;) {
        const auto count = ::read(host_fd_.get(), frame.data(), frame.size());
        if (count > 0) {
            push_rx_packet(std::span<const uint8_t>(frame.data(), static_cast<size_t>(count)));
            ++received;
            continue;
        }
        if (count < 0 && errno == EAGAIN) break;
        break;
    }
    return received;
}

#include "simrv/device/virtio/VirtioNetQueue.hpp"

VirtioPciNet::VirtioPciNet(virtio::NetBackend::Mode mode)
    : VirtioPciDevice(virtio::kDevIdNet, 0x020000, 2), backend_(mode) {}

void VirtioPciNet::poll_backend() {
    if (backend_.poll_host_rx() != 0) on_queue_notify(0);
}

auto VirtioPciNet::get_device_features(uint32_t select) -> uint32_t {
    if (select == 0) {
        return static_cast<uint32_t>(virtio::kVirtioNetFMac | virtio::kVirtioNetFStatus);
    }
    if (select == 1) {
        return virtio::kVirtioFVersion1FeatureWord;
    }
    return 0;
}

auto VirtioPciNet::read_device_config(simrv::xlen::Address offset, uint8_t size) -> uint32_t {
    const auto& mac = backend_.get_mac();
    uint32_t value = 0;
    for (uint8_t byte = 0; byte < size && byte < sizeof(value); ++byte) {
        const auto config_offset = offset + byte;
        uint8_t value_byte = 0;
        if (config_offset < mac.size()) {
            value_byte = mac[config_offset];
        } else if (config_offset == 6) {
            value_byte = 1;  // VIRTIO_NET_S_LINK_UP
        }
        value |= static_cast<uint32_t>(value_byte) << (byte * 8);
    }
    return value;
}

void VirtioPciNet::on_queue_notify(uint16_t queue_index) {
    if (queue_index >= queues_.size()) return;
    auto& q = queues_[queue_index];
    core::Machine* machine = root_complex_ != nullptr ? root_complex_->machine() : nullptr;
    const bool defer_completion = machine != nullptr && machine->runtime_profile.is_cycle_mode() &&
                                  machine->dma_engine().is_enabled();

    auto dma_read_cb = [this](simrv::xlen::Address addr, void* buf, size_t size) {
        return dma_read(addr, buf, size);
    };
    auto dma_write_cb = [this](simrv::xlen::Address addr, const void* buf, size_t size) {
        return dma_write(addr, buf, size);
    };
    auto schedule_deferred = [this, machine, queue_index](size_t transfer_bytes,
                                                          uint16_t head_desc_idx, uint32_t len) {
        const auto current_cycle = machine->memory().system_bus().cycle();
        machine->dma_engine().schedule_transfer(
            transfer_bytes, current_cycle,
            [this, queue_index, head_desc_idx, len]() {
                auto& q = queues_[queue_index];
                uint16_t used_idx = 0;
                dma_read(q.device_addr + 2, &used_idx, 2);
                virtio::VirtqUsedElem elem{head_desc_idx, len};
                dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem,
                          sizeof(elem));
                used_idx++;
                dma_write(q.device_addr + 2, &used_idx, 2);
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
