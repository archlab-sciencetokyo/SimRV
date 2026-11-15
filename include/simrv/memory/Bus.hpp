/**
 * @file Bus.hpp
 * @brief Protocol-neutral system interconnect bus abstraction (TileLink & AXI4).
 */
#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <deque>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "simrv/CompilerHints.hpp"
#include "simrv/memory/Axi4.hpp"
#include "simrv/memory/BusProtocolSpec.hpp"
#include "simrv/memory/TileLinkProtocol.hpp"
#include "simrv/memory/TileLinkProtocolChecker.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
struct BusTraceContext;
}  // namespace simrv::core

namespace simrv::memory {

class CoherenceHub;
class MmioRouter;
class TileLinkNode;

class Bus {
   public:
    using Cycle = simrv::xlen::CycleCount;

    struct TimedResponse {
        TlChannelD payload{};
        std::array<simrv::xlen::Byte, kTlBlockBytes> line_data{};
        Cycle ready_cycle = 0;
        simrv::xlen::Counter sequence = 0;
        bool has_line_data = false;
    };

    struct ContentionStats {
        bool dma_active = false;
        simrv::xlen::LatencyCycles dma_penalty_cycles = 0;
        simrv::xlen::Counter delayed_requests = 0;
        simrv::xlen::LatencyCycles added_latency_cycles = 0;
    };

    struct TlTransactionRecord {
        Cycle cycle = 0;
        TileLinkChannel channel = TileLinkChannel::A;
        std::string_view opcode;
        TlSourceId source = 0;
        TlSinkId sink = 0;
        simrv::xlen::PhysAddr address{};
        std::string_view detail;
    };

    static constexpr size_t kMaxTransactionHistory = 32;

    Bus() = default;
    virtual ~Bus() = default;

    [[nodiscard]] virtual auto protocol() const noexcept -> BusProtocol = 0;
    [[nodiscard]] virtual auto protocol_spec() const noexcept -> BusProtocolSpec = 0;
    [[nodiscard]] auto protocol_name() const noexcept -> std::string_view {
        return bus_protocol_name(protocol());
    }
    [[nodiscard]] auto protocol_display_name() const noexcept -> std::string_view {
        return bus_protocol_display_name(protocol());
    }

    virtual auto add_node(TileLinkNode* node) -> bool = 0;
    virtual void configure_timing(uint32_t request_latency, uint32_t response_latency,
                                  uint32_t data_request_latency = 0,
                                  uint32_t data_response_latency = 0,
                                  uint32_t startup_data_response_latency = 0) = 0;
    [[nodiscard]] virtual auto request_latency() const noexcept -> uint32_t = 0;
    [[nodiscard]] virtual auto response_latency() const noexcept -> uint32_t = 0;

    [[nodiscard]] virtual auto router() -> MmioRouter& = 0;
    [[nodiscard]] virtual auto router() const -> const MmioRouter& = 0;

    [[nodiscard]] virtual auto has_coherence() const noexcept -> bool = 0;
    [[nodiscard]] virtual auto coherence_hub() -> CoherenceHub& = 0;
    [[nodiscard]] virtual auto coherence_hub() const -> const CoherenceHub& = 0;

    virtual auto send_request(const TlChannelA& req) -> bool = 0;
    virtual auto get_response(TlSourceId source_id, TlChannelD& resp) -> bool = 0;
    virtual auto try_get_timed_response(TlSourceId source_id, TimedResponse& resp) -> bool = 0;
    virtual void cancel_source(TlSourceId source_id) = 0;

