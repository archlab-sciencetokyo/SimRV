/**
 * @file Console.cpp
 * @brief SimRV implementation unit.
 */
#include "Console.hpp"

#include <algorithm>
#include <print>
#include <utility>

#include "Machine.hpp"

constexpr Word CONSOLE_MAGIC_VALUE = 0x74726976;
constexpr Word CONSOLE_VERSION = 2;
constexpr Word CONSOLE_DEVICE_ID = 3;
constexpr Word CONSOLE_VENDOR_ID = 0xffff;
constexpr Word CONSOLE_DEVICE_FEATURES = 1;
constexpr Word CONSOLE_CONFIG_GENERATION = 0;
constexpr Word CONSOLE_QUEUE_NUM_MAX = 2;
extern void update_descriptor(Word, Word, int, QueueState*, Byte*);
extern auto load_from_ram(Address addr, int n, Byte* ram) -> Word;
extern void store_to_ram(Address addr, Word data, int n, Byte* ram);

using DescriptorSize = std::integral_constant<std::size_t, 16>;

namespace {
void reset_micro_controller_state(IoController& controller) {
    controller.pc = 0;
    std::ranges::fill(controller.reg, Register{0});
    controller.reg[11] = 0x8000;
}
}  // namespace

void process_console_queue_requests(Byte* mmem, Word q_num, QueueState* qs) {
    Descriptor desc;
    Byte* p;
    auto avail_idx = static_cast<uint16_t>(load_from_ram(qs->AvailLow + 2, 2, mmem));
    while (qs->last_avail_idx != avail_idx) {
        Address adr = qs->AvailLow + 4 + (qs->last_avail_idx & (q_num - 1)) * 2;
        uint16_t desc_idx_header = load_from_ram(adr, 2, mmem);
        Address desc_adr_header = desc_idx_header * DescriptorSize::value + qs->DescLow;

        p = reinterpret_cast<Byte*>(&desc);
        for (std::size_t i = 0; i < DescriptorSize::value; i++) {
            *p = static_cast<Byte>(
                static_cast<uint8_t>(load_from_ram(desc_adr_header + i, 1, mmem)));
            p++;
        }

        for (int i = 0; std::cmp_less(i, desc.len); i++) { /* write to stdout */
            uint8_t d = load_from_ram(desc.adr + i, 1, mmem);
            if (write(fileno(stdout), &d, 1) < 0) std::println("__ ERROR in cons_request!");
        }
        fflush(stdout);

        update_descriptor(desc_idx_header, 0, static_cast<int>(q_num), qs, mmem);
        qs->last_avail_idx++;
    }
}

