/**
 * @file Rtc.cpp
 * @brief Real-Time Clock (RTC) MMIO device implementation.
 */
#include "simrv/device/Rtc.hpp"

#include <chrono>

#include "simrv/core/Machine.hpp"

namespace simrv {

Rtc::Rtc(simrv::core::Machine& machine) : machine_(machine) { sync_with_system_time(); }

void Rtc::sync_with_system_time() {
    base_epoch_ns_ = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count());
}

auto Rtc::current_time_ns() const -> uint64_t {
    return base_epoch_ns_ + (machine_.platform_time() * 100ULL);
}

void Rtc::evaluate_alarm() {
    if (alarm_enabled_ && !alarm_status_) {
        const uint64_t rtc_ns = current_time_ns();
        if (rtc_ns >= alarm_time_) {
            alarm_status_ = true;
            machine_.set_platform_irq(static_cast<int>(kRtcIrq), true);
        }
    }
}

auto Rtc::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    evaluate_alarm();
    resp.denied = false;
    resp.corrupt = false;
    resp.data = 0;

    const bool is_write = (req.opcode == memory::TlOpcodeA::PutFullData ||
                           req.opcode == memory::TlOpcodeA::PutPartialData);
    const Address offset = req.address - kBaseAddress;

    if (!is_write) {
        const uint64_t rtc_ns = current_time_ns();

        switch (offset) {
            case kRtcOffset:  // TIME_LOW (0x00)
                resp.data = static_cast<Word>(rtc_ns & 0xffffffffULL);
                break;
            case kRtcOffset + 4:  // TIME_HIGH (0x04)
                resp.data = static_cast<Word>(rtc_ns >> 32);
                break;
            case 0x10:  // IRQ_ENABLED
                resp.data = alarm_enabled_ ? 1 : 0;
                break;
            case 0x18:  // ALARM_STATUS
                resp.data = alarm_status_ ? 1 : 0;
                break;
            default:
                resp.data = 0;
                break;
        }
    } else {
        switch (offset) {
            case kRtcOffset:  // TIME_LOW (0x00)
                base_epoch_ns_ = (base_epoch_ns_ & 0xffffffff00000000ULL) | req.data;
                break;
            case kRtcOffset + 4:  // TIME_HIGH (0x04)
                base_epoch_ns_ = (base_epoch_ns_ & 0x00000000ffffffffULL) |
                                 (static_cast<uint64_t>(req.data) << 32);
                break;
            case 0x08:  // ALARM_LOW
                alarm_time_ = (alarm_time_ & 0xffffffff00000000ULL) | req.data;
                break;
            case 0x0c:  // ALARM_HIGH
                alarm_time_ =
                    (alarm_time_ & 0x00000000ffffffffULL) | (static_cast<uint64_t>(req.data) << 32);
                break;
            case 0x10:  // IRQ_ENABLED
                alarm_enabled_ = (req.data != 0);
                break;
            case 0x14:  // CLEAR_ALARM
                alarm_enabled_ = false;
                alarm_status_ = false;
                machine_.set_platform_irq(static_cast<int>(kRtcIrq), false);
                break;
            case 0x1c:  // CLEAR_INTERRUPT
                alarm_status_ = false;
                machine_.set_platform_irq(static_cast<int>(kRtcIrq), false);
                break;
            default:
                break;
        }
    }
    return true;
}

}  // namespace simrv