    /// Advance deterministic arbitration by one interconnect clock.
    SIMRV_ALWAYS_INLINE void advance_cycle() noexcept {
        if (simrv::compiler::likely(!has_pending_requests_.load(std::memory_order_relaxed))) {
            if (simrv::compiler::likely(!is_smp_enabled_)) {
                cycle_.store(cycle_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            } else {
                cycle_.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
        advance_cycle_slow();
    }
    virtual void advance_cycle_slow() = 0;
    [[nodiscard]] auto cycle() const noexcept -> Cycle {
        return cycle_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] virtual auto pending_requests() const noexcept -> size_t = 0;
    [[nodiscard]] virtual auto pending_responses() const noexcept -> size_t = 0;
    [[nodiscard]] virtual auto contention_stats() const -> ContentionStats = 0;
    void set_smp_enabled(bool enabled) noexcept { is_smp_enabled_ = enabled; }
    [[nodiscard]] auto is_smp_enabled() const noexcept -> bool { return is_smp_enabled_; }

    virtual auto acquire_block(const TlChannelA& req, TlChannelD& resp,
                               std::array<simrv::xlen::Byte, kTlBlockBytes>& line_data) -> bool = 0;
    virtual auto acquire_perm(const TlChannelA& req, TlChannelD& resp) -> bool = 0;
    virtual auto release_line(const TlChannelC& req, TlChannelD& resp,
                              const std::array<simrv::xlen::Byte, kTlBlockBytes>* data = nullptr)
        -> bool = 0;

    virtual void mark_modified(simrv::xlen::PhysAddr line_base, simrv::xlen::HartId hart) = 0;
    template <std::integral I, typename... Args>
    void mark_modified(I, Args&&...) = delete;

    virtual void grant_ack(const TlChannelE& ack) = 0;

    [[nodiscard]] virtual auto protocol_checker() const -> const TileLinkProtocolChecker& = 0;
    [[nodiscard]] virtual auto protocol_checker() -> TileLinkProtocolChecker& = 0;

    [[nodiscard]] virtual auto transaction_history() const
        -> const std::deque<TlTransactionRecord>& = 0;
    virtual void record_transaction(
        TileLinkChannel ch, std::string_view opcode, TlSourceId source, TlSinkId sink,
        simrv::xlen::PhysAddr address, std::string_view detail = {},
        const simrv::core::BusTraceContext* trace_context = nullptr) = 0;

    // Optional AXI4 channel-level entry points (active when Spec.is_axi())
    virtual auto send_ar(const Axi4Ar& /*ar*/) -> bool { return false; }
    virtual auto recv_r(Axi4R& /*r*/) -> bool { return false; }
    virtual auto send_aw(const Axi4Aw& /*aw*/) -> bool { return false; }
    virtual auto send_w(const Axi4W& /*w*/) -> bool { return false; }
    virtual auto recv_b(Axi4B& /*b*/) -> bool { return false; }
    virtual auto axi_read(simrv::xlen::Address /*addr*/, uint8_t /*size_bytes*/,
                          uint8_t /*len*/ = 0)
        -> std::pair<AxiResp, std::vector<simrv::xlen::Word>> {
        return {AxiResp::Decerr, {}};
    }
    virtual auto axi_write(simrv::xlen::Address /*addr*/,
                           std::span<const simrv::xlen::Word> /*data*/, uint8_t /*size_bytes*/,
                           uint32_t /*strb*/ = 0xFFFFFFFFU) -> AxiResp {
        return AxiResp::Decerr;
    }

    [[nodiscard]] static constexpr auto ram_capabilities() -> TlManagerCapabilities {
        return kCoherentRamCapabilities;
    }
    [[nodiscard]] static constexpr auto mmio_capabilities() -> TlManagerCapabilities {
        return kMmioCapabilities;
    }

    [[nodiscard]] virtual auto read_count() const -> simrv::xlen::Counter = 0;
    [[nodiscard]] virtual auto write_count() const -> simrv::xlen::Counter = 0;

   protected:
    std::atomic<Cycle> cycle_{0};
    std::atomic<bool> has_pending_requests_{false};
    bool is_smp_enabled_ = false;
};

using SystemBus = Bus;

}  // namespace simrv::memory
