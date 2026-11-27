/**
 * @file Axi4.hpp
 * @brief AMBA AXI4 protocol definitions, signal channels, and TileLink bridge.
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::memory {

enum class AxiBurst : uint8_t {
    Fixed = 0b00,
    Incr = 0b01,
    Wrap = 0b10,
    Reserved = 0b11,
};

enum class AxiResp : uint8_t {
    Okay = 0b00,
    Exokay = 0b01,
    Slverr = 0b10,
    Decerr = 0b11,
};

[[nodiscard]] constexpr auto to_string(AxiBurst b) noexcept -> std::string_view {
    switch (b) {
        case AxiBurst::Fixed:
            return "FIXED";
        case AxiBurst::Incr:
            return "INCR";
        case AxiBurst::Wrap:
            return "WRAP";
        default:
            return "RESERVED";
    }
}

[[nodiscard]] constexpr auto to_string(AxiResp r) noexcept -> std::string_view {
    switch (r) {
        case AxiResp::Okay:
            return "OKAY";
        case AxiResp::Exokay:
            return "EXOKAY";
        case AxiResp::Slverr:
            return "SLVERR";
        case AxiResp::Decerr:
            return "DECERR";
    }
    return "UNKNOWN";
}

// -----------------------------------------------------------------------------
// AXI4 Five Channel Payloads
// -----------------------------------------------------------------------------

using AxiId = uint32_t;

struct Axi4Aw {
    AxiId id{0};
    simrv::xlen::Address addr{0};
    uint8_t len{0};   // Burst length: beats - 1
    uint8_t size{3};  // Burst size: 2^size bytes per beat
    AxiBurst burst{AxiBurst::Incr};
    uint8_t lock{0};
    uint8_t cache{0};
    uint8_t prot{0};
    uint8_t qos{0};
};

struct Axi4W {
    simrv::xlen::Word data{0};
    uint32_t strb{0xFFFFFFFFU};
    bool last{true};
    std::array<simrv::xlen::Byte, 16> data_bytes{};
    bool data_bytes_valid{false};
};

struct Axi4B {
    AxiId id{0};
    AxiResp resp{AxiResp::Okay};
};

struct Axi4Ar {
    AxiId id{0};
    simrv::xlen::Address addr{0};
    uint8_t len{0};   // Burst length: beats - 1
    uint8_t size{3};  // Burst size: 2^size bytes per beat
    AxiBurst burst{AxiBurst::Incr};
    uint8_t lock{0};
    uint8_t cache{0};
    uint8_t prot{0};
    uint8_t qos{0};
};

struct Axi4R {
    AxiId id{0};
    simrv::xlen::Word data{0};
    AxiResp resp{AxiResp::Okay};
    bool last{true};
    std::array<simrv::xlen::Byte, 16> data_bytes{};
    bool data_bytes_valid{false};
};

/// Signals sampled at a single AXI clock edge.
/// The system bus currently supports exclusive RAM transactions in single-hart,
/// non-SMP configurations.
struct Axi4CycleInput {
    bool ar_valid{false};
    Axi4Ar ar{};
    bool aw_valid{false};
    Axi4Aw aw{};
    bool w_valid{false};
    Axi4W w{};
    bool r_ready{false};
    bool b_ready{false};
};

/// Signals presented before the edge; a transfer occurs when valid and ready are both true.
struct Axi4CycleOutput {
    bool ar_ready{false};
    bool aw_ready{false};
    bool w_ready{false};
    bool r_valid{false};
    Axi4R r{};
    bool b_valid{false};
    Axi4B b{};
};

// -----------------------------------------------------------------------------
// AXI4 Transaction Tracer
// -----------------------------------------------------------------------------

class Axi4Tracer {
   public:
    explicit Axi4Tracer(const std::string& path = "");
    ~Axi4Tracer();

    void set_path(const std::string& path);
    void enable(bool en = true);
    [[nodiscard]] auto is_enabled() const noexcept -> bool { return enabled_; }

    void trace_aw(simrv::xlen::CycleCount cycle, const Axi4Aw& aw);
    void trace_w(simrv::xlen::CycleCount cycle, const Axi4W& w);
    void trace_b(simrv::xlen::CycleCount cycle, const Axi4B& b);
    void trace_ar(simrv::xlen::CycleCount cycle, const Axi4Ar& ar);
    void trace_r(simrv::xlen::CycleCount cycle, const Axi4R& r);

   private:
    std::mutex mutex_;
    std::ofstream out_;
    bool enabled_{false};
};

// -----------------------------------------------------------------------------
// AXI4 Protocol Bridge
// -----------------------------------------------------------------------------

class Axi4Bridge {
   public:
    struct ChannelStats {
        uint64_t ar = 0;
        uint64_t r = 0;
        uint64_t aw = 0;
        uint64_t w = 0;
        uint64_t b = 0;
    };

    explicit Axi4Bridge(core::Machine* machine = nullptr, pipeline::AxiConfig config = {});

    void set_config(const pipeline::AxiConfig& config) noexcept {
        config_ = config;
        tracer_.enable(config_.trace_axi);
    }
    [[nodiscard]] auto config() const noexcept -> const pipeline::AxiConfig& { return config_; }
    [[nodiscard]] auto channel_stats() const noexcept -> ChannelStats {
        return {.ar = ar_count_.load(std::memory_order_relaxed),
                .r = r_count_.load(std::memory_order_relaxed),
                .aw = aw_count_.load(std::memory_order_relaxed),
                .w = w_count_.load(std::memory_order_relaxed),
                .b = b_count_.load(std::memory_order_relaxed)};
    }

    [[nodiscard]] auto tracer() noexcept -> Axi4Tracer& { return tracer_; }
    [[nodiscard]] auto tracer() const noexcept -> const Axi4Tracer& { return tracer_; }

    // Channel-level transaction entry points
    auto send_ar(const Axi4Ar& ar) -> bool;
    auto recv_r(Axi4R& r) -> bool;

    auto send_aw(const Axi4Aw& aw) -> bool;
    auto send_w(const Axi4W& w) -> bool;
    auto recv_b(Axi4B& b) -> bool;

    // Helper burst methods
    auto axi_read(simrv::xlen::Address addr, uint8_t size_bytes, uint8_t len = 0)
        -> std::pair<AxiResp, std::vector<simrv::xlen::Word>>;
    auto axi_write(simrv::xlen::Address addr, std::span<const simrv::xlen::Word> data,
                   uint8_t size_bytes, uint32_t strb = 0xFFFFFFFFU) -> AxiResp;

   private:
    core::Machine* machine_{nullptr};
    pipeline::AxiConfig config_{};
    Axi4Tracer tracer_;

    std::vector<Axi4Ar> pending_ar_;
    std::vector<Axi4R> pending_r_;
    std::vector<Axi4Aw> pending_aw_;
    std::vector<Axi4W> pending_w_;
    std::vector<Axi4B> pending_b_;
    std::atomic<uint64_t> ar_count_{0};
    std::atomic<uint64_t> r_count_{0};
    std::atomic<uint64_t> aw_count_{0};
    std::atomic<uint64_t> w_count_{0};
    std::atomic<uint64_t> b_count_{0};
};

}  // namespace simrv::memory
