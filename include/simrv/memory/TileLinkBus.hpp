/**
 * @file TileLinkBus.hpp
 * @brief TileLink-style sequential bus implementation.
 */
#pragma once

#include <cstdint>
#include <queue>
#include <vector>

#include "simrv/memory/Bus.hpp"
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

    auto send_request(const TlChannelA& req) -> bool override;
    auto get_response(uint8_t source_id, TlChannelD& resp) -> bool override;

    [[nodiscard]] auto read_count() const -> uint64_t override { return read_count_ + router_.mmio_read_count(); }
    [[nodiscard]] auto write_count() const -> uint64_t override { return write_count_ + router_.mmio_write_count(); }

   private:
    void tick();

    simrv::core::Machine& machine_;
    uint64_t read_count_ = 0;
    uint64_t write_count_ = 0;

    MmioRouter router_;
    std::queue<TlChannelA> req_queue_;
    std::vector<TlChannelD> resp_queue_;
};

}  // namespace simrv::memory