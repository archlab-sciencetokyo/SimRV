#include "simrv/device/RingBufferEthernetMmio.hpp"

#include <algorithm>

#include "simrv/core/Machine.hpp"

namespace simrv::device {

class RingBufferEthernetMmio::Window final : public memory::MmioDevice {
   public:
    enum class Kind : uint8_t { Csr, Rx, Tx };

    Window(RingBufferEthernetMmio& owner, core::Machine& machine, Kind kind, Address base, Address size,
          std::string label)
        : memory::MmioDevice(&machine),
          owner_(owner),
          kind_(kind),
          base_(base),
          size_(size),
          label_(std::move(label)) {}

    [[nodiscard]] auto name() const -> const char* override { return label_.c_str(); }
    [[nodiscard]] auto base_address() const -> Address override { return base_; }
    [[nodiscard]] auto size() const -> Address override { return size_; }
    [[nodiscard]] auto read32(Address offset) -> uint32_t override {
        if (kind_ == Kind::Csr) return owner_.read_csr(offset);
        return owner_.read_buffer(kind_ == Kind::Tx, offset);
    }
    void write32(Address offset, uint32_t value) override {
        if (kind_ == Kind::Csr) {
            owner_.write_csr(offset, value);
        } else {
            owner_.write_buffer(kind_ == Kind::Tx, offset, value);
        }
    }

