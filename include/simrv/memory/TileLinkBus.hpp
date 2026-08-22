/**
 * @file TileLinkBus.hpp
 * @brief TileLink-style sequential bus implementation.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "simrv/memory/Bus.hpp"
#include "simrv/memory/CoherenceHub.hpp"
#include "simrv/memory/MmioRouter.hpp"
#include "simrv/memory/TileLinkNode.hpp"

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

    [[nodiscard]] auto router() -> MmioRouter& { return router_; }
    [[nodiscard]] auto router() const -> const MmioRouter& { return router_; }

    [[nodiscard]] auto coherence_hub() -> CoherenceHub& { return coherence_hub_; }
    [[nodiscard]] auto coherence_hub() const -> const CoherenceHub& { return coherence_hub_; }

    auto send_request(const TlChannelA& req) -> bool override;
    auto get_response(uint8_t source_id, TlChannelD& resp) -> bool override;
    auto try_get_timed_response(uint8_t source_id, TimedResponse& resp) -> bool;
    void cancel_source(uint8_t source_id);

    /// Advance deterministic arbitration by one interconnect clock.
    void advance_cycle();
    [[nodiscard]] auto cycle() const noexcept -> Cycle { return cycle_; }
    [[nodiscard]] auto pending_requests() const noexcept -> size_t { return req_queue_.size(); }
    [[nodiscard]] auto pending_responses() const noexcept -> size_t { return resp_queue_.size(); }

    auto acquire_block(const TlChannelA& req, TlChannelD& resp,
                       std::array<Byte, CoherenceHub::kLineBytes>& line_data) -> bool {
        return coherence_hub_.handle_acquire(req, resp, line_data);
    }

    auto acquire_perm(const TlChannelA& req, TlChannelD& resp) -> bool {
        std::array<Byte, CoherenceHub::kLineBytes> unused{};
        return coherence_hub_.handle_acquire(req, resp, unused);
    }

    auto release_line(const TlChannelC& req, TlChannelD& resp,
                      const std::array<Byte, CoherenceHub::kLineBytes>* data = nullptr) -> bool {
        return coherence_hub_.handle_release(req, resp, data);
    }

    void grant_ack(const TlChannelE& ack) { coherence_hub_.process_grant_ack(ack); }

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

    void process_request(const TimedRequest& request);

    simrv::core::Machine& machine_;
    uint64_t read_count_ = 0;
    uint64_t write_count_ = 0;

    MmioRouter router_;
    CoherenceHub coherence_hub_;
    Cycle cycle_ = 0;
    uint64_t next_sequence_ = 0;
    std::deque<TimedRequest> req_queue_;
    std::vector<TimedResponse> resp_queue_;
};

}  // namespace simrv::memory
