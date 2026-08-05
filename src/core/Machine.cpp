/**
 * @file Machine.cpp
 * @brief Machine top-level orchestration and cycle-loop implementation.
 */
#include "simrv/core/Machine.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>

#include "simrv/core/Logger.hpp"
#include "simrv/device/InputDevice.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {
bool g_appmode = true;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
Address g_dram_base =
    kDramBaseAddress;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}  // namespace simrv::memory

namespace simrv::core {
Machine::Machine() : memory_(*this) { cpu.machine_ = this; }

void Machine::reset_state() {
    tohost = 0;
    reboot_requested = false;
    exit_code = 0;
    is_shutdown_ = false;
    is_running_ = true;
    cpu.reset();
}

auto Machine::is_paused() const -> bool { return s_tuimode && tui && tui->is_paused(); }

void Machine::pause() {
    if (s_tuimode && tui) {
        tui->pause_loop();
    }
}

void Machine::resume() {
    if (s_tuimode && tui) {
        tui->unpause_loop();
    }
}

void Machine::stop() {
    is_shutdown_ = true;
    if (s_tuimode && tui) {
        tui->pause_loop();
    } else {
        is_running_ = false;
    }
}

void Machine::finalize_cycle_tohost() {
    if (tohost == 0) {
        return;
    }

    // Standard 64-bit HTIF handling
    const auto dev = static_cast<uint8_t>(tohost >> 56);
    const auto cmd = static_cast<uint8_t>(tohost >> 48);
    const uint64_t payload = tohost & 0x0000FFFFFFFFFFFFULL;

    if (dev == 1 && cmd == 1) {
        // HTIF Console Print
        if (s_tuimode && tui) {
            tui->handle_char_write(static_cast<char>(payload & 0xff));
        } else {
            std::print("{}", static_cast<char>(payload & 0xff));
            fflush(stdout);
        }
        tohost = 0;
        return;
    }

    // Compatibility for older 32-bit SimRV HTIF protocol:
    // writes of ((CMD_PRINT_CHAR << 16) | c) or (CMD_POWER_OFF << 16)
    if (dev == 0 && cmd == 0) {
        const auto old_cmd = static_cast<uint16_t>(tohost >> 16);
        const auto old_payload = static_cast<uint16_t>(tohost & 0xffffULL);
        if (old_cmd == 1) {  // CMD_PRINT_CHAR
            const char ch = static_cast<char>(old_payload & 0xff);
            if (s_tuimode && tui) {
                tui->handle_char_write(ch);
            } else {
                std::print("{}", ch);
                fflush(stdout);
            }
            tohost = 0;
            return;
        } else if (old_cmd == 2) {  // CMD_POWER_OFF
            simrv::log::info(
                "[Power] Compatibility: guest requested poweroff via tohost (old protocol).");
            exit_code = 0;
            stop();
            tohost = 0;
            return;
        } else {
            // HTIF Syscall handling: payload is a pointer to the syscall block in guest DRAM
            if (payload >= 0x80000000ULL && payload < (0x80000000ULL + memory::kDramSize)) {
                const Address masked_payload = payload & simrv::memory::kDramMask;
                uint64_t syscall_num = 0;
                uint64_t arg0 = 0;
                uint64_t arg1 = 0;
                uint64_t arg2 = 0;

                std::memcpy(&syscall_num, mmem + masked_payload + 0, 8);
                std::memcpy(&arg0, mmem + masked_payload + 8, 8);
                std::memcpy(&arg1, mmem + masked_payload + 16, 8);
                std::memcpy(&arg2, mmem + masked_payload + 24, 8);

                if (syscall_num == 64) {  // SYS_write
                    const Address buf_masked = arg1 & simrv::memory::kDramMask;
                    for (uint64_t i = 0; i < arg2; ++i) {
                        char ch = static_cast<char>(mmem[buf_masked + i]);
                        if (s_tuimode && tui) {
                            tui->handle_char_write(ch);
                        } else {
                            std::print("{}", ch);
                        }
                    }
                    if (!s_tuimode) {
                        std::fflush(stdout);
                    }

                    // Write success response (bytes written) to fromhost
                    const Address fromhost_addr =
                        (s_isatest_tohost != 0 ? s_isatest_tohost : 0x80001000) + 8;
                    const Address fromhost_masked = fromhost_addr & simrv::memory::kDramMask;
                    uint64_t resp = arg2;
                    std::memcpy(mmem + fromhost_masked, &resp, 8);
                    tohost = 0;
                    return;
                } else if (syscall_num == 93) {  // SYS_exit
                    const int code = static_cast<int>(arg0);
                    if (s_appmode) {
                        if (code == 0) {
                            simrv::log::info("ISA TEST PASS");
                        } else {
                            simrv::log::error("ISA TEST FAIL code={}", code);
                        }
                    } else {
                        if (code == 0) {
                            simrv::log::info("Program Halted (SUCCESS / PASS)");
                        } else {
                            simrv::log::error("Program Halted (FAIL / EXIT code={})", code);
                        }
                    }
                    exit_code = code;
                    stop();
                    tohost = 0;
                    return;
                }
            }
        }
    }

    // Universal tohost halting check (e.g. exit code via tohost)
    if (tohost == 1) {
        if (s_appmode) {
            simrv::log::info("ISA TEST PASS");
        } else {
            simrv::log::info("Program Halted (SUCCESS / PASS)");
        }
        exit_code = 0;
        stop();
        tohost = 0;
        return;
    } else if ((tohost & 1) != 0u) {
        const int code = static_cast<int>(tohost >> 1);
        if (s_appmode) {
            simrv::log::error("ISA TEST FAIL code={} (tohost=0x{:016x})", code, tohost);
        } else {
            simrv::log::error("Program Halted (FAIL / EXIT code={})", code);
        }
        exit_code = code == 0 ? 1 : code;
        stop();
        tohost = 0;
        return;
    }
}

Machine::~Machine() = default;

}  // namespace simrv::core
