/**
 * @file TemplatedBus.hpp
 * @brief C++23 NTTP-specialized system interconnect supporting TileLink (UL/UH/C) and AXI4 /
 * AXI4-Lite.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "simrv/memory/Axi4.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/memory/BusProtocolSpec.hpp"
#include "simrv/memory/CoherenceHub.hpp"
#include "simrv/memory/MmioRouter.hpp"
#include "simrv/memory/TileLinkProtocolChecker.hpp"
#include "simrv/util/SmallFlatMap.hpp"

namespace simrv::core {
class Machine;
}  // namespace simrv::core

namespace simrv::memory {

template <BusProtocolSpec Spec>
class TemplatedBus final : public Bus {
   public:
    static constexpr BusProtocolSpec kSpec = Spec;

    explicit TemplatedBus(simrv::core::Machine& machine);

    [[nodiscard]] auto protocol() const noexcept -> BusProtocol override { return Spec.protocol; }
    [[nodiscard]] auto protocol_spec() const noexcept -> BusProtocolSpec override { return Spec; }

    auto add_node(TileLinkNode* node) -> bool override;
    void configure_timing(uint32_t request_latency, uint32_t response_latency,
                          uint32_t data_request_latency = 0, uint32_t data_response_latency = 0,
                          uint32_t startup_data_response_latency = 0) override;
    [[nodiscard]] auto request_latency() const noexcept -> uint32_t override {
        return request_latency_;
    }
    [[nodiscard]] auto response_latency() const noexcept -> uint32_t override {
        return response_latency_;
    }

    [[nodiscard]] auto router() -> MmioRouter& override { return router_; }
    [[nodiscard]] auto router() const -> const MmioRouter& override { return router_; }

    [[nodiscard]] auto has_coherence() const noexcept -> bool override {
        return Spec.supports_coherence;
    }
    [[nodiscard]] auto coherence_hub() -> CoherenceHub& override { return coherence_hub_; }
    [[nodiscard]] auto coherence_hub() const -> const CoherenceHub& override {
        return coherence_hub_;
    }

    auto send_request(const TlChannelA& req) -> bool override;
    auto get_response(TlSourceId source_id, TlChannelD& resp) -> bool override;
    auto try_get_timed_response(TlSourceId source_id, TimedResponse& resp) -> bool override;
    void cancel_source(TlSourceId source_id) override;

    void advance_cycle_slow() override;
    [[nodiscard]] auto pending_requests() const noexcept -> size_t override {
        return req_queue_.size();
    }
    [[nodiscard]] auto pending_responses() const noexcept -> size_t override;
    [[nodiscard]] auto contention_stats() const -> ContentionStats override;

    auto acquire_block(const TlChannelA& req, TlChannelD& resp,
                       std::array<simrv::xlen::Byte, CoherenceHub::kLineBytes>& line_data)
        -> bool override;
    auto acquire_perm(const TlChannelA& req, TlChannelD& resp) -> bool override;
    auto release_line(const TlChannelC& req, TlChannelD& resp,
                      const std::array<simrv::xlen::Byte, CoherenceHub::kLineBytes>* data = nullptr)
        -> bool override;

    void mark_modified(simrv::xlen::PhysAddr line_base, simrv::xlen::HartId hart) override;
    void grant_ack(const TlChannelE& ack) override;

    [[nodiscard]] auto protocol_checker() const -> const TileLinkProtocolChecker& override {
        return protocol_checker_;
    }
    [[nodiscard]] auto protocol_checker() -> TileLinkProtocolChecker& override {
        return protocol_checker_;
    }

    [[nodiscard]] auto transaction_history() const
        -> const std::deque<TlTransactionRecord>& override {
        return transaction_history_;
    }
    void record_transaction(TileLinkChannel ch, std::string_view opcode, TlSourceId source,
                            TlSinkId sink, simrv::xlen::PhysAddr address,
                            std::string_view detail = {},
                            const simrv::core::BusTraceContext* trace_context = nullptr) override;

    auto send_ar(const Axi4Ar& ar) -> bool override;
    auto recv_r(Axi4R& r) -> bool override;
    auto send_aw(const Axi4Aw& aw) -> bool override;
    auto send_w(const Axi4W& w) -> bool override;
    auto recv_b(Axi4B& b) -> bool override;
    auto axi_read(simrv::xlen::Address addr, uint8_t size_bytes, uint8_t len = 0)
        -> std::pair<AxiResp, std::vector<simrv::xlen::Word>> override;
    auto axi_write(simrv::xlen::Address addr, std::span<const simrv::xlen::Word> data,
                   uint8_t size_bytes, uint32_t strb = 0xFFFFFFFFU) -> AxiResp override;

    [[nodiscard]] auto axi_tracer() noexcept -> Axi4Tracer& { return axi_tracer_; }
    [[nodiscard]] auto axi_tracer() const noexcept -> const Axi4Tracer& { return axi_tracer_; }

    [[nodiscard]] auto read_count() const -> simrv::xlen::Counter override {
        if constexpr (Spec.supports_coherence) {
            return read_count_ + router_.mmio_read_count() + coherence_hub_.stats().acquire_count;
        } else {
            return read_count_ + router_.mmio_read_count();
        }
    }
    [[nodiscard]] auto write_count() const -> simrv::xlen::Counter override {
        if constexpr (Spec.supports_coherence) {
            return write_count_ + router_.mmio_write_count() +
                   coherence_hub_.stats().writeback_count;
        } else {
            return write_count_ + router_.mmio_write_count();
        }
    }

   private:
    struct TimedRequest {
        TlChannelA payload{};
        Cycle submitted_cycle = 0;
        simrv::xlen::LatencyCycles request_latency = 1;
        simrv::xlen::Counter sequence = 0;
    };

    struct TimedDBeat {
        TlChannelD payload{};
        Cycle ready_cycle = 0;
        simrv::xlen::Counter sequence = 0;
        uint8_t beat_index = 0;
        uint8_t beat_count = 1;
    };

    struct DAssembly {
        TimedResponse response{};
        uint8_t next_beat = 0;
        uint8_t beat_count = 1;
    };

    void process_request(const TimedRequest& request);
    auto consume_d_beat(TlSourceId source_id, bool honor_ready, TimedResponse& response) -> bool;

    simrv::core::Machine& machine_;
    simrv::xlen::Counter read_count_ = 0;
    simrv::xlen::Counter write_count_ = 0;
    std::atomic<simrv::xlen::Counter> dma_delayed_requests_{0};
    std::atomic<simrv::xlen::LatencyCycles> dma_added_latency_cycles_{0};

    mutable std::recursive_mutex bus_mutex_;
    MmioRouter router_;
    CoherenceHub coherence_hub_;
    TileLinkProtocolChecker protocol_checker_;
    Axi4Tracer axi_tracer_;
    uint64_t next_sequence_ = 0;
    uint32_t request_latency_ = 1;
    uint32_t response_latency_ = 1;
    uint32_t data_request_latency_ = 1;
    uint32_t data_response_latency_ = 1;
    uint32_t startup_data_response_latency_ = 0;
    uint64_t data_response_count_ = 0;
    std::deque<TimedRequest> req_queue_;
    std::deque<TimedDBeat> d_queue_;
    simrv::util::SmallFlatMap<TlSourceId, DAssembly, 8> d_assemblies_;
    std::deque<TlTransactionRecord> transaction_history_;

    // AXI4 channel state (used when Spec.is_axi())
    std::vector<Axi4Ar> pending_ar_;
    std::vector<Axi4R> pending_r_;
    std::vector<Axi4Aw> pending_aw_;
    std::vector<Axi4W> pending_w_;
    std::vector<Axi4B> pending_b_;
};

[[nodiscard]] auto make_system_bus(simrv::core::Machine& machine, const BusProtocolSpec& spec)
    -> std::unique_ptr<Bus>;

[[nodiscard]] inline auto make_system_bus(simrv::core::Machine& machine, BusProtocol protocol)
    -> std::unique_ptr<Bus> {
    if (const auto* entry = find_bus_catalog_entry(protocol)) {
        return make_system_bus(machine, entry->spec);
    }
    return make_system_bus(machine, kDefaultTileLinkCSpec);
}

extern template class TemplatedBus<kDefaultTileLinkCSpec>;
extern template class TemplatedBus<kDefaultTileLinkUHSpec>;
extern template class TemplatedBus<kDefaultTileLinkULSpec>;
extern template class TemplatedBus<kDefaultAxi4Spec>;
extern template class TemplatedBus<kDefaultAxi4LiteSpec>;

}  // namespace simrv::memory
