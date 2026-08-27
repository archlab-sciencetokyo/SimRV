/**
 * @file TileLinkBus.cpp
 * @brief TileLink-style simple sequential bus implementation.
 */
#include "simrv/memory/TileLinkBus.hpp"

#include <algorithm>
#include <cstdint>
#include <format>

#include "simrv/Define.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

using simrv::isa::Funct3;

namespace {
template <typename MutexT>
struct SmpLockGuard {
    MutexT& mutex_;
    bool locked_;
    SIMRV_ALWAYS_INLINE SmpLockGuard(MutexT& mutex, bool active) noexcept
        : mutex_(mutex), locked_(active) {
        if (simrv::compiler::unlikely(locked_)) mutex_.lock();
    }
    SIMRV_ALWAYS_INLINE ~SmpLockGuard() noexcept {
        if (simrv::compiler::unlikely(locked_)) mutex_.unlock();
    }
};
}  // namespace

TileLinkBus::TileLinkBus(simrv::core::Machine& machine)
    : machine_(machine), coherence_hub_(machine) {}

void TileLinkBus::add_node(TileLinkNode* node) { router_.register_device(node); }

void TileLinkBus::configure_timing(uint32_t request_latency, uint32_t response_latency) {
    SmpLockGuard lock(bus_mutex_, machine_.s_smp_multithreaded.load(std::memory_order_relaxed));
    request_latency_ = std::max(1u, request_latency);
    response_latency_ = std::max(1u, response_latency);
}

auto TileLinkBus::send_request(const TlChannelA& req) -> bool {
    SmpLockGuard lock(bus_mutex_, machine_.s_smp_multithreaded.load(std::memory_order_relaxed));
    req_queue_.push_back(
        TimedRequest{.payload = req, .submitted_cycle = cycle_, .sequence = next_sequence_++});
    return true;
}

void TileLinkBus::advance_cycle() {
    SmpLockGuard lock(bus_mutex_, machine_.s_smp_multithreaded.load(std::memory_order_relaxed));
    ++cycle_;
    if (!req_queue_.empty() && req_queue_.front().submitted_cycle + request_latency_ <= cycle_) {
        auto request = std::move(req_queue_.front());
        req_queue_.pop_front();
        process_request(request);
    }
}

