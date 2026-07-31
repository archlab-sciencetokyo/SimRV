/**
 * @file VirtioDevice.cpp
 * @brief Common implementation for VirtIO MMIO device registers.
 */
#include "simrv/device/VirtioDevice.hpp"

#include <ios>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::device {

VirtioDevice::VirtioDevice(simrv::core::Machine& machine, Word irq, Word max_queues)
    : machine_(machine), irq_(irq), max_queues_(max_queues) {}

auto VirtioDevice::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    const Address offset = req.address - base_address();
    if (req.opcode == memory::TlOpcodeA::Get) {
        resp.data = mmio_read(offset);
        if (machine_.s_dlog_mode && machine_.tracer.fp_dlog.is_open()) {
            machine_.tracer.fp_dlog << std::hex << resp.data << '\n';
            machine_.tracer.fp_dlog.flush();
        }
    } else {
        mmio_write(offset, req.data);
    }
    return true;
}

void VirtioDevice::trigger_interrupt() {
    InterruptStatus |= 1;
    machine_.cpu.plic_set_irq(static_cast<int>(irq_), 1);
}

auto VirtioDevice::mmio_read(Address offset) const -> Word {
    if (offset >= static_cast<Address>(virtio::MmioOffset::Config)) {
        return read_config(offset);
    }

    switch (static_cast<virtio::MmioOffset>(offset)) {
        case virtio::MmioOffset::MagicValue:
            return virtio::kMagicValue;
        case virtio::MmioOffset::Version:
            return virtio::kVersion;
        case virtio::MmioOffset::DeviceId:
            return get_device_id();
        case virtio::MmioOffset::VendorId:
            return get_vendor_id();
        case virtio::MmioOffset::DeviceFeatures:
            if (DeviceFeaturesSel == 1) {
                return 1;  // VIRTIO_F_VERSION_1 (bit 32)
            }
            return get_device_features();
        case virtio::MmioOffset::QueueNumMax:
            return get_queue_num_max();
        case virtio::MmioOffset::ConfigGeneration:
            return get_config_generation();
        case virtio::MmioOffset::QueueReady:
            return QueueSel < max_queues_ ? Queue[QueueSel].Ready : 0;
        case virtio::MmioOffset::InterruptStatus:
            return InterruptStatus;
        case virtio::MmioOffset::Status:
            return Status;
        default:
            return 0;
    }
}

void VirtioDevice::mmio_write(Address offset, Word wdata) {
    if (offset >= static_cast<Address>(virtio::MmioOffset::Config)) {
        write_config(offset, wdata);
        return;
    }

    switch (static_cast<virtio::MmioOffset>(offset)) {
        case virtio::MmioOffset::DeviceFeaturesSel:
            DeviceFeaturesSel = wdata;
            break;
        case virtio::MmioOffset::DriverFeatures:
            DriverFeatures = wdata;
            break;
        case virtio::MmioOffset::DriverFeaturesSel:
            DriverFeaturesSel = wdata;
            break;
        case virtio::MmioOffset::QueueSel:
            QueueSel = wdata;
            break;
        case virtio::MmioOffset::QueueNum:
            if (QueueSel < max_queues_) {
                QueueNum = wdata;
            }
            break;
        case virtio::MmioOffset::QueueReady:
            if (QueueSel < max_queues_) {
                Queue[QueueSel].Ready = wdata;
            }
            break;
        case virtio::MmioOffset::QueueNotify:
            if (QueueSel < max_queues_ && wdata < max_queues_) {
                Queue[QueueSel].Notify = wdata;
                process_queue(wdata);
            }
            break;
        case virtio::MmioOffset::InterruptACK:
            InterruptStatus &= ~wdata;
            if (InterruptStatus == 0) {
                machine_.cpu.plic_set_irq(static_cast<int>(irq_), 0);
            }
            break;
        case virtio::MmioOffset::Status:
            Status = wdata;
            break;
        case virtio::MmioOffset::QueueDescLow:
            if (QueueSel < max_queues_) {
                Queue[QueueSel].DescLow = wdata;
            }
            break;
        case virtio::MmioOffset::QueueDescHigh:
            if (QueueSel < max_queues_) {
                Queue[QueueSel].DescHigh = wdata;
            }
            break;
        case virtio::MmioOffset::QueueAvailLow:
            if (QueueSel < max_queues_) {
                Queue[QueueSel].AvailLow = wdata;
            }
            break;
        case virtio::MmioOffset::QueueAvailHigh:
            if (QueueSel < max_queues_) {
                Queue[QueueSel].AvailHigh = wdata;
            }
            break;
        case virtio::MmioOffset::QueueUsedLow:
            if (QueueSel < max_queues_) {
                Queue[QueueSel].UsedLow = wdata;
            }
            break;
        case virtio::MmioOffset::QueueUsedHigh:
            if (QueueSel < max_queues_) {
                Queue[QueueSel].UsedHigh = wdata;
            }
            break;
        default:
            break;
    }
}

}  // namespace simrv::device