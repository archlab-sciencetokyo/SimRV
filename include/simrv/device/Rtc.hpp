/**
 * @file Rtc.hpp
 * @brief Real-Time Clock (RTC) MMIO device interface.
 */
#pragma once

#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv {

/**
 * @class Rtc
 * @brief Real-time clock device returning cycle count.
 *
 * Provides a simple memory-mapped register that returns the current
 * machine cycle count. Can be read to obtain elapsed simulation time.
 */
class Rtc : public memory::TileLinkNode {
   public:
    /// Construct an RTC device instance.
    explicit Rtc(simrv::core::Machine& machine);

    static constexpr Address kBaseAddress = static_cast<Address>(0x70000000u);
    static constexpr Address kSize = static_cast<Address>(0x00001000u);
    static constexpr Address kRtcOffset = static_cast<Address>(0x0u);
    static constexpr uint32_t kRtcIrq = 3;

    [[nodiscard]] auto name() const -> const char* override { return "rtc"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }

    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override;
    void evaluate_alarm();
    void sync_with_system_time();
    [[nodiscard]] auto current_time_ns() const -> uint64_t;

   private:
    simrv::core::Machine& machine_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    uint64_t base_epoch_ns_{0};
    uint64_t alarm_time_{0};
    bool alarm_enabled_{false};
    bool alarm_status_{false};
};

}  // namespace simrv
