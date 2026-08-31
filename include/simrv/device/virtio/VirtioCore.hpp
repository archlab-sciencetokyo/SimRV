/**
 * @file VirtioCore.hpp
 * @brief Common OASIS VirtIO 1.2 Specification Definitions, Virtqueues, and Shared Backends.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/xlen/Types.hpp"

namespace simrv::device::virtio {

// VirtIO 1.2 Device IDs
inline constexpr uint16_t kDevIdNet = 1;
inline constexpr uint16_t kDevIdBlock = 2;
inline constexpr uint16_t kDevIdConsole = 3;
inline constexpr uint16_t kDevIdRng = 4;
inline constexpr uint16_t kDevIdBalloon = 5;
inline constexpr uint16_t kDevIdGpu = 16;
inline constexpr uint16_t kDevIdInput = 18;
inline constexpr uint16_t kDevIdSound = 25;

// VirtIO 1.2 Common Feature Bits
inline constexpr uint64_t kVirtioFVersion1 = (1ULL << 32);
inline constexpr uint64_t kVirtioFAccessPlatform = (1ULL << 33);
inline constexpr uint64_t kVirtioFRingPacked = (1ULL << 34);

// Virtqueue Split Ring Flags
inline constexpr uint16_t kVirtqDescFNext = 1;
inline constexpr uint16_t kVirtqDescFWrite = 2;
inline constexpr uint16_t kVirtqDescFIndirect = 4;

// Split Virtqueue Standard Descriptor (16 bytes)
#pragma pack(push, 1)
struct VirtqDesc {
    uint64_t addr{0};
    uint32_t len{0};
    uint16_t flags{0};
    uint16_t next{0};
};

struct VirtqUsedElem {
    uint32_t id{0};
    uint32_t len{0};
};
#pragma pack(pop)

// Dynamic Virtqueue state tracker
struct QueueState {
    uint16_t num_max{64};
    uint16_t num{64};
    uint16_t ready{0};
    uint64_t desc_addr{0};
    uint64_t driver_addr{0};
    uint64_t device_addr{0};
    uint16_t last_avail_idx{0};
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
        file_.flush();
        return true;
    }

    auto write_sectors(uint64_t sector, const std::byte* src, std::size_t len) -> bool {
        return write_sectors(sector, std::span<const std::byte>(src, len));
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

    explicit NetBackend(Mode mode = Mode::User) : mode_(mode) {}

    void set_mac(std::array<uint8_t, 6> mac) { mac_ = mac; }
    [[nodiscard]] auto get_mac() const -> const std::array<uint8_t, 6>& { return mac_; }
    [[nodiscard]] auto mode() const -> Mode { return mode_; }
    void set_mode(Mode m) { mode_ = m; }

    void push_rx_packet(const std::vector<uint8_t>& packet) { rx_queue_.push_back(packet); }
    void push_rx_packet(std::span<const uint8_t> packet) {
        rx_queue_.emplace_back(packet.begin(), packet.end());
    }

    [[nodiscard]] auto has_rx_packet() const -> bool { return !rx_queue_.empty(); }

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
};

class InputBackend {
   public:
    uint8_t select{0};
    uint8_t subsel{0};

    [[nodiscard]] auto read_config(Address offset, uint8_t size) const -> uint32_t {
        if (offset == 0) return select;
        if (offset == 1) return subsel;
        if (offset == 2) {
            return get_config_size();
        }
        if (offset >= 8) {
            const Address u_offset = offset - 8;
            return get_config_u(u_offset, size);
        }
        return 0;
    }

    void write_config(Address offset, uint32_t val, uint8_t size) {
        (void)size;
        if (offset == 0) select = static_cast<uint8_t>(val);
        if (offset == 1) subsel = static_cast<uint8_t>(val);
    }

   private:
    [[nodiscard]] auto get_config_size() const -> uint8_t {
        switch (select) {
            case 0x01: {  // VIRTIO_INPUT_CFG_ID_NAME
                static constexpr std::string_view kName = "SimRV Keyboard";
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
                if (subsel == 1) return 16;  // EV_KEY
                if (subsel == 2) return 1;   // EV_REL
                return 0;
            default:
                return 0;
        }
    }

    [[nodiscard]] auto get_config_u(Address u_offset, uint8_t size) const -> uint32_t {
        uint32_t res = 0;
        for (uint8_t b = 0; b < size; ++b) {
            const Address off = u_offset + b;
            uint8_t val = 0;
            if (select == 0x01) {
                static constexpr std::string_view kName = "SimRV Keyboard";
                if (off < kName.size()) val = static_cast<uint8_t>(kName[off]);
            } else if (select == 0x02) {
                static constexpr std::string_view kSerial = "simrv-input-0";
                if (off < kSerial.size()) val = static_cast<uint8_t>(kSerial[off]);
            } else if (select == 0x11) {
                if (subsel == 0 && off == 0)
                    val = 0x01;  // SYN_REPORT
                else if (subsel == 1 && off < 16)
                    val = 0xFF;  // Key map
                else if (subsel == 2 && off == 0)
                    val = 0x03;  // REL_X | REL_Y
            }
            res |= (static_cast<uint32_t>(val) << (b * 8));
        }
        return res;
    }
};

}  // namespace simrv::device::virtio
