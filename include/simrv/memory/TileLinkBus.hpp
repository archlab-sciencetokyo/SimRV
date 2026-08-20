/**
 * @file TileLinkBus.hpp
 * @brief TileLink-style sequential bus implementation.
 */
#pragma once

#include <cstdint>
#include <queue>
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
    explicit TileLinkBus(simrv::core::Machine& machine);

    void add_node(TileLinkNode* node);

    [[nodiscard]] auto router() -> MmioRouter& { return router_; }
    [[nodiscard]] auto router() const -> const MmioRouter& { return router_; }

    [[nodiscard]] auto coherence_hub() -> CoherenceHub& { return coherence_hub_; }
    [[nodiscard]] auto coherence_hub() const -> const CoherenceHub& { return coherence_hub_; }

    auto send_request(const TlChannelA& req) -> bool override;
    auto get_response(uint8_t source_id, TlChannelD& resp) -> bool override;

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

    void grant_ack(const TlChannelE& ack) {
        coherence_hub_.process_grant_ack(ack);
    }

    [[nodiscard]] auto read_count() const -> uint64_t override {
        return read_count_ + router_.mmio_read_count() + coherence_hub_.stats().acquire_count;
    }
    [[nodiscard]] auto write_count() const -> uint64_t override {
        return write_count_ + router_.mmio_write_count() + coherence_hub_.stats().writeback_count;
    }

   private:
    void tick();

    simrv::core::Machine& machine_;
    uint64_t read_count_ = 0;
    uint64_t write_count_ = 0;

    MmioRouter router_;
    CoherenceHub coherence_hub_;
    std::queue<TlChannelA> req_queue_;
    std::vector<TlChannelD> resp_queue_;
};

}  // namespace simrv::memory