void TileLinkBus::process_request(const TimedRequest& request) {
    auto req = request.payload;

    if (req.mask == 0) {
        req.mask = TlChannelA::compute_mask(req.size, req.address);
    }

    TlChannelD resp{};
    resp.source = req.source;
    resp.size = req.size;
    resp.error = false;
    resp.data = 0;

    const Instruction funct3 = req.size;
    const bool valid_size = req.size <= 3;
    const size_t transfer_bytes = valid_size ? (size_t{1} << req.size) : 0;
    bool handled = false;
    std::array<Byte, CoherenceHub::kLineBytes> response_line{};
    bool has_line_data = false;

    if (req.opcode == TlOpcodeA::AcquireBlock || req.opcode == TlOpcodeA::AcquirePerm) {
        handled = coherence_hub_.handle_acquire(req, resp, response_line);
        has_line_data = req.opcode == TlOpcodeA::AcquireBlock && handled && !resp.error;
    } else if (!valid_size) {
        resp.error = true;
        handled = true;
    } else if (req.opcode == TlOpcodeA::Get) {
        resp.opcode = TlOpcodeD::AccessAckData;
        if (machine_.memory_geometry().contains(req.address, transfer_bytes) &&
            machine_.ram_data() != nullptr) {
            bool found_in_cache = false;
            for (uint32_t h = 0; h < machine_.num_harts(); ++h) {
                Word cached_data = 0;
                if (machine_.hart(h).dcache.read(req.address, cached_data, funct3)) {
                    resp.data = cached_data;
                    found_in_cache = true;
                    break;
                }
            }
            if (!found_in_cache) {
                resp.data = simrv::memory::ram_read_fast(req.address, funct3, machine_.ram_view());
            }
            ++read_count_;
            handled = true;
        }
    } else if (req.opcode == TlOpcodeA::LogicalData) {
        // Page-table A/D updates use an atomic OR at the globally ordered bus boundary.
        resp.opcode = TlOpcodeD::AccessAckData;
        if (machine_.memory_geometry().contains(req.address, transfer_bytes) &&
            machine_.ram_data() != nullptr) {
            coherence_hub_.invalidate_line_external(req.address);
            const Word previous =
                simrv::memory::ram_read_fast(req.address, funct3, machine_.ram_view());
            resp.data = previous;
            simrv::memory::ram_write_fast(req.address, previous | req.data, funct3,
                                          machine_.ram_view());
            ++read_count_;
            ++write_count_;
            handled = true;
        }
    } else if (req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData) {
        resp.opcode = TlOpcodeD::AccessAck;
        const bool is_tohost_write = simrv::xlen::kIsXLen64
                                         ? (funct3 == static_cast<Instruction>(Funct3::Sw) ||
                                            funct3 == static_cast<Instruction>(Funct3::Sd))
                                         : (funct3 == static_cast<Instruction>(Funct3::Sw));
        if (is_tohost_write) {
            if (req.address == machine_.s_isatest_tohost || req.address == 0x80001000 ||
                req.address == 0x40008000) {
                machine_.tohost = simrv::xlen::kIsXLen64
                                      ? req.data
                                      : ((machine_.tohost & 0xFFFFFFFF00000000ULL) | req.data);
            } else if (!simrv::xlen::kIsXLen64 &&
                       (req.address == machine_.s_isatest_tohost + 4 || req.address == 0x80001004 ||
                        req.address == 0x40008004)) {
                machine_.tohost = (machine_.tohost & 0x00000000FFFFFFFFULL) |
                                  (static_cast<uint64_t>(req.data) << 32);
            }
        }

        if (machine_.memory_geometry().contains(req.address, transfer_bytes) &&
            machine_.ram_data() != nullptr) {
            simrv::memory::ram_write_fast(req.address, req.data, funct3, machine_.ram_view());
            ++write_count_;
            handled = true;
        }
    } else {
        resp.error = true;
        handled = true;
    }

    if (!handled) {
        handled = router_.route_request(req, resp);
    }

    if (!handled) {
        resp.error = true;
    }
    resp_queue_.push_back(TimedResponse{.payload = resp,
                                        .line_data = response_line,
                                        // A latency of one means that the response is visible in
                                        // the cycle in which the request completes.  This keeps
                                        // the default one-cycle request/response contract while
                                        // larger configured latencies add whole CA transitions.
                                        .ready_cycle = cycle_ + response_latency_ - 1,
                                        .sequence = request.sequence,
                                        .has_line_data = has_line_data});
}

auto TileLinkBus::try_get_timed_response(uint8_t source_id, TimedResponse& resp) -> bool {
    SmpLockGuard lock(bus_mutex_, machine_.s_smp_multithreaded.load(std::memory_order_relaxed));
    auto it = std::ranges::find_if(resp_queue_, [this, source_id](const auto& r) -> bool {
        return r.payload.source == source_id && r.ready_cycle <= cycle_;
    });
    if (it != resp_queue_.end()) {
        resp = *it;
        resp_queue_.erase(it);
        return true;
    }
    return false;
}

void TileLinkBus::cancel_source(uint8_t source_id) {
    SmpLockGuard lock(bus_mutex_, machine_.s_smp_multithreaded.load(std::memory_order_relaxed));
    std::erase_if(req_queue_, [source_id](const TimedRequest& request) {
        return request.payload.source == source_id;
    });
    std::erase_if(resp_queue_, [source_id](const TimedResponse& response) {
        return response.payload.source == source_id;
    });
}

auto TileLinkBus::get_response(uint8_t source_id, TlChannelD& resp) -> bool {
    SmpLockGuard lock(bus_mutex_, machine_.s_smp_multithreaded.load(std::memory_order_relaxed));
    // IA deliberately remains functional and synchronous.  CA uses
    // try_get_timed_response(), which honours ready_cycle exactly.
    const auto pop_response = [&]() -> bool {
        auto response = std::ranges::find_if(resp_queue_, [source_id](const auto& item) {
            return item.payload.source == source_id;
        });
        if (response == resp_queue_.end()) return false;
        resp = response->payload;
        resp_queue_.erase(response);
        return true;
    };
    while (true) {
        if (pop_response()) return true;
        if (req_queue_.empty()) {
            break;
        }
        const auto request = req_queue_.front();
        req_queue_.pop_front();
        process_request(request);
    }
    return false;
}

}  // namespace simrv::memory
