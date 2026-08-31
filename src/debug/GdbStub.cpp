/**
 * @file GdbStub.cpp
 * @brief GDB Remote Serial Protocol stub implementation.
 *
 * Register layout presented to GDB (matches riscv:rv32 GDB target):
 *   regs  0-31  : x0-x31 (32-bit each, 4 bytes each)
 *   reg   32    : pc      (32-bit, 4 bytes)
 *   regs  33-64 : f0-f31  (64-bit each, 8 bytes each)
 *
 * All multi-byte values are little-endian in the RSP wire format.
 */
#include "simrv/debug/GdbStub.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <print>
#include <stdexcept>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Types.hpp"

// EBREAK encoding (RV32 uncompressed: 0x00100073)
static constexpr Instruction kEbreak32 = 0x00100073U;

namespace simrv::debug {

// ---------------------------------------------------------------------------
// UniqueFd Implementation
// ---------------------------------------------------------------------------

UniqueFd::~UniqueFd() { reset(); }

void UniqueFd::reset() noexcept {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

GdbStub::GdbStub(uint16_t port) : listen_fd_(::socket(AF_INET, SOCK_STREAM, 0)), port_(port) {
    if (listen_fd_.get() < 0) {
        throw std::runtime_error(std::format("GdbStub: socket() failed: {}", std::strerror(errno)));
    }

    int opt = 1;
    ::setsockopt(listen_fd_.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(listen_fd_.get(), IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(listen_fd_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) <
        0) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        listen_fd_.reset();
        throw std::runtime_error(
            std::format("GdbStub: bind() on port {} failed: {}", port_, std::strerror(errno)));
    }

    if (::listen(listen_fd_.get(), 1) < 0) {
        listen_fd_.reset();
        throw std::runtime_error(std::format("GdbStub: listen() failed: {}", std::strerror(errno)));
    }
}

GdbStub::~GdbStub() { close_connection(); }

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

void GdbStub::wait_for_connection() {
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    conn_fd_ =
        UniqueFd(::accept(listen_fd_.get(), reinterpret_cast<sockaddr*>(&peer),
                          &peer_len));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (conn_fd_.get() < 0) {
        throw std::runtime_error(std::format("GdbStub: accept() failed: {}", std::strerror(errno)));
    }
    int opt = 1;
    ::setsockopt(conn_fd_.get(), IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    std::array<char, INET_ADDRSTRLEN> peer_ip{};
    ::inet_ntop(AF_INET, &peer.sin_addr, peer_ip.data(), peer_ip.size());
    simrv::log::info("GDB connected from {}:{}", peer_ip.data(), ntohs(peer.sin_port));
}

void GdbStub::close_connection() {
    conn_fd_.reset();
    single_step_ = false;
    no_ack_mode_ = false;
}

// ---------------------------------------------------------------------------
// Low-level RSP I/O
// ---------------------------------------------------------------------------

auto GdbStub::recv_char() -> int {
    char c = 0;
    const ssize_t n = ::read(conn_fd_.get(), &c, 1);
    if (n <= 0) {
        close_connection();
        return -1;
    }
    return static_cast<unsigned char>(c);
}

auto GdbStub::recv_packet(std::string& out) -> bool {
    // Eat any '+'/'-' acks
    while (true) {
        const int c = recv_char();
        if (c < 0) {
            return false;
        }
        if (c == '+' || c == '-') {
            continue;
        }
        if (c == '$') {
            break;
        }
        if (c == 0x03) {
            // Ctrl-C interrupt
            out = "\x03";
            return true;
        }
    }

    out.clear();
    uint8_t sum = 0;
    while (true) {
        const int c = recv_char();
        if (c < 0) {
            return false;
        }
        if (c == '#') {
            break;
        }
        out += static_cast<char>(c);
        sum += static_cast<uint8_t>(c);
    }

    // Read the two checksum hex digits
    const int hi = recv_char();
    const int lo = recv_char();
    if (hi < 0 || lo < 0) {
        return false;
    }

    auto hex_digit = [](int ch) -> uint8_t {
        if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
        if (ch >= 'a' && ch <= 'f') return static_cast<uint8_t>(ch - 'a' + 10);
        if (ch >= 'A' && ch <= 'F') return static_cast<uint8_t>(ch - 'A' + 10);
        return 0;
    };

    const auto recv_sum = static_cast<uint8_t>((hex_digit(hi) << 4) | hex_digit(lo));

    if (!no_ack_mode_) {
        const char ack = (recv_sum == sum) ? '+' : '-';
        ::write(conn_fd_.get(), &ack, 1);
    }

    return recv_sum == sum;
}

auto GdbStub::checksum(const std::string& data) -> uint8_t {
    uint8_t s = 0;
    for (const char c : data) {
        s += static_cast<uint8_t>(c);
    }
    return s;
}

void GdbStub::send_raw(const std::string& s) {
    std::size_t sent = 0;
    while (sent < s.size()) {
        const ssize_t n = ::write(conn_fd_.get(), s.data() + sent, s.size() - sent);
        if (n <= 0) {
            close_connection();
            return;
        }
        sent += static_cast<std::size_t>(n);
    }
}

void GdbStub::send_packet(const std::string& data) {
    const uint8_t cs = checksum(data);
    const std::string pkt = std::format("${}#{:02x}", data, cs);
    send_raw(pkt);
}

// ---------------------------------------------------------------------------
// Register encoding helpers (little-endian hex)
// ---------------------------------------------------------------------------

template <typename T>
[[nodiscard]] auto to_le_hex(T val) -> std::string {
    std::string s;
    s.reserve(sizeof(T) * 2);
    for (std::size_t b = 0; b < sizeof(T); ++b) {
        s += std::format("{:02x}", static_cast<uint8_t>(val >> (b * 8)));
    }
    return s;
}

auto GdbStub::reg_to_hex(Register val) -> std::string { return to_le_hex(val); }

static auto fp_to_hex(uint64_t val) -> std::string { return to_le_hex(val); }

auto GdbStub::hex_to_reg(const std::string& s, std::size_t offset) -> Register {
    auto hd = [&](std::size_t i) -> Register {
        if (offset + i >= s.size()) return 0;
        const char c = s.at(offset + i);
        if (c >= '0' && c <= '9') return static_cast<Register>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<Register>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<Register>(c - 'A' + 10);
        return 0;
    };
    Register val = 0;
    for (size_t b = 0; b < sizeof(Register); ++b) {
        const Register byte_val = (hd(b * 2) << 4) | hd(b * 2 + 1);
        val |= (byte_val << (b * 8));
    }
    return val;
}

// ---------------------------------------------------------------------------
// Register read/write helpers
// ---------------------------------------------------------------------------

// GDB register index -> value from ArchState
static auto read_gdb_reg(std::size_t idx, const simrv::core::ArchState& state)
    -> std::optional<std::string> {
    if (idx < 32) {
        return GdbStub::reg_to_hex(state.regs.read(static_cast<RegId>(idx)));
    }
    if (idx == 32) {
        return GdbStub::reg_to_hex(state.pc);
    }
    if (idx >= 33 && idx < 65) {
        // FP registers: 64-bit, little-endian
        return fp_to_hex(state.regs.read_fp(static_cast<RegId>(idx - 33)));
    }
    return std::nullopt;
}

static void write_gdb_reg(std::size_t idx, const std::string& hex, std::size_t hex_off,
                          simrv::core::ArchState& state) {
    if (idx < 32) {
        const Register val = GdbStub::hex_to_reg(hex, hex_off);
        state.regs.write(static_cast<RegId>(idx), val);
    } else if (idx == 32) {
        state.pc = static_cast<Address>(GdbStub::hex_to_reg(hex, hex_off));
    }
    // FP write not yet implemented
}

// ---------------------------------------------------------------------------
// RSP command handlers
// ---------------------------------------------------------------------------

void GdbStub::cmd_read_registers(simrv::core::Machine& machine) {
    auto& target_cpu = (current_hart_.raw() < machine.num_harts()) ? machine.hart(current_hart_)
                                                                   : machine.primary_hart();
    const auto& state = target_cpu.state();
    std::string resp;
    // x0-x31 (XLEN bytes each, little-endian)
    for (std::size_t i = 0; i < 32; ++i) {
        resp += reg_to_hex(state.regs.read(static_cast<RegId>(i)));
    }
    // pc
    resp += reg_to_hex(state.pc);
    // f0-f31 (8 bytes each, little-endian)
    for (std::size_t i = 0; i < 32; ++i) {
        resp += fp_to_hex(state.regs.read_fp(static_cast<RegId>(i)));
    }
    send_packet(resp);
}

void GdbStub::cmd_write_registers(const std::string& pkt, simrv::core::Machine& machine) {
    // G<hex data>
    auto& target_cpu = (current_hart_.raw() < machine.num_harts()) ? machine.hart(current_hart_)
                                                                   : machine.primary_hart();
    auto& state = target_cpu.state();
    const size_t hex_per_reg = sizeof(Register) * 2;
    std::size_t off = 1;
    for (std::size_t i = 0; i < 33 && off + hex_per_reg <= pkt.size(); ++i, off += hex_per_reg) {
        write_gdb_reg(i, pkt, off, state);
    }
    send_packet("OK");
}

void GdbStub::cmd_read_register(const std::string& pkt, simrv::core::Machine& machine) {
    // p n
    const std::size_t idx = std::stoul(pkt.substr(1), nullptr, 16);
    auto& target_cpu = (current_hart_.raw() < machine.num_harts()) ? machine.hart(current_hart_)
                                                                   : machine.primary_hart();
    const auto result = read_gdb_reg(idx, target_cpu.state());
    if (result) {
        send_packet(*result);
    } else {
        send_packet("E01");
    }
}

void GdbStub::cmd_write_register(const std::string& pkt, simrv::core::Machine& machine) {
    // P n=v
    const std::size_t eq = pkt.find('=');
    if (eq == std::string::npos || eq + 1 >= pkt.size()) {
        send_packet("E01");
        return;
    }
    const std::size_t idx = std::stoul(pkt.substr(1, eq - 1), nullptr, 16);
    auto& target_cpu = (current_hart_.raw() < machine.num_harts()) ? machine.hart(current_hart_)
                                                                   : machine.primary_hart();
    write_gdb_reg(idx, pkt, eq + 1, target_cpu.state());
    send_packet("OK");
}

void GdbStub::cmd_read_memory(const std::string& pkt, simrv::core::Machine& machine) {
    // m addr,len
    const std::size_t comma = pkt.find(',');
    if (comma == std::string::npos) {
        send_packet("E01");
        return;
    }
    const auto addr = static_cast<Address>(std::stoull(pkt.substr(1, comma - 1), nullptr, 16));
    const auto len = static_cast<size_t>(std::stoul(pkt.substr(comma + 1), nullptr, 16));

    // Clamp to a safe maximum
    const size_t safe_len = std::min(len, size_t{4096});

    std::string resp;
    resp.reserve(safe_len * 2);

    // Direct physical memory read (bypasses MMU)
    const auto ram = machine.ram_view();

    for (size_t i = 0; i < safe_len; ++i) {
        const auto phys = static_cast<uint64_t>(addr) + i;
        uint8_t byte_val = 0;
        if (ram.contains(phys)) {
            byte_val = static_cast<uint8_t>(*ram.unchecked_ptr(phys));
        }
        resp += std::format("{:02x}", byte_val);
    }
    send_packet(resp);
}

void GdbStub::cmd_write_memory(const std::string& pkt, simrv::core::Machine& machine) {
    // M addr,len:data
    const std::size_t comma = pkt.find(',');
    const std::size_t colon = pkt.find(':');
    if (comma == std::string::npos || colon == std::string::npos) {
        send_packet("E01");
        return;
    }
    const auto addr = static_cast<Address>(std::stoull(pkt.substr(1, comma - 1), nullptr, 16));
    const auto len =
        static_cast<size_t>(std::stoul(pkt.substr(comma + 1, colon - comma - 1), nullptr, 16));

    const auto ram = machine.ram_view();

    auto hd = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
        return 0;
    };

    for (size_t i = 0; i < len; ++i) {
        const auto hex_off = colon + 1 + i * 2;
        if (hex_off + 1 >= pkt.size()) break;
        const auto byte_val =
            static_cast<uint8_t>((hd(pkt.at(hex_off)) << 4) | hd(pkt.at(hex_off + 1)));
        const auto phys = static_cast<uint64_t>(addr) + i;
        if (ram.contains(phys)) {
            *ram.unchecked_ptr(phys) = static_cast<Byte>(byte_val);
        }
    }
    send_packet("OK");
}

void GdbStub::cmd_insert_breakpoint(const std::string& pkt, simrv::core::Machine& machine) {
    // Z0,addr,4
    const std::size_t c1 = pkt.find(',');
    if (c1 == std::string::npos) {
        send_packet("E01");
        return;
    }
    const std::size_t c2 = pkt.find(',', c1 + 1);
    if (c2 == std::string::npos) {
        send_packet("E01");
        return;
    }

    const auto type = static_cast<uint32_t>(std::stoul(pkt.substr(1, c1 - 1), nullptr, 16));
    if (type != std::to_underlying(GdbBreakpointType::Software)) {
        send_packet("");
        return;
    }  // only sw breakpoints

    const auto addr =
        static_cast<Address>(std::stoull(pkt.substr(c1 + 1, c2 - c1 - 1), nullptr, 16));

    const auto phys = static_cast<uint64_t>(addr);

    const auto ram = machine.ram_view();
    if (!ram.contains(phys, sizeof(Instruction))) {
        send_packet("E02");
        return;
    }
    auto* ptr = ram.unchecked_ptr(phys);

    // Save original word
    Instruction orig = 0;
    std::memcpy(&orig, ptr, sizeof(Instruction));
    sw_breakpoints_[addr] = orig;

    // Write EBREAK
    const Instruction ebrk = kEbreak32;
    std::memcpy(ptr, &ebrk, sizeof(Instruction));

    send_packet("OK");
}

void GdbStub::cmd_remove_breakpoint(const std::string& pkt, simrv::core::Machine& machine) {
    // z0,addr,4
    const std::size_t c1 = pkt.find(',');
    if (c1 == std::string::npos) {
        send_packet("E01");
        return;
    }
    const std::size_t c2 = pkt.find(',', c1 + 1);
    if (c2 == std::string::npos) {
        send_packet("E01");
        return;
    }

    const auto type = static_cast<uint32_t>(std::stoul(pkt.substr(1, c1 - 1), nullptr, 16));
    if (type != std::to_underlying(GdbBreakpointType::Software)) {
        send_packet("");
        return;
    }

    const auto addr =
        static_cast<Address>(std::stoull(pkt.substr(c1 + 1, c2 - c1 - 1), nullptr, 16));

    auto it = sw_breakpoints_.find(addr);
    if (it == sw_breakpoints_.end()) {
        send_packet("E03");
        return;
    }

    const auto phys = static_cast<uint64_t>(addr);
    const auto ram = machine.ram_view();
    if (ram.contains(phys, sizeof(Instruction))) {
        auto* ptr = ram.unchecked_ptr(phys);
        std::memcpy(ptr, &it->second, sizeof(Instruction));
    }
    sw_breakpoints_.erase(it);
    send_packet("OK");
}

// ---------------------------------------------------------------------------
// Query packet handler
// ---------------------------------------------------------------------------

void GdbStub::handle_query(const std::string& pkt, simrv::core::Machine& machine) {
    if (pkt == "qSupported") {
        send_packet("PacketSize=4000;QStartNoAckMode+;swbreak+;multiprocess+");
        return;
    }
    if (pkt == "QStartNoAckMode") {
        no_ack_mode_ = true;
        send_packet("OK");
        return;
    }
    if (pkt == "qAttached") {
        send_packet("1");
        return;
    }
    if (pkt.starts_with("qSupported:")) {
        send_packet("PacketSize=4000;QStartNoAckMode+;swbreak+;multiprocess+");
        return;
    }
    if (pkt == "qC") {
        send_packet(std::format("QC{:x}", current_hart_.raw() + 1));
        return;
    }
    if (pkt == "qfThreadInfo") {
        std::string threads = "m";
        for (size_t i = 1; i <= machine.num_harts(); ++i) {
            if (i > 1) threads += ",";
            threads += std::format("{:x}", i);
        }
        send_packet(threads);
        return;
    }
    if (pkt == "qsThreadInfo") {
        send_packet("l");
        return;
    }
    if (pkt == "qOffsets") {
        send_packet("Text=0;Data=0;Bss=0");
        return;
    }
    if (pkt == "qSymbol::") {
        send_packet("OK");
        return;
    }
    // Unrecognised query
    send_packet("");
}

// ---------------------------------------------------------------------------
// Main packet dispatcher
// ---------------------------------------------------------------------------

auto GdbStub::handle_packet(const std::string& pkt, simrv::core::Machine& machine) -> bool {
    if (pkt.empty()) {
        send_packet("");
        return false;
    }

    // Ctrl-C interrupt
    if (pkt.front() == '\x03') {
        send_packet("S05");  // SIGTRAP
        return false;
    }

    switch (pkt.front()) {
        case '?':
            send_packet("S05");  // SIGTRAP
            return false;

        case 'g':
            cmd_read_registers(machine);
            return false;

        case 'G':
            cmd_write_registers(pkt, machine);
            return false;

        case 'p':
            cmd_read_register(pkt, machine);
            return false;

        case 'P':
            cmd_write_register(pkt, machine);
            return false;

        case 'm':
            cmd_read_memory(pkt, machine);
            return false;

        case 'M':
            cmd_write_memory(pkt, machine);
            return false;

        case 'Z':
            cmd_insert_breakpoint(pkt, machine);
            return false;

        case 'z':
            cmd_remove_breakpoint(pkt, machine);
            return false;

        case 'c':
            // Continue: resume simulation
            single_step_ = false;
            return true;  // signal caller to resume

        case 's':
            // Single step: execute one instruction then halt again
            single_step_ = true;
            return true;

        case 'D':
            // Detach
            send_packet("OK");
            close_connection();
            return true;

        case 'k':
            // Kill
            machine.stop();
            close_connection();
            return true;

        case 'H': {
            if (pkt.size() > 2) {
                const long tid = std::stol(pkt.substr(2), nullptr, 16);
                if (tid > 0 && static_cast<size_t>(tid) <= machine.num_harts()) {
                    current_hart_ = HartId{static_cast<uint32_t>(tid - 1)};
                } else if (tid == 0 || tid == -1) {
                    current_hart_ = HartId{0};
                }
            }
            send_packet("OK");
            return false;
        }

        case 'T': {
            if (pkt.size() > 1) {
                const long tid = std::stol(pkt.substr(1), nullptr, 16);
                if (tid > 0 && static_cast<size_t>(tid) <= machine.num_harts()) {
                    send_packet("OK");
                    return false;
                }
            }
            send_packet("E01");
            return false;
        }

        case 'v':
            if (pkt == "vCont?") {
                send_packet("vCont;c;C;s;S");
                return false;
            }
            if (pkt.starts_with("vCont;c")) {
                single_step_ = false;
                return true;
            }
            if (pkt.starts_with("vCont;s")) {
                single_step_ = true;
                return true;
            }
            send_packet("");
            return false;

        case 'q':
        case 'Q':
            handle_query(pkt, machine);
            return false;

        default:
            send_packet("");
            return false;
    }
}

// ---------------------------------------------------------------------------
// Public interface: poll (non-blocking) and notify_breakpoint (blocking)
// ---------------------------------------------------------------------------

void GdbStub::poll(simrv::core::Machine& machine) {
    if (!is_connected()) return;

    // Non-blocking check using poll(2)
    struct pollfd pfd{};
    pfd.fd = conn_fd_.get();
    pfd.events = POLLIN;
    const int rc = ::poll(&pfd, 1, 0);
    if (rc <= 0) return;

    std::string pkt;
    if (!recv_packet(pkt)) return;

    static_cast<void>(handle_packet(pkt, machine));
}

void GdbStub::notify_breakpoint(simrv::core::Machine& machine) {
    if (!is_connected()) return;

    // Notify GDB that we stopped
    send_packet("S05");  // SIGTRAP

    // Block in the command loop until 'c' or 's'
    while (is_connected()) {
        std::string pkt;
        if (!recv_packet(pkt)) break;
        const bool resume = handle_packet(pkt, machine);
        if (resume) break;
    }
}

}  // namespace simrv::debug
