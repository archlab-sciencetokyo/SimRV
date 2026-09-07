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
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::debug {

namespace {
auto valid_hex(std::string_view text) -> bool {
    return !text.empty() && std::ranges::all_of(text, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}
auto parse_hex(std::string_view text) -> std::optional<uint64_t> {
    if (!valid_hex(text)) return std::nullopt;
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

void invalidate_debug_state(simrv::core::Machine& machine, simrv::core::CPU& cpu) {
    const auto hart = HartId{static_cast<uint32_t>(cpu.state().mhartid)};
    machine.memory().system_bus().cancel_source(
        simrv::memory::make_tl_source(hart, simrv::memory::TlPort::Instruction));
    machine.memory().system_bus().cancel_source(
        simrv::memory::make_tl_source(hart, simrv::memory::TlPort::Data));
    cpu.ca_pipeline.reset();
    cpu.ca_state.reset_instruction();
    cpu.TLB_flush();
    cpu.icache.flush();
    cpu.dcache.flush();
}
}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

GdbStub::GdbStub(uint16_t port)
    : listen_fd_(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)),
      wake_fd_(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
      port_(port) {
    if (listen_fd_.get() < 0) {
        throw std::runtime_error(std::format("GdbStub: socket() failed: {}", std::strerror(errno)));
    }
    if (wake_fd_.get() < 0) {
        throw std::runtime_error(
            std::format("GdbStub: eventfd() failed: {}", std::strerror(errno)));
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

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    if (::getsockname(listen_fd_.get(), reinterpret_cast<sockaddr*>(&bound), &bound_len) < 0) {
        throw std::runtime_error(
            std::format("GdbStub: getsockname() failed: {}", std::strerror(errno)));
    }
    port_ = ntohs(bound.sin_port);
}

GdbStub::~GdbStub() { stop(); }

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

void GdbStub::start(std::function<void()> wake_machine) {
    if (running_.exchange(true, std::memory_order_acq_rel)) return;
    wake_machine_ = std::move(wake_machine);
    state_.store(GdbConnectionState::Listening, std::memory_order_release);
    worker_ = std::jthread([this](const std::stop_token& stop_token) { worker_loop(stop_token); });
}

void GdbStub::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel) && !worker_.joinable()) return;
    worker_.request_stop();
    if (const int fd = connected_fd_.load(std::memory_order_acquire); fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
    }
    signal_worker();
    command_cv_.notify_all();
    if (wake_machine_) wake_machine_();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
    close_connection();
    state_.store(GdbConnectionState::Stopped, std::memory_order_release);
    simrv::log::info("GDB server shut down");
}

void GdbStub::signal_worker() noexcept {
    const uint64_t one = 1;
    const ssize_t ignored = ::write(wake_fd_.get(), &one, sizeof(one));
    static_cast<void>(ignored);
}

void GdbStub::drain_worker_signal() noexcept {
    uint64_t value = 0;
    while (::read(wake_fd_.get(), &value, sizeof(value)) == sizeof(value)) {
    }
}

auto GdbStub::wait_for_client(const std::stop_token& stop_token) -> bool {
    pollfd fds[2]{{.fd = listen_fd_.get(), .events = POLLIN, .revents = 0},
                  {.fd = wake_fd_.get(), .events = POLLIN, .revents = 0}};
    while (running_.load(std::memory_order_acquire) && !stop_token.stop_requested()) {
        const int ready = ::poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if ((fds[1].revents & POLLIN) != 0) {
            drain_worker_signal();
            if (!running_.load(std::memory_order_acquire)) return false;
        }
        if ((fds[0].revents & POLLIN) == 0) continue;

        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        conn_fd_ = util::UniqueFd(
            ::accept(listen_fd_.get(), reinterpret_cast<sockaddr*>(&peer),
                     &peer_len));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        if (conn_fd_.get() < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        int opt = 1;
        ::setsockopt(conn_fd_.get(), IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        std::array<char, INET_ADDRSTRLEN> peer_ip{};
        ::inet_ntop(AF_INET, &peer.sin_addr, peer_ip.data(), peer_ip.size());
        simrv::log::info("GDB connected from {}:{}", peer_ip.data(), ntohs(peer.sin_port));
        connected_fd_.store(conn_fd_.get(), std::memory_order_release);
        state_.store(GdbConnectionState::Connected, std::memory_order_release);
        no_ack_mode_.store(false, std::memory_order_release);
        request_pause();
        static_cast<void>(submit_command(CommandKind::Attach));
        return running_.load(std::memory_order_acquire);
    }
    return false;
}

void GdbStub::close_connection() {
    connected_fd_.store(-1, std::memory_order_release);
    if (conn_fd_) ::shutdown(conn_fd_.get(), SHUT_RDWR);
    conn_fd_.reset();
    no_ack_mode_.store(false, std::memory_order_release);
    {
        const std::lock_guard lock(outbound_mutex_);
        outbound_packets_.clear();
    }
    if (running_.load(std::memory_order_acquire)) {
        state_.store(GdbConnectionState::Listening, std::memory_order_release);
    }
}

// ---------------------------------------------------------------------------
// Low-level RSP I/O
// ---------------------------------------------------------------------------

auto GdbStub::recv_char() -> int {
    char c = 0;
    while (running_.load(std::memory_order_acquire)) {
        pollfd fds[2]{{.fd = conn_fd_.get(), .events = POLLIN, .revents = 0},
                      {.fd = wake_fd_.get(), .events = POLLIN, .revents = 0}};
        if (::poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if ((fds[1].revents & POLLIN) != 0) {
            drain_worker_signal();
            if (!running_.load(std::memory_order_acquire)) return -1;
            flush_outbound_packets();
        }
        if ((fds[0].revents & POLLIN) != 0) {
            const auto n = ::read(conn_fd_.get(), &c, 1);
            if (n < 0 && errno == EINTR) continue;
            return n == 1 ? static_cast<unsigned char>(c) : -1;
        }
        if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) return -1;
    }
    return -1;
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
        if (out.size() >= 16384) {
            errno = EMSGSIZE;
            return false;
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

    const bool valid_checksum =
        valid_hex(std::string{static_cast<char>(hi), static_cast<char>(lo)});
    const auto recv_sum = static_cast<uint8_t>((hex_digit(hi) << 4) | hex_digit(lo));

    if (!no_ack_mode_.load(std::memory_order_acquire)) {
        const char ack = (valid_checksum && recv_sum == sum) ? '+' : '-';
        if (!send_raw(std::string(1, ack))) return false;
    }

    if (!valid_checksum || recv_sum != sum) {
        errno = EBADMSG;
        return false;
    }
    return true;
}

auto GdbStub::checksum(const std::string& data) -> uint8_t {
    uint8_t s = 0;
    for (const char c : data) {
        s += static_cast<uint8_t>(c);
    }
    return s;
}

auto GdbStub::send_raw(const std::string& s) -> bool {
    std::size_t sent = 0;
    while (sent < s.size()) {
        const ssize_t n = ::send(conn_fd_.get(), s.data() + sent, s.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

auto GdbStub::send_packet_wire(const std::string& data) -> bool {
    const uint8_t cs = checksum(data);
    const std::string pkt = std::format("${}#{:02x}", data, cs);
    return send_raw(pkt);
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

static auto hex_to_fp(const std::string& s, std::size_t offset) -> uint64_t {
    auto hd = [&](std::size_t i) -> uint64_t {
        if (offset + i >= s.size()) return 0;
        const char c = s.at(offset + i);
        if (c >= '0' && c <= '9') return static_cast<uint64_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint64_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<uint64_t>(c - 'A' + 10);
        return 0;
    };
    uint64_t val = 0;
    for (size_t b = 0; b < sizeof(uint64_t); ++b) {
        const uint64_t byte_val = (hd(b * 2) << 4) | hd(b * 2 + 1);
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
    } else if (idx >= 33 && idx < 65) {
        const uint64_t val = hex_to_fp(hex, hex_off);
        state.regs.write_fp(static_cast<RegId>(idx - 33), val);
    }
}

// ---------------------------------------------------------------------------
// RSP command handlers
// ---------------------------------------------------------------------------

auto GdbStub::cmd_read_registers(simrv::core::Machine& machine) -> std::string {
    auto& target_cpu = (current_hart_.raw() < machine.num_harts()) ? machine.hart(current_hart_)
                                                                   : machine.primary_hart();
    const auto& state = target_cpu.state();
    std::string resp;
    // x0-x31 (XLEN bytes each, little-endian)
    for (std::size_t i = 0; i < 32; ++i) {
        resp += reg_to_hex(state.regs.read(static_cast<RegId>(i)));
    }
    // pc (XLEN bytes, little-endian)
    resp += reg_to_hex(state.pc);
    // f0-f31 (8 bytes each, little-endian)
    for (std::size_t i = 0; i < 32; ++i) {
        resp += fp_to_hex(state.regs.read_fp(static_cast<RegId>(i)));
    }
    return resp;
}

auto GdbStub::cmd_write_registers(const std::string& pkt, simrv::core::Machine& machine)
    -> std::string {
    if (pkt.size() != 1 + 33 * sizeof(Register) * 2 + 32 * 16 ||
        !valid_hex(std::string_view(pkt).substr(1))) {
        return "E01";
    }
    // G<hex data>
    auto& target_cpu = (current_hart_.raw() < machine.num_harts()) ? machine.hart(current_hart_)
                                                                   : machine.primary_hart();
    auto& state = target_cpu.state();
    const size_t hex_per_int = sizeof(Register) * 2;
    std::size_t off = 1;
    // x0..x31 + pc
    for (std::size_t i = 0; i < 33 && off + hex_per_int <= pkt.size(); ++i, off += hex_per_int) {
        write_gdb_reg(i, pkt, off, state);
    }
    // f0..f31 (8 bytes each = 16 hex chars)
    constexpr size_t hex_per_fp = sizeof(uint64_t) * 2;
    for (std::size_t i = 33; i < 65 && off + hex_per_fp <= pkt.size(); ++i, off += hex_per_fp) {
        write_gdb_reg(i, pkt, off, state);
    }
    invalidate_debug_state(machine, target_cpu);
    return "OK";
}

auto GdbStub::cmd_read_register(const std::string& pkt, simrv::core::Machine& machine)
    -> std::string {
    // p n
    const auto parsed = parse_hex(std::string_view(pkt).substr(1));
    if (!parsed) {
        return "E01";
    }
    const auto idx = *parsed;
    auto& target_cpu = (current_hart_.raw() < machine.num_harts()) ? machine.hart(current_hart_)
                                                                   : machine.primary_hart();
    const auto result = read_gdb_reg(idx, target_cpu.state());
    if (result) {
        return *result;
    } else {
        return "E01";
    }
}

auto GdbStub::cmd_write_register(const std::string& pkt, simrv::core::Machine& machine)
    -> std::string {
    // P n=v
    const std::size_t eq = pkt.find('=');
    if (eq == std::string::npos || eq + 1 >= pkt.size()) {
        return "E01";
    }
    const auto parsed = parse_hex(std::string_view(pkt).substr(1, eq - 1));
    if (!parsed || *parsed >= 65 ||
        pkt.size() - eq - 1 != (*parsed < 33 ? sizeof(Register) * 2 : 16) ||
        !valid_hex(std::string_view(pkt).substr(eq + 1))) {
        return "E01";
    }
    const auto idx = *parsed;
    auto& target_cpu = (current_hart_.raw() < machine.num_harts()) ? machine.hart(current_hart_)
                                                                   : machine.primary_hart();
    write_gdb_reg(idx, pkt, eq + 1, target_cpu.state());
    invalidate_debug_state(machine, target_cpu);
    return "OK";
}

auto GdbStub::cmd_read_memory(const std::string& pkt, simrv::core::Machine& machine)
    -> std::string {
    // m addr,len
    const std::size_t comma = pkt.find(',');
    if (comma == std::string::npos) {
        return "E01";
    }
    const auto address = parse_hex(std::string_view(pkt).substr(1, comma - 1));
    const auto length = parse_hex(std::string_view(pkt).substr(comma + 1));
    if (!address || !length || *address > std::numeric_limits<Address>::max() || *length > 4096 ||
        !machine.ram_view().contains(*address, *length)) {
        return "E01";
    }
    const auto addr = static_cast<Address>(*address);
    const auto len = static_cast<size_t>(*length);

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
    return resp;
}

auto GdbStub::cmd_write_memory(const std::string& pkt, simrv::core::Machine& machine)
    -> std::string {
    // M addr,len:data
    const std::size_t comma = pkt.find(',');
    const std::size_t colon = pkt.find(':');
    if (comma == std::string::npos || colon == std::string::npos || colon <= comma) {
        return "E01";
    }
    const auto address = parse_hex(std::string_view(pkt).substr(1, comma - 1));
    const auto length = parse_hex(std::string_view(pkt).substr(comma + 1, colon - comma - 1));
    if (!address || !length || *address > std::numeric_limits<Address>::max() || *length > 4096 ||
        pkt.size() - colon - 1 != *length * 2 ||
        (*length != 0 && !valid_hex(std::string_view(pkt).substr(colon + 1))) ||
        !machine.ram_view().contains(*address, *length)) {
        return "E01";
    }
    const auto addr = static_cast<Address>(*address);
    const auto len = static_cast<size_t>(*length);

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
    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        invalidate_debug_state(machine, machine.hart(hart));
    }
    return "OK";
}

auto GdbStub::cmd_breakpoint(const std::string& pkt, simrv::core::Machine& machine) -> std::string {
    const bool insert = pkt.front() == 'Z';
    const auto c1 = pkt.find(',');
    if (c1 == std::string::npos) return "E01";
    const auto c2 = pkt.find(',', c1 + 1);
    if (c2 == std::string::npos) return "E01";
    const auto type = parse_hex(std::string_view(pkt).substr(1, c1 - 1));
    const auto address = parse_hex(std::string_view(pkt).substr(c1 + 1, c2 - c1 - 1));
    const auto length = parse_hex(std::string_view(pkt).substr(c2 + 1));
    if (!type || !address || !length || *length == 0 ||
        *address > std::numeric_limits<Address>::max() ||
        (insert && *length - 1 > std::numeric_limits<Address>::max() - *address))
        return "E01";

    auto& breakpoints = machine.breakpoint_manager();
    const auto addr = static_cast<Address>(*address);
    if (*type == std::to_underlying(GdbBreakpointType::Software)) {
        if (insert)
            breakpoints.add_pc_breakpoint(addr, BreakpointOwner::Gdb);
        else
            breakpoints.remove_pc_breakpoint(addr, BreakpointOwner::Gdb);
        return "OK";
    }

    WatchType watch;
    switch (*type) {
        case std::to_underlying(GdbBreakpointType::WriteWatch):
            watch = WatchType::Write;
            break;
        case std::to_underlying(GdbBreakpointType::ReadWatch):
            watch = WatchType::Read;
            break;
        case std::to_underlying(GdbBreakpointType::AccessWatch):
            watch = WatchType::Access;
            break;
        default:
            return "";
    }
    if (insert)
        breakpoints.add_watchpoint(addr, *length, watch, "gdb", BreakpointOwner::Gdb);
    else
        breakpoints.remove_watchpoint(addr, *length, watch, BreakpointOwner::Gdb);
    return "OK";
}

namespace {
auto get_target_xml() -> const std::string& {
    static const std::string xml = []() {
        std::string s;
        s.reserve(4096);
        s += "<?xml version=\"1.0\"?>\n";
        s += "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n";
        s += "<target version=\"1.0\">\n";
        if constexpr (simrv::xlen::kIsXLen64) {
            s += "  <architecture>riscv:rv64</architecture>\n";
        } else {
            s += "  <architecture>riscv:rv32</architecture>\n";
        }
        s += "  <feature name=\"org.gnu.gdb.riscv.cpu\">\n";
        const int bits = simrv::xlen::kIsXLen64 ? 64 : 32;
        static constexpr std::array<const char*, 32> kRegNames = {
            "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
            "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
            "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
        for (size_t i = 0; i < 32; ++i) {
            s += std::format(
                "    <reg name=\"{}\" bitsize=\"{}\" type=\"code_ptr\" regnum=\"{}\"/>\n",
                kRegNames[i], bits, i);
        }
        s += std::format("    <reg name=\"pc\" bitsize=\"{}\" type=\"code_ptr\" regnum=\"32\"/>\n",
                         bits);
        s += "  </feature>\n";
        s += "  <feature name=\"org.gnu.gdb.riscv.fpu\">\n";
        for (size_t i = 0; i < 32; ++i) {
            s += std::format(
                "    <reg name=\"f{}\" bitsize=\"64\" type=\"ieee_double\" regnum=\"{}\"/>\n", i,
                33 + i);
        }
        s += "  </feature>\n";
        s += "</target>\n";
        return s;
    }();
    return xml;
}
}  // namespace

auto GdbStub::handle_qxfer(const std::string& pkt) -> std::string {
    // qXfer:features:read:target.xml:offset,length
    static constexpr std::string_view kPrefix = "qXfer:features:read:target.xml:";
    if (!pkt.starts_with(kPrefix)) {
        return "";
    }

    const std::string params = pkt.substr(kPrefix.size());
    const auto comma = params.find(',');
    if (comma == std::string::npos) {
        return "E01";
    }

    const auto parsed_offset = parse_hex(std::string_view(params).substr(0, comma));
    const auto parsed_length = parse_hex(std::string_view(params).substr(comma + 1));
    if (!parsed_offset || !parsed_length || *parsed_length == 0) {
        return "E01";
    }
    const size_t offset = *parsed_offset;
    const size_t length = std::min<uint64_t>(*parsed_length, 16383);

    const auto& xml = get_target_xml();
    if (offset >= xml.size()) {
        return "l";
    }

    const size_t chunk_len = std::min(length, xml.size() - offset);
    const std::string chunk = xml.substr(offset, chunk_len);
    const bool is_last = (offset + chunk_len >= xml.size());
    return (is_last ? "l" : "m") + chunk;
}

auto GdbStub::handle_query(const std::string& pkt, simrv::core::Machine& machine) -> std::string {
    if (pkt == "qSupported" || pkt.starts_with("qSupported:")) {
        return "PacketSize=4000;QStartNoAckMode+;swbreak+;qXfer:features:read+";
    }
    if (pkt == "QStartNoAckMode") {
        no_ack_mode_.store(true, std::memory_order_release);
        return "OK";
    }
    if (pkt == "qAttached") {
        return "1";
    }
    if (pkt.starts_with("qXfer:")) {
        return handle_qxfer(pkt);
    }
    if (pkt == "qC") {
        return std::format("QC{:x}", current_hart_.raw() + 1);
    }
    if (pkt == "qfThreadInfo") {
        std::string threads = "m";
        for (size_t i = 1; i <= machine.num_harts(); ++i) {
            if (i > 1) threads += ",";
            threads += std::format("{:x}", i);
        }
        return threads;
    }
    if (pkt == "qsThreadInfo") {
        return "l";
    }
    if (pkt == "qOffsets") {
        return "Text=0;Data=0;Bss=0";
    }
    if (pkt == "qSymbol::") {
        return "OK";
    }
    // Unrecognised query
    return "";
}

// ---------------------------------------------------------------------------
// Main packet dispatcher
// ---------------------------------------------------------------------------

auto GdbStub::handle_packet(const std::string& pkt, simrv::core::Machine& machine)
    -> CommandResult {
    if (pkt.empty()) {
        return {.response = ""};
    }

    // Ctrl-C interrupt
    if (pkt.front() == '\x03') {
        machine.pause();
        pause_requested_.store(false, std::memory_order_release);
        last_stop_reply_ = std::format("T02thread:{:x};", current_hart_.raw() + 1);
        return {.response = last_stop_reply_};
    }

    switch (pkt.front()) {
        case '?':
            return {.response = last_stop_reply_};

        case 'g':
            return {.response = cmd_read_registers(machine)};

        case 'G':
            return {.response = cmd_write_registers(pkt, machine)};

        case 'p':
            return {.response = cmd_read_register(pkt, machine)};

        case 'P':
            return {.response = cmd_write_register(pkt, machine)};

        case 'm':
            return {.response = cmd_read_memory(pkt, machine)};

        case 'M':
            return {.response = cmd_write_memory(pkt, machine)};

        case 'Z':
            return {.response = cmd_breakpoint(pkt, machine)};

        case 'z':
            return {.response = cmd_breakpoint(pkt, machine)};

        case 'c':
            if (pkt != "c") {
                return {.response = "E01"};
            }
            machine.resume();
            return {};

        case 's':
            if (pkt != "s") {
                return {.response = "E01"};
            }
            if (!machine.begin_debug_step(current_hart_)) return {.response = "E01"};
            return {};

        case 'D':
            machine.breakpoint_manager().clear_owner(BreakpointOwner::Gdb);
            machine.resume();
            simrv::log::info("GDB detached; target resumed and listener retained");
            return {.response = "OK", .connection = ConnectionDisposition::Close};

        case 'k':
            machine.breakpoint_manager().clear_owner(BreakpointOwner::Gdb);
            machine.stop();
            return {.response = std::nullopt, .connection = ConnectionDisposition::Close};

        case 'H': {
            if (pkt.size() < 3 || (pkt[1] != 'g' && pkt[1] != 'c')) {
                return {.response = "E01"};
            }
            const auto text = std::string_view(pkt).substr(2);
            const auto tid = text == "-1" ? std::optional<uint64_t>{0} : parse_hex(text);
            if (!tid || *tid > machine.num_harts()) {
                return {.response = "E01"};
            }
            current_hart_ = HartId{static_cast<uint32_t>(*tid == 0 ? 0 : *tid - 1)};
            return {.response = "OK"};
        }

        case 'T': {
            if (const auto tid = parse_hex(std::string_view(pkt).substr(1)); tid) {
                if (*tid > 0 && *tid <= machine.num_harts()) {
                    return {.response = "OK"};
                }
            }
            return {.response = "E01"};
        }

        case 'v':
            if (pkt == "vCont?") {
                return {.response = "vCont;c;s"};
            }
            if (pkt.starts_with("vCont;")) {
                std::string_view actions = std::string_view(pkt).substr(6);
                std::optional<HartId> stepping;
                bool seen = false;
                while (!actions.empty()) {
                    const auto end = actions.find(';');
                    const auto action = actions.substr(0, end);
                    if (action.empty() || (action[0] != 'c' && action[0] != 's') ||
                        (action.size() > 1 && action[1] != ':')) {
                        return {.response = "E01"};
                    }
                    HartId target = current_hart_;
                    if (action.size() > 1) {
                        const auto tid = parse_hex(action.substr(2));
                        if (!tid || *tid == 0 || *tid > machine.num_harts()) {
                            return {.response = "E01"};
                        }
                        target = HartId{static_cast<uint32_t>(*tid - 1)};
                    }
                    if (action[0] == 's') {
                        if (stepping) {
                            return {.response = "E01"};
                        }
                        stepping = target;
                    }
                    seen = true;
                    if (end == std::string_view::npos) break;
                    actions.remove_prefix(end + 1);
                    if (actions.empty()) {
                        return {.response = "E01"};
                    }
                }
                if (!seen) {
                    return {.response = "E01"};
                }
                if (stepping) {
                    if (!machine.begin_debug_step(*stepping)) return {.response = "E01"};
                } else
                    machine.resume();
                return {};
            }
            return {.response = ""};

        case 'q':
        case 'Q':
            return {.response = handle_query(pkt, machine)};

        default:
            return {.response = ""};
    }
}

void GdbStub::request_pause() noexcept {
    pause_requested_.store(true, std::memory_order_release);
    if (wake_machine_) wake_machine_();
}

auto GdbStub::submit_command(CommandKind kind, std::string packet) -> CommandResult {
    auto command = std::make_shared<Command>();
    command->kind = kind;
    command->packet = std::move(packet);
    {
        const std::lock_guard lock(command_mutex_);
        commands_.push_back(command);
        pending_commands_.fetch_add(1, std::memory_order_release);
    }
    if (wake_machine_) wake_machine_();

    std::unique_lock lock(command_mutex_);
    command_cv_.wait(lock, [this, &command]() {
        return command->done || !running_.load(std::memory_order_acquire);
    });
    return command->result;
}

void GdbStub::service_pending(simrv::core::Machine& machine) {
    while (true) {
        std::shared_ptr<Command> command;
        {
            const std::lock_guard lock(command_mutex_);
            if (commands_.empty()) break;
            command = commands_.front();
            commands_.pop_front();
        }

        CommandResult result;
        try {
            switch (command->kind) {
                case CommandKind::Attach:
                    machine.pause();
                    last_stop_reply_ = std::format("T05thread:{:x};", current_hart_.raw() + 1);
                    pause_requested_.store(false, std::memory_order_release);
                    break;
                case CommandKind::Disconnect:
                    machine.pause();
                    machine.breakpoint_manager().clear_owner(BreakpointOwner::Gdb);
                    current_hart_ = HartId{0};
                    pause_requested_.store(false, std::memory_order_release);
                    break;
                case CommandKind::Packet:
                    if (machine.execution_state() != simrv::core::ExecutionState::Paused &&
                        machine.execution_state() != simrv::core::ExecutionState::Stopped) {
                        machine.pause();
                    }
                    pause_requested_.store(false, std::memory_order_release);
                    result = handle_packet(command->packet, machine);
                    break;
            }
        } catch (const std::exception& ex) {
            simrv::log::warn("GDB rejected malformed packet '{}': {}", command->packet, ex.what());
            result.response = "E01";
        }

        {
            const std::lock_guard lock(command_mutex_);
            command->result = std::move(result);
            command->done = true;
            pending_commands_.fetch_sub(1, std::memory_order_release);
        }
        command_cv_.notify_all();
    }
}

void GdbStub::notify_stop(HartId hart, GdbSignal signal, std::string reason) {
    if (!is_connected()) return;
    current_hart_ = hart;
    std::string reply =
        std::format("T{:02x}thread:{:x};", std::to_underlying(signal), hart.raw() + 1);
    reply += reason;
    last_stop_reply_ = reply;
    simrv::log::info("GDB target stopped on hart {}: signal {}{}", hart.raw(),
                     std::to_underlying(signal), reason.empty() ? "" : std::format(", {}", reason));
    {
        const std::lock_guard lock(outbound_mutex_);
        outbound_packets_.push_back(std::move(reply));
    }
    signal_worker();
}

void GdbStub::flush_outbound_packets() {
    std::deque<std::string> packets;
    {
        const std::lock_guard lock(outbound_mutex_);
        packets.swap(outbound_packets_);
    }
    for (const auto& packet : packets) {
        if (!send_packet_wire(packet)) break;
    }
}

void GdbStub::connection_loop(const std::stop_token& stop_token) {
    bool explicit_close = false;
    while (running_.load(std::memory_order_acquire) && !stop_token.stop_requested()) {
        std::string packet;
        errno = 0;
        if (!recv_packet(packet)) {
            if (errno == EBADMSG) continue;
            break;
        }
        request_pause();
        const auto result = submit_command(CommandKind::Packet, packet);
        if (result.response && !send_packet_wire(*result.response)) break;
        if (result.connection == ConnectionDisposition::Close) {
            explicit_close = true;
            break;
        }
    }

    if (!explicit_close && running_.load(std::memory_order_acquire)) {
        request_pause();
        static_cast<void>(submit_command(CommandKind::Disconnect));
        simrv::log::warn("GDB client disconnected; target paused and listener restarted");
    }
}

void GdbStub::worker_loop(const std::stop_token& stop_token) {
    while (running_.load(std::memory_order_acquire) && !stop_token.stop_requested()) {
        state_.store(GdbConnectionState::Listening, std::memory_order_release);
        if (!wait_for_client(stop_token)) break;
        connection_loop(stop_token);
        close_connection();
    }
    state_.store(GdbConnectionState::Stopped, std::memory_order_release);
}

}  // namespace simrv::debug