   private:
    RingBufferEthernetMmio& owner_;
    Kind kind_;
    Address base_;
    Address size_;
    std::string label_;
};

RingBufferEthernetMmio::RingBufferEthernetMmio(core::Machine& machine, std::string name,
                                               Address csr_base, Address csr_size, uint32_t irq,
                                               Address rx_base, Address rx_size, Address tx_base,
                                               Address tx_size,
                                               std::array<uint8_t, 6> mac_address,
                                               NetworkBackend::Mode mode)
    : machine_(machine),
      name_(std::move(name)),
      csr_base_(csr_base),
      csr_size_(csr_size),
      rx_size_(rx_size),
      tx_size_(tx_size),
      irq_(irq),
      backend_(mode),
      rx_buffer_(static_cast<size_t>(rx_size)),
      tx_buffer_(static_cast<size_t>(tx_size)) {
    backend_.set_mac(mac_address);
    csr_window_ = std::make_unique<Window>(*this, machine_, Window::Kind::Csr, csr_base_, csr_size_,
                                           name_);
    rx_window_ = std::make_unique<Window>(*this, machine_, Window::Kind::Rx, rx_base, rx_size_,
                                          name_);
    tx_window_ = std::make_unique<Window>(*this, machine_, Window::Kind::Tx, tx_base, tx_size_,
                                          name_);
}

RingBufferEthernetMmio::~RingBufferEthernetMmio() { machine_.set_platform_irq(irq_, false); }

auto RingBufferEthernetMmio::csr_node() -> memory::TileLinkNode* { return csr_window_.get(); }
auto RingBufferEthernetMmio::rx_node() -> memory::TileLinkNode* { return rx_window_.get(); }
auto RingBufferEthernetMmio::tx_node() -> memory::TileLinkNode* { return tx_window_.get(); }

auto RingBufferEthernetMmio::read_csr(Address offset) -> uint32_t {
    const auto index = static_cast<size_t>(offset / 4);
    if (index >= csr_.size()) return 0;
    if (index == 4) {
        const auto start = csr_[5] & static_cast<uint32_t>(tx_size_ - 1);
        const auto end = csr_[6] & static_cast<uint32_t>(tx_size_ - 1);
        return start != end ? 1U : 0U;
    }
    return csr_[index];
}

void RingBufferEthernetMmio::write_csr(Address offset, uint32_t value) {
    if ((offset & 3U) != 0) return;
    switch (offset / 4) {
        case 0:
            csr_[0] = value & static_cast<uint32_t>(rx_size_ - 1);
            update_rx_irq();
            break;
        case 2:
            csr_[2] = value;
            break;
        case 6:
            csr_[6] = value & static_cast<uint32_t>(tx_size_ - 1) & ~3U;
            drain_tx();
            break;
        default:
            break;
    }
}

auto RingBufferEthernetMmio::read_buffer(bool tx, Address offset) -> uint32_t {
    const auto& buffer = tx ? tx_buffer_ : rx_buffer_;
    if (offset >= buffer.size() || (offset + 4) > buffer.size()) return 0;
    return static_cast<uint32_t>(buffer[static_cast<size_t>(offset)]) |
           (static_cast<uint32_t>(buffer[static_cast<size_t>(offset + 1)]) << 8) |
           (static_cast<uint32_t>(buffer[static_cast<size_t>(offset + 2)]) << 16) |
           (static_cast<uint32_t>(buffer[static_cast<size_t>(offset + 3)]) << 24);
}

void RingBufferEthernetMmio::write_buffer(bool tx, Address offset, uint32_t value) {
    auto& buffer = tx ? tx_buffer_ : rx_buffer_;
    if (offset >= buffer.size() || (offset + 4) > buffer.size()) return;
    for (size_t byte = 0; byte < 4; ++byte) {
        buffer[static_cast<size_t>(offset) + byte] = static_cast<uint8_t>(value >> (byte * 8));
    }
}

void RingBufferEthernetMmio::drain_tx() {
    auto cursor = csr_[5] & static_cast<uint32_t>(tx_size_ - 1) & ~3U;
    const auto end = csr_[6];
    size_t records = 0;
    while (cursor != end && records++ < tx_buffer_.size() / 4) {
        uint32_t length = 0;
        for (size_t byte = 0; byte < 4; ++byte) {
            length |= static_cast<uint32_t>(tx_buffer_[(cursor + byte) % tx_buffer_.size()])
                      << (byte * 8);
        }
        cursor = (cursor + 4) % static_cast<uint32_t>(tx_buffer_.size());
        if (length > 1514 || length > tx_buffer_.size() - 4) {
            csr_[5] = end;
            break;
        }
        std::vector<uint8_t> packet(length);
        for (uint32_t byte = 0; byte < length; ++byte) {
            packet[byte] = tx_buffer_[cursor];
            cursor = (cursor + 1) % static_cast<uint32_t>(tx_buffer_.size());
        }
        cursor = (cursor + 3U) & ~3U;
        cursor &= static_cast<uint32_t>(tx_size_ - 1);
        backend_.send_tx_packet(packet);
    }
    csr_[5] = end;
}

void RingBufferEthernetMmio::receive_packet(std::span<const uint8_t> packet) {
    if (packet.empty() || packet.size() > 1514) return;
    const uint32_t start = csr_[0] & static_cast<uint32_t>(rx_size_ - 1);
    const uint32_t end = csr_[1] & static_cast<uint32_t>(rx_size_ - 1);
    const uint32_t used =
        (end + static_cast<uint32_t>(rx_size_) - start) % static_cast<uint32_t>(rx_size_);
    const uint32_t free = static_cast<uint32_t>(rx_size_) - used - 4;
    if (packet.size() > free) {
        csr_[3] |= 1U;
        return;
    }
    uint32_t cursor = end;
    for (const uint8_t byte : packet) {
        rx_buffer_[cursor] = byte;
        cursor = (cursor + 1) % static_cast<uint32_t>(rx_size_);
    }
    csr_[1] = cursor;
    csr_[3] = 0;
    update_rx_irq();
}

void RingBufferEthernetMmio::update_rx_irq() {
    const bool pending = (csr_[0] & static_cast<uint32_t>(rx_size_ - 1)) !=
                         (csr_[1] & static_cast<uint32_t>(rx_size_ - 1));
    machine_.set_platform_irq(irq_, pending);
}

void RingBufferEthernetMmio::poll_backend() {
    (void)backend_.poll_host_rx();
    while (backend_.has_rx_packet()) receive_packet(backend_.pop_rx_packet());
}

void RingBufferEthernetMmio::reset() {
    csr_.fill(0);
    std::fill(rx_buffer_.begin(), rx_buffer_.end(), 0);
    std::fill(tx_buffer_.begin(), tx_buffer_.end(), 0);
    backend_.reset();
    machine_.set_platform_irq(irq_, false);
}

}  // namespace simrv::device
