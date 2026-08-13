/**
 * @file Console.cpp
 * @brief Virtio TTY console implementation.
 */
#include "simrv/device/Console.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "simrv/core/Machine.hpp"
#include "simrv/device/Virtio.hpp"
#include "simrv/device/VirtioUtil.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::device {

Console::Console(simrv::core::Machine& machine)
    : VirtioDevice(machine, virtio::kConsoleIrq, virtio::kConsoleMaxQueueNum) {}

void Console::push_input_byte(uint8_t byte) {
    std::scoped_lock lock(rx_pending_mutex_);
    rx_pending_.push(byte);
}

auto Console::pop_pending_input() -> bool {
    std::scoped_lock lock(rx_pending_mutex_);
    if (rx_pending_.empty()) return false;
    cons_fifo = static_cast<Byte>(rx_pending_.front());
    rx_pending_.pop();
    fifo_en = static_cast<Byte>(1);
    return true;
}

void Console::process_queue(Word q_idx) {
    if (q_idx >= virtio::kConsoleMaxQueueNum) return;
    if (q_idx == 1) {  // TX Queue
        virtio::QueueState& qs = Queue[q_idx];
        if (qs.Ready == 0) return;
        const auto avail_idx = virtio_detail::read_struct_from_ram<uint16_t>(qs.AvailLow + 2, mmem);
        bool written = false;
        while (static_cast<uint16_t>(qs.last_avail_idx) != avail_idx) {
            const auto desc_idx = virtio_detail::next_avail_desc_idx(&qs, QueueNum, mmem);
            const auto desc_addr = virtio_detail::get_desc_addr(desc_idx, &qs);
            auto desc = virtio_detail::read_struct_from_ram<virtio::Descriptor>(desc_addr, mmem);

            for (Word i = 0; i < desc.len; ++i) {
                const char c =
                    static_cast<char>(virtio_detail::load_from_ram(desc.adr + i, 1, mmem));
                if (machine_.s_tuimode && machine_.tui) {
                    machine_.tui->handle_char_write(c);
                } else {
                    if (::write(STDOUT_FILENO, &c, 1) < 0) {
                    }
                }
            }
            written = true;
            virtio_detail::update_descriptor(desc_idx, desc.len, static_cast<int>(QueueNum), &qs,
                                             mmem);
            qs.last_avail_idx++;
        }
        if (written) {
            trigger_interrupt();
        }
    }
}

auto Console::MC_receive_input(simrv::core::Machine& machine) -> int {
    if (machine.s_tuimode) return 0;
    if (Status == 0) return 0;

    // Check if terminal data is available via poll without blocking
    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        char ch = 0;
        if (::read(STDIN_FILENO, &ch, 1) > 0) {
            if (ch == 0x11) {  // Ctrl+Q break sequence
                return -1;
            }

            virtio::QueueState& qs = Queue[0];  // RX Queue
            if (qs.Ready != 0) {
                const auto avail_idx =
                    virtio_detail::read_struct_from_ram<uint16_t>(qs.AvailLow + 2, mmem);

                // If the guest OS has supplied RX buffers, fill one.
                if (static_cast<uint16_t>(qs.last_avail_idx) != avail_idx) {
                    const auto desc_idx = virtio_detail::next_avail_desc_idx(&qs, QueueNum, mmem);
                    const auto desc_addr = virtio_detail::get_desc_addr(desc_idx, &qs);
                    auto desc =
                        virtio_detail::read_struct_from_ram<virtio::Descriptor>(desc_addr, mmem);

                    virtio_detail::store_to_ram(desc.adr, static_cast<Word>(ch), 1, mmem);
                    virtio_detail::update_descriptor(desc_idx, 1, static_cast<int>(QueueNum), &qs,
                                                     mmem);
                    qs.last_avail_idx++;

                    trigger_interrupt();
                    return 1;  // Trigger IRQ in Machine.cpp
                }
            }
        }
    }

    // If synthetic input (cons_fifo) was populated via test automation, process it
    if (fifo_en != static_cast<Byte>(0)) {
        virtio::QueueState& qs = Queue[0];
        if (qs.Ready != 0) {
            const auto avail_idx =
                virtio_detail::read_struct_from_ram<uint16_t>(qs.AvailLow + 2, mmem);
            if (static_cast<uint16_t>(qs.last_avail_idx) != avail_idx) {
                const auto desc_idx = virtio_detail::next_avail_desc_idx(&qs, QueueNum, mmem);
                const auto desc_addr = virtio_detail::get_desc_addr(desc_idx, &qs);
                auto desc =
                    virtio_detail::read_struct_from_ram<virtio::Descriptor>(desc_addr, mmem);

                virtio_detail::store_to_ram(desc.adr, static_cast<Word>(cons_fifo), 1, mmem);
                virtio_detail::update_descriptor(desc_idx, 1, static_cast<int>(QueueNum), &qs,
                                                 mmem);
                qs.last_avail_idx++;

                fifo_en = static_cast<Byte>(0);
                trigger_interrupt();
                return 1;
            }
        }
    }

    return 0;
}

}  // namespace simrv::device
