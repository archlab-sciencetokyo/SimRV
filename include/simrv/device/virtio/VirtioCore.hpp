/**
 * @file VirtioCore.hpp
 * @brief Common OASIS VirtIO 1.2 Specification Definitions, Virtqueues, and Shared Backends.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/util/UniqueFd.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::device::virtio {

// VirtIO Types
using VirtioDeviceId = uint16_t;
using VirtioQueueIndex = uint16_t;

// VirtIO 1.2 Device IDs
inline constexpr VirtioDeviceId kDevIdNet = 1;
inline constexpr VirtioDeviceId kDevIdBlock = 2;
inline constexpr VirtioDeviceId kDevIdConsole = 3;
inline constexpr VirtioDeviceId kDevIdRng = 4;
inline constexpr VirtioDeviceId kDevIdBalloon = 5;
inline constexpr VirtioDeviceId kDevIdGpu = 16;
inline constexpr VirtioDeviceId kDevIdInput = 18;
inline constexpr VirtioDeviceId kDevIdSound = 25;

// VirtIO 1.2 Common Feature Bits
inline constexpr uint64_t kVirtioFVersion1 = (1ULL << 32);
inline constexpr uint64_t kVirtioFAccessPlatform = (1ULL << 33);
inline constexpr uint64_t kVirtioFRingPacked = (1ULL << 34);

// VirtIO 1.2 Device Status Flags
inline constexpr uint8_t kVirtioStatusAcknowledge =
    std::to_underlying(VirtioDeviceStatus::Acknowledge);
inline constexpr uint8_t kVirtioStatusDriver = std::to_underlying(VirtioDeviceStatus::Driver);
inline constexpr uint8_t kVirtioStatusDriverOk = std::to_underlying(VirtioDeviceStatus::DriverOk);
inline constexpr uint8_t kVirtioStatusFeaturesOk =
    std::to_underlying(VirtioDeviceStatus::FeaturesOk);
inline constexpr uint8_t kVirtioStatusDeviceNeedsReset =
    std::to_underlying(VirtioDeviceStatus::DeviceNeedsReset);
inline constexpr uint8_t kVirtioStatusFailed = std::to_underlying(VirtioDeviceStatus::Failed);

// Virtqueue Split Ring Flags
inline constexpr uint16_t kVirtqDescFNext = std::to_underlying(VirtqDescFlag::Next);
inline constexpr uint16_t kVirtqDescFWrite = std::to_underlying(VirtqDescFlag::Write);
inline constexpr uint16_t kVirtqDescFIndirect = std::to_underlying(VirtqDescFlag::Indirect);

// Split Virtqueue Standard Descriptor (16 bytes)
#pragma pack(push, 1)
struct VirtqDesc {
    uint64_t addr{0};
    uint32_t len{0};
    uint16_t flags{0};
    uint16_t next{0};

    [[nodiscard]] constexpr auto has_flag(VirtqDescFlag f) const noexcept -> bool {
        return (flags & std::to_underlying(f)) != 0;
    }
};

struct VirtqUsedElem {
    uint32_t id{0};
    uint32_t len{0};
};
#pragma pack(pop)

// Dynamic Virtqueue state tracker
struct QueueState {
    VirtioQueueIndex num_max{64};
    VirtioQueueIndex num{64};
    uint16_t ready{0};
    uint64_t desc_addr{0};
    uint64_t driver_addr{0};
    uint64_t device_addr{0};
    VirtioQueueIndex last_avail_idx{0};
};

// -----------------------------------------------------------------------------
// Shared Device Backends
// -----------------------------------------------------------------------------

class BlockBackend {
   public:
    explicit BlockBackend(const std::string& path = "") {
        if (!path.empty()) {
            load_disk(path);
        }
    }

    auto load_disk(const std::string& path) -> bool {
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
            file_.open(path, std::ios::in | std::ios::binary);
        }
        if (file_.is_open()) {
            file_.seekg(0, std::ios::end);
            size_bytes_ = static_cast<uint64_t>(file_.tellg());
            file_.seekg(0, std::ios::beg);
            return true;
        }
        size_bytes_ = 0;
        return false;
    }

    [[nodiscard]] auto is_loaded() const -> bool { return file_.is_open(); }
    [[nodiscard]] auto capacity_sectors() const -> uint64_t { return size_bytes_ / 512ULL; }

    auto read_sectors(uint64_t sector, std::span<std::byte> dst) -> bool {
        if (!file_.is_open()) return false;
        file_.seekg(static_cast<std::streamoff>(sector * 512ULL));
        file_.read(reinterpret_cast<char*>(dst.data()), static_cast<std::streamsize>(dst.size()));
        return file_.gcount() == static_cast<std::streamsize>(dst.size());
    }

    auto read_sectors(uint64_t sector, std::byte* dst, std::size_t len) -> bool {
        return read_sectors(sector, std::span<std::byte>(dst, len));
    }

    auto write_sectors(uint64_t sector, std::span<const std::byte> src) -> bool {
        if (!file_.is_open()) return false;
        file_.seekp(static_cast<std::streamoff>(sector * 512ULL));
        file_.write(reinterpret_cast<const char*>(src.data()),
                    static_cast<std::streamsize>(src.size()));
        return true;
    }

    auto write_sectors(uint64_t sector, const std::byte* src, std::size_t len) -> bool {
        return write_sectors(sector, std::span<const std::byte>(src, len));
    }

    void flush() {
        if (file_.is_open()) {
            file_.flush();
        }
    }

   private:
    std::fstream file_;
    uint64_t size_bytes_{0};
};

class ConsoleBackend {
   public:
    ConsoleBackend() = default;

    void push_rx(uint8_t byte) { rx_fifo_.push_back(byte); }
    [[nodiscard]] auto has_rx() const -> bool { return !rx_fifo_.empty(); }
    auto pop_rx() -> uint8_t {
        if (rx_fifo_.empty()) return 0;
        uint8_t val = rx_fifo_.front();
        rx_fifo_.erase(rx_fifo_.begin());
        return val;
    }

    void write_tx(uint8_t byte) { tx_buffer_.push_back(byte); }

    [[nodiscard]] auto get_tx_data() const -> const std::vector<uint8_t>& { return tx_buffer_; }

   private:
    std::vector<uint8_t> rx_fifo_;
    std::vector<uint8_t> tx_buffer_;
};

class RngBackend {
   public:
    RngBackend() : rng_(1337) {}

    void fill_random(std::span<std::byte> dst) {
        for (auto& byte : dst) {
            byte = static_cast<std::byte>(dist_(rng_));
        }
    }

    void fill_random(std::byte* dst, std::size_t len) {
        fill_random(std::span<std::byte>(dst, len));
    }

   private:
    std::mt19937 rng_;
    std::uniform_int_distribution<uint16_t> dist_{0, 255};
};

inline constexpr uint64_t kVirtioNetFMac = (1ULL << 5);
inline constexpr uint64_t kVirtioNetFStatus = (1ULL << 16);

class NetBackend {
   public:
    enum class Mode : uint8_t {
        User = 0,    // User-mode packet buffer / frame echo
        Tap = 1,     // Host Linux TAP bridge
        Socket = 2,  // UDP/TCP socket tunnel
        None = 3,    // Disabled
    };

    explicit NetBackend(Mode mode = Mode::User);
    ~NetBackend();

    NetBackend(const NetBackend&) = delete;
    auto operator=(const NetBackend&) -> NetBackend& = delete;
    NetBackend(NetBackend&&) noexcept = default;
    auto operator=(NetBackend&&) noexcept -> NetBackend& = default;

    void set_mac(std::array<uint8_t, 6> mac) { mac_ = mac; }
    [[nodiscard]] auto get_mac() const -> const std::array<uint8_t, 6>& { return mac_; }
    [[nodiscard]] auto mode() const -> Mode { return mode_; }
    void set_mode(Mode m) { mode_ = m; }
    auto poll_host_rx() -> std::size_t;
    [[nodiscard]] auto host_backend_available() const noexcept -> bool {
        return host_fd_.get() >= 0;
    }

    void push_rx_packet(const std::vector<uint8_t>& packet) { rx_queue_.push_back(packet); }
    void push_rx_packet(std::span<const uint8_t> packet) {
        rx_queue_.emplace_back(packet.begin(), packet.end());
    }

    [[nodiscard]] auto has_rx_packet() const -> bool { return !rx_queue_.empty(); }
    void reset() {
        rx_queue_.clear();
        tx_history_.clear();
    }

    auto pop_rx_packet() -> std::vector<uint8_t> {
        if (rx_queue_.empty()) return {};
        auto pkt = rx_queue_.front();
        rx_queue_.erase(rx_queue_.begin());
        return pkt;
    }

    void send_tx_packet(std::span<const uint8_t> data) {
        std::vector<uint8_t> pkt(data.begin(), data.end());
        tx_history_.push_back(pkt);
        if (mode_ == Mode::User) {
            // Loopback ARP/ICMP frame simulation if requested
            if (data.size() >= 14) {
                // If destination matches our MAC or broadcast, echo
                push_rx_packet(pkt);
            }
        } else if (mode_ == Mode::Tap && host_fd_) {
            (void)::write(host_fd_.get(), pkt.data(), pkt.size());
        }
    }

    void send_tx_packet(const uint8_t* data, std::size_t len) {
        send_tx_packet(std::span<const uint8_t>(data, len));
    }

    [[nodiscard]] auto tx_packet_count() const -> std::size_t { return tx_history_.size(); }

   private:
    Mode mode_{Mode::User};
    std::array<uint8_t, 6> mac_{0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    std::vector<std::vector<uint8_t>> rx_queue_;
    std::vector<std::vector<uint8_t>> tx_history_;
    util::UniqueFd host_fd_;
};

struct VirtioInputEvent {
    uint16_t type{0};
    uint16_t code{0};
    uint32_t value{0};
};

struct VirtioInputAbsInfo {
    uint32_t min{0};
    uint32_t max{0};
    uint32_t fuzz{0};
    uint32_t flat{0};
    uint32_t res{0};
};

class InputBackend {
   public:
    uint8_t select{0};
    uint8_t subsel{0};
    std::deque<VirtioInputEvent> event_queue;
    std::mutex event_mutex;

    [[nodiscard]] auto read_config(Address offset, uint8_t size) const -> uint64_t {
        uint64_t res = 0;
        const uint8_t bytes_to_read = std::min<uint8_t>(size, 8);
        for (uint8_t b = 0; b < bytes_to_read; ++b) {
            const Address off = offset + b;
            uint8_t val = 0;
            if (off == 0) {
                val = select;
            } else if (off == 1) {
                val = subsel;
            } else if (off == 2) {
                val = get_config_size();
            } else if (off >= 8) {
                val = get_config_u_byte(off - 8);
            }
            res |= (static_cast<uint64_t>(val) << (b * 8));
        }
        return res;
    }

    void write_config(Address offset, uint64_t val, uint8_t size) {
        for (uint8_t b = 0; b < size; ++b) {
            const Address off = offset + b;
            const uint8_t byte_val = static_cast<uint8_t>((val >> (b * 8)) & 0xFF);
            if (off == 0) select = byte_val;
            if (off == 1) subsel = byte_val;
        }
    }

   private:
    [[nodiscard]] auto get_config_size() const -> uint8_t {
        switch (select) {
            case 0x01: {  // VIRTIO_INPUT_CFG_ID_NAME
                static constexpr std::string_view kName = "SimRV Combined Input";
                return static_cast<uint8_t>(kName.size());
            }
            case 0x02: {  // VIRTIO_INPUT_CFG_ID_SERIAL
                static constexpr std::string_view kSerial = "simrv-input-0";
                return static_cast<uint8_t>(kSerial.size());
            }
            case 0x03:  // VIRTIO_INPUT_CFG_ID_DEVIDS
                return 8;
            case 0x11:                       // VIRTIO_INPUT_CFG_EV_BITS
                if (subsel == 0) return 1;   // EV_SYN
                if (subsel == 1) return 48;  // EV_KEY (keys 0..127 + mouse buttons)
                if (subsel == 2) return 2;   // EV_REL
                if (subsel == 3) return 1;   // EV_ABS
                return 0;
            case 0x12:  // VIRTIO_INPUT_CFG_ABS_INFO
                if (subsel == 0 || subsel == 1) return sizeof(VirtioInputAbsInfo);
                return 0;
            default:
                return 0;
        }
    }

    [[nodiscard]] auto get_config_u_byte(Address off) const -> uint8_t {
        if (select == 0x01) {
            static constexpr std::string_view kName = "SimRV Combined Input";
            if (off < kName.size()) return static_cast<uint8_t>(kName[off]);
        } else if (select == 0x02) {
            static constexpr std::string_view kSerial = "simrv-input-0";
            if (off < kSerial.size()) return static_cast<uint8_t>(kSerial[off]);
        } else if (select == 0x11) {
            if (subsel == 0 && off == 0) return 0x01;  // SYN_REPORT
            if (subsel == 1) {
                if (off < 16) return 0xFF;   // Standard keys (0..127)
                if (off == 34) return 0x07;  // BTN_LEFT(0x110), BTN_RIGHT(0x111), BTN_MIDDLE(0x112)
            } else if (subsel == 2) {
                if (off == 0) return 0x00;  // No REL_X / REL_Y (absolute pointer)
                if (off == 1) return 0x01;  // REL_WHEEL
            } else if (subsel == 3 && off == 0) {
                return 0x03;  // ABS_X | ABS_Y
            }
        } else if (select == 0x12 && (subsel == 0 || subsel == 1) &&
                   off < sizeof(VirtioInputAbsInfo)) {
            VirtioInputAbsInfo info{};
            info.min = 0;
            info.max = (subsel == 0) ? 639 : 479;
            const auto* p = reinterpret_cast<const uint8_t*>(&info);
            return p[off];
        }
        return 0;
    }
};

}  // namespace simrv::device::virtio