auto Console::receive_input() -> int {
    Descriptor desc;
    Byte* p;

    QueueState* qs = &Queue[0];
    if (!qs->Ready) return 0;

    auto avail_idx = static_cast<uint16_t>(load_from_ram(qs->AvailLow + 2, 2, mmem));
    if (qs->last_avail_idx == avail_idx) return 0;

    Address adr = qs->AvailLow + 4 + (qs->last_avail_idx & (QueueNum - 1)) * 2;
    uint16_t desc_idx_header = load_from_ram(adr, 2, mmem);
    Address desc_adr_header = desc_idx_header * DescriptorSize::value + qs->DescLow;

    p = reinterpret_cast<Byte*>(&desc);
    for (std::size_t i = 0; i < DescriptorSize::value; i++) {
        *p = static_cast<Byte>(static_cast<uint8_t>(load_from_ram(desc_adr_header + i, 1, mmem)));
        p++;
    }

    constexpr int stdin_fd = 0;
    struct timeval tv;
    fd_set rfds, wfds, efds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    FD_SET(stdin_fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    ssize_t r_len = 0;
    if (select(stdin_fd + 1, &rfds, &wfds, &efds, &tv) > 0 && FD_ISSET(stdin_fd, &rfds)) {
        uint8_t buf;
        r_len = ::read(fileno(stdin), &buf, 1);
        if (r_len != 1) {
            std::println("__ ERROR: in console input");
            exit(0);
        }
        if (buf == 0x11) {
            std::println("\n__ Terminated by Control+'q'");
            return -1;
        }

        store_to_ram(static_cast<Address>(desc.adr), static_cast<Word>(buf), 1, mmem);
        update_descriptor(desc_idx_header, static_cast<Word>(r_len), 2, qs, mmem);  // 2019-08-30
        qs->last_avail_idx++;
    }
    return static_cast<int>(r_len);
}

Console::Console()
    : mmem(nullptr),
      Queue(nullptr),
      DeviceFeaturesSel(0),
      DriverFeatures(0),
      DriverFeaturesSel(0),
      InterruptStatus(0),
      Status(0),
      QueueSel(0),
      QueueNum(0),
      cons_fifo(static_cast<Byte>(0)),
      fifo_en(static_cast<Byte>(0)) {}

auto Console::read(Machine& machine, Address p_addr, Word& rdata) -> bool {
    rdata = mmio_read(offset(p_addr));
    if (machine.s_debugmode) {
        std::println("__ {:10} VIO mem_read  {:08x} {:08x}", machine.cpu.mtime, p_addr, rdata);
    }
    if (machine.s_dlog_mode && machine.s_fp_dlog.is_open()) {
        machine.s_fp_dlog << std::hex << rdata << '\n';
        machine.s_fp_dlog.flush();
    }
    return true;
}

auto Console::write(Machine& machine, Address p_addr, Word wdata) -> bool {
    mmio_write(machine, offset(p_addr), wdata);
    if (machine.s_debugmode) {
        std::println("__ {:10} VIO mem_write {:08x} {:08x}", machine.cpu.mtime, p_addr, wdata);
    }
    return true;
}

auto Console::mmio_read(Address offset) -> Word {
    Word rdata = 0;
    switch (offset) {
        case 0x000:
            rdata = CONSOLE_MAGIC_VALUE;
            break;
        case 0x004:
            rdata = CONSOLE_VERSION;
            break;
        case 0x008:
            rdata = CONSOLE_DEVICE_ID;
            break;
        case 0x00c:
            rdata = CONSOLE_VENDOR_ID;
            break;
        case 0x010:
            rdata = CONSOLE_DEVICE_FEATURES;
            break;
        case 0x034:
            rdata = CONSOLE_QUEUE_NUM_MAX;
            break;
        case 0x0fc:
            rdata = CONSOLE_CONFIG_GENERATION;
            break;
        case 0x044:
            rdata = Queue[QueueSel].Ready;
            break;
        case 0x060:
            rdata = InterruptStatus;
            break;
        case 0x070:
            rdata = Status;
            break;
        default:
            break;  //{ printf("__ Error: console_read() default %x.\n", offset); exit(0); }
    }
    // printf("%8ld:CALL Console READ mem[%x]->%x\n", cpu.mtime, offset, rdata);
    return rdata;
}

void Console::mmio_write(Machine& machine, Address offset, Word wdata) {
    switch (offset) {
        case 0x030:
            QueueSel = wdata;
            break;
        case 0x038:
            QueueNum = wdata;
            break;
        case 0x044:
            Queue[QueueSel].Ready = wdata;
            break;
        case 0x080:
            Queue[QueueSel].DescLow = wdata;
            break;
        case 0x090:
            Queue[QueueSel].AvailLow = wdata;
            break;
        case 0x0a0:
            Queue[QueueSel].UsedLow = wdata;
            break;
        case 0x070:
            Status = wdata;
            break;
        case 0x050: {
            Queue[QueueSel].Notify = wdata;
            if (wdata > 1) {
                std::println("__ ERROR: wrong value console_write()");
                exit(0);
            }
            if (wdata == 1) {
                if (machine.s_use_uc && machine.micro_controller != nullptr) {
                    reset_micro_controller_state(*machine.micro_controller);
                    machine.micro_controller->Qnum = wdata;
                    machine.micro_controller->Mode = 1;
                    machine.micro_controller->Qsel = wdata;
                    while (machine.micro_controller->exec()) {
                    }

                } else {
                    process_console_queue_requests(mmem, wdata, &Queue[wdata]);
                }
            }
            break;
        }
        case 0x064: {
            InterruptStatus &= ~wdata;
            if (InterruptStatus == 0) {
                machine.cpu.plic_set_irq(simrv::virtio::kConsoleIrq, 0);
            }
            break;
        }
        default:
            break;
    }
}

constexpr Word MICRO_CONT_MODE_KEY = 0;
auto Console::MC_receive_input(Machine& machine) -> int {
    int ret;
    if (machine.s_use_uc && machine.micro_controller != nullptr && (MICRO_CONT_MODE_KEY != 0)) {
        reset_micro_controller_state(*machine.micro_controller);
        machine.micro_controller->Qnum = QueueNum;
        machine.micro_controller->Mode = 3;
        machine.micro_controller->cons_fifo = cons_fifo;
        while (machine.micro_controller->exec()) {
        }
        ret = 1;  // KEY_ON;
        InterruptStatus |= 1;
    } else {
        ret = receive_input();
        InterruptStatus |= 1;
    }
    return ret;
}
