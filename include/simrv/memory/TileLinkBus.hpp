/**
 * @file TileLinkBus.hpp
 * @brief Internal TileLink-C 1.8.1 profile fabric and transaction adapter.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "simrv/memory/Bus.hpp"
#include "simrv/memory/CoherenceHub.hpp"
#include "simrv/memory/MmioRouter.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/memory/TileLinkProtocolChecker.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::memory {

class TileLinkBus : public Bus {
   public:
    using Cycle = uint64_t;

    struct TimedResponse {
        TlChannelD payload{};
        std::array<Byte, CoherenceHub::kLineBytes> line_data{};
        Cycle ready_cycle = 0;
        uint64_t sequence = 0;
        bool has_line_data = false;
    };

    explicit TileLinkBus(simrv::core::Machine& machine);

    void add_node(TileLinkNode* node);
    /// Set fixed ready/valid-style transport delays for CA. Both values are at least one cycle.
    void configure_timing(uint32_t request_latency, uint32_t response_latency);
    [[nodiscard]] auto request_latency() const noexcept -> uint32_t { return request_latency_; }
    [[nodiscard]] auto response_latency() const noexcept -> uint32_t { return response_latency_; }

    [[nodiscard]] auto router() -> MmioRouter& { return router_; }
    [[nodiscard]] auto router() const -> const MmioRouter& { return router_; }

    [[nodiscard]] auto coherence_hub() -> CoherenceHub& { return coherence_hub_; }
    [[nodiscard]] auto coherence_hub() const -> const CoherenceHub& { return coherence_hub_; }

    auto send_request(const TlChannelA& req) -> bool override;
    auto get_response(TlSourceId source_id, TlChannelD& resp) -> bool override;
    auto try_get_timed_response(TlSourceId source_id, TimedResponse& resp) -> bool;
    void cancel_source(TlSourceId source_id);

    /// Advance deterministic arbitration by one interconnect clock.
    void advance_cycle();
    [[nodiscard]] auto cycle() const noexcept -> Cycle { return cycle_; }
    [[nodiscard]] auto pending_requests() const noexcept -> size_t { return req_queue_.size(); }
    [[nodiscard]] auto pending_responses() const noexcept -> size_t;

    auto acquire_block(const TlChannelA& req, TlChannelD& resp,
                       std::array<Byte, CoherenceHub::kLineBytes>& line_data) -> bool;

    auto acquire_perm(const TlChannelA& req, TlChannelD& resp) -> bool;

    auto release_line(const TlChannelC& req, TlChannelD& resp,
                      const std::array<Byte, CoherenceHub::kLineBytes>* data = nullptr) -> bool {
        return coherence_hub_.handle_release(req, resp, data);
    }

    void grant_ack(const TlChannelE& ack);

    [[nodiscard]] auto protocol_checker() const -> const TileLinkProtocolChecker& {
        return protocol_checker_;
    }
    [[nodiscard]] static constexpr auto ram_capabilities() -> TlManagerCapabilities {
        return kCoherentRamCapabilities;
    }
    [[nodiscard]] static constexpr auto mmio_capabilities() -> TlManagerCapabilities {
        return kMmioCapabilities;
    }

    [[nodiscard]] auto read_count() const -> uint64_t override {
        return read_count_ + router_.mmio_read_count() + coherence_hub_.stats().acquire_count;
    }
    [[nodiscard]] auto write_count() const -> uint64_t override {
        return write_count_ + router_.mmio_write_count() + coherence_hub_.stats().writeback_count;
    }

   private:
    struct TimedRequest {
        TlChannelA payload{};
        Cycle submitted_cycle = 0;
        uint64_t sequence = 0;
    };

    struct TimedDBeat {
        TlChannelD payload{};
        Cycle ready_cycle = 0;
        uint64_t sequence = 0;
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
    uint64_t read_count_ = 0;
    uint64_t write_count_ = 0;

    mutable std::recursive_mutex bus_mutex_;
    MmioRouter router_;
    CoherenceHub coherence_hub_;
    TileLinkProtocolChecker protocol_checker_;
    Cycle cycle_ = 0;
    uint64_t next_sequence_ = 0;
    uint32_t request_latency_ = 1;
    uint32_t response_latency_ = 1;
    std::deque<TimedRequest> req_queue_;
    std::deque<TimedDBeat> d_queue_;
    std::unordered_map<TlSourceId, DAssembly> d_assemblies_;
};

}  // namespace simrv::memory
