#include <iostream>\n/**
 * @file VirtioDevice.cpp
 * @brief Common implementation for VirtIO MMIO device registers.
 */
#include "simrv/device/VirtioDevice.hpp"

#include <print>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::device {

VirtioDevice::VirtioDevice(simrv::core::Machine& machine, Word irq)
    : machine_(machine), irq_(irq) {}

auto VirtioDevice::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    std::cout << "VirtIO Access: addr=" << std::hex << req.address << "\n";
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
    machine_.cpu.plic_set_irq(irq_, 1);
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
            return get_device_features();
        case virtio::MmioOffset::QueueNumMax:
            return get_queue_num_max();
        case virtio::MmioOffset::ConfigGeneration:
            return get_config_generation();
        case virtio::MmioOffset::QueueReady:
            return Queue[QueueSel].Ready;
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
            QueueNum = wdata;
            break;
        case virtio::MmioOffset::QueueReady:
            Queue[QueueSel].Ready = wdata;
            break;
        case virtio::MmioOffset::QueueNotify:
            Queue[QueueSel].Notify = wdata;
            process_queue(wdata);
            break;
        case virtio::MmioOffset::InterruptACK:
            InterruptStatus &= ~wdata;
            if (InterruptStatus == 0) {
                machine_.cpu.plic_set_irq(irq_, 0);
            }
            break;
        case virtio::MmioOffset::Status:
            Status = wdata;
            break;
        case virtio::MmioOffset::QueueDescLow:
            Queue[QueueSel].DescLow = wdata;
            break;
        case virtio::MmioOffset::QueueDescHigh:
            Queue[QueueSel].DescHigh = wdata;
            break;
        case virtio::MmioOffset::QueueAvailLow:
            Queue[QueueSel].AvailLow = wdata;
            break;
        case virtio::MmioOffset::QueueAvailHigh:
            Queue[QueueSel].AvailHigh = wdata;
            break;
        case virtio::MmioOffset::QueueUsedLow:
            Queue[QueueSel].UsedLow = wdata;
            break;
        case virtio::MmioOffset::QueueUsedHigh:
            Queue[QueueSel].UsedHigh = wdata;
            break;
        default:
            break;
    }
}

}  // namespace simrv::device