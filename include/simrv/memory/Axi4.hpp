/**
 * @file Axi4.hpp
 * @brief AMBA AXI4 protocol definitions, signal channels, and TileLink bridge.
 */
#pragma once

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
#include "simrv/xlen/Types.hpp"

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

struct Axi4Aw {
    uint32_t id{0};
    Address addr{0};
    uint8_t len{0};   // Burst length: beats - 1
    uint8_t size{3};  // Burst size: 2^size bytes per beat
    AxiBurst burst{AxiBurst::Incr};
    uint8_t lock{0};
    uint8_t cache{0};
    uint8_t prot{0};
    uint8_t qos{0};
};

struct Axi4W {
    Word data{0};
    uint32_t strb{0xFFFFFFFFU};
    bool last{true};
};

struct Axi4B {
    uint32_t id{0};
    AxiResp resp{AxiResp::Okay};
};

struct Axi4Ar {
    uint32_t id{0};
    Address addr{0};
    uint8_t len{0};   // Burst length: beats - 1
    uint8_t size{3};  // Burst size: 2^size bytes per beat
    AxiBurst burst{AxiBurst::Incr};
    uint8_t lock{0};
    uint8_t cache{0};
    uint8_t prot{0};
    uint8_t qos{0};
};

struct Axi4R {
    uint32_t id{0};
    Word data{0};
    AxiResp resp{AxiResp::Okay};
    bool last{true};
};

// -----------------------------------------------------------------------------
// AXI4 Transaction Tracer
// -----------------------------------------------------------------------------

class Axi4Tracer {
   public:
    explicit Axi4Tracer(const std::string& path = "");
    ~Axi4Tracer();

    void enable(bool en = true);
    [[nodiscard]] auto is_enabled() const noexcept -> bool { return enabled_; }

    void trace_aw(uint64_t cycle, const Axi4Aw& aw);
    void trace_w(uint64_t cycle, const Axi4W& w);
    void trace_b(uint64_t cycle, const Axi4B& b);
    void trace_ar(uint64_t cycle, const Axi4Ar& ar);
    void trace_r(uint64_t cycle, const Axi4R& r);

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
    explicit Axi4Bridge(core::Machine* machine = nullptr, pipeline::AxiConfig config = {});

    void set_config(const pipeline::AxiConfig& config) noexcept {
        config_ = config;
        tracer_.enable(config_.trace_axi);
    }
    [[nodiscard]] auto config() const noexcept -> const pipeline::AxiConfig& { return config_; }

    [[nodiscard]] auto tracer() noexcept -> Axi4Tracer& { return tracer_; }
    [[nodiscard]] auto tracer() const noexcept -> const Axi4Tracer& { return tracer_; }

    // Channel-level transaction entry points
    auto send_ar(const Axi4Ar& ar) -> bool;
    auto recv_r(Axi4R& r) -> bool;

    auto send_aw(const Axi4Aw& aw) -> bool;
    auto send_w(const Axi4W& w) -> bool;
    auto recv_b(Axi4B& b) -> bool;

    // Helper burst methods
    auto axi_read(Address addr, uint8_t size_bytes, uint8_t len = 0)
        -> std::pair<AxiResp, std::vector<Word>>;
    auto axi_write(Address addr, std::span<const Word> data, uint8_t size_bytes,
                   uint32_t strb = 0xFFFFFFFFU) -> AxiResp;

   private:
    core::Machine* machine_{nullptr};
    pipeline::AxiConfig config_{};
    Axi4Tracer tracer_;

    std::vector<Axi4Ar> pending_ar_;
    std::vector<Axi4R> pending_r_;
    std::vector<Axi4Aw> pending_aw_;
    std::vector<Axi4W> pending_w_;
    std::vector<Axi4B> pending_b_;
};

}  // namespace simrv::memory
