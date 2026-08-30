/**
 * @file TileLinkBus.cpp
 * @brief Internal TileLink-C 1.8.1 profile fabric and transaction adapter.
 */
#include "simrv/memory/TileLinkBus.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <unordered_set>

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
    SmpLockGuard lock(bus_mutex_, machine_.configuration().execution.smp_multithreaded);
    request_latency_ = std::max(1u, request_latency);
    response_latency_ = std::max(1u, response_latency);
}

auto TileLinkBus::send_request(const TlChannelA& req) -> bool {
    SmpLockGuard lock(bus_mutex_, machine_.configuration().execution.smp_multithreaded);
    if (const auto valid = protocol_checker_.accept_a(req); !valid) {
        simrv::log::warn("TileLink-C request rejected: {}", valid.error());
        return false;
    }
    req_queue_.push_back(
        TimedRequest{.payload = req, .submitted_cycle = cycle_, .sequence = next_sequence_++});
    return true;
}

void TileLinkBus::advance_cycle() {
    SmpLockGuard lock(bus_mutex_, machine_.configuration().execution.smp_multithreaded);
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
    resp.denied = false;
    resp.corrupt = false;
    resp.data = 0;

    const Instruction funct3 = req.size;
    const bool valid_size = req.size <= 3;
    const size_t transfer_bytes = valid_size ? (size_t{1} << req.size) : 0;
    bool handled = false;
    std::array<Byte, CoherenceHub::kLineBytes> response_line{};
    bool has_line_data = false;

    if (req.opcode == TlOpcodeA::AcquireBlock || req.opcode == TlOpcodeA::AcquirePerm) {
        handled = coherence_hub_.handle_acquire(req, resp, response_line);
        has_line_data = req.opcode == TlOpcodeA::AcquireBlock && handled && !resp.failed();
    } else if (!valid_size) {
        resp.denied = true;
        handled = true;
    } else if (req.opcode == TlOpcodeA::Get) {
        resp.opcode = TlOpcodeD::AccessAckData;
        if (machine_.memory_geometry().contains(req.address.raw(), transfer_bytes) &&
            machine_.ram_data() != nullptr) {
            coherence_hub_.invalidate_line_external(req.address.raw());
            resp.data =
                simrv::memory::ram_read_fast(req.address.raw(), funct3, machine_.ram_view());
            ++read_count_;
            handled = true;
        }
    } else if (req.opcode == TlOpcodeA::LogicalData && req.logical == TlLogical::Or &&
               !req.corrupt) {
        // Page-table A/D updates use an atomic OR at the globally ordered bus boundary.
        resp.opcode = TlOpcodeD::AccessAckData;
        if (machine_.memory_geometry().contains(req.address.raw(), transfer_bytes) &&
            machine_.ram_data() != nullptr) {
            coherence_hub_.invalidate_line_external(req.address.raw());
            const Word previous =
                simrv::memory::ram_read_fast(req.address.raw(), funct3, machine_.ram_view());
            resp.data = previous;
            simrv::memory::ram_write_fast(req.address.raw(), previous | req.data, funct3,
                                          machine_.ram_view());
            ++read_count_;
            ++write_count_;
            handled = true;
        }
    } else if ((req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData) &&
               !req.corrupt) {
        resp.opcode = TlOpcodeD::AccessAck;
        const bool is_tohost_write = simrv::xlen::kIsXLen64
                                         ? (funct3 == static_cast<Instruction>(Funct3::Sw) ||
                                            funct3 == static_cast<Instruction>(Funct3::Sd))
                                         : (funct3 == static_cast<Instruction>(Funct3::Sw));
        if (is_tohost_write) {
            if (req.address == machine_.configuration().isa.isatest_tohost ||
                req.address == 0x80001000 || req.address == 0x40008000) {
                machine_.tohost = simrv::xlen::kIsXLen64
                                      ? req.data
                                      : ((machine_.tohost & 0xFFFFFFFF00000000ULL) | req.data);
            } else if (!simrv::xlen::kIsXLen64 &&
                       (req.address == machine_.configuration().isa.isatest_tohost + 4 ||
                        req.address == 0x80001004 || req.address == 0x40008004)) {
                machine_.tohost = (machine_.tohost & 0x00000000FFFFFFFFULL) |
                                  (static_cast<uint64_t>(req.data) << 32);
            }
        }

        if (machine_.memory_geometry().contains(req.address.raw(), transfer_bytes) &&
            machine_.ram_data() != nullptr) {
            coherence_hub_.invalidate_line_external(req.address.raw());
            if (req.opcode == TlOpcodeA::PutPartialData) {
                auto* destination = machine_.ram_view().unchecked_ptr(req.address.raw());
                const unsigned lane_base =
                    static_cast<unsigned>(req.address.raw() & (kTlBeatBytes - 1u));
                for (size_t byte = 0; byte < transfer_bytes; ++byte) {
                    const auto lane = static_cast<unsigned>(lane_base + byte);
                    if ((req.mask & (TlMask{1} << lane)) != 0) {
                        destination[byte] = static_cast<Byte>((req.data >> (byte * 8u)) & 0xffu);
                    }
                }
            } else {
                simrv::memory::ram_write_fast(req.address.raw(), req.data, funct3,
                                              machine_.ram_view());
            }
            ++write_count_;
            handled = true;
        }
    } else {
        if (req.opcode == TlOpcodeA::Intent) {
            resp.opcode = TlOpcodeD::HintAck;
        } else if (req.opcode == TlOpcodeA::PutFullData ||
                   req.opcode == TlOpcodeA::PutPartialData) {
            resp.opcode = TlOpcodeD::AccessAck;
        } else {
            resp.opcode = TlOpcodeD::AccessAckData;
        }
        resp.denied = true;
        handled = true;
    }

    if (!handled) {
        handled = router_.route_request(req, resp);
    }

    if (!handled) {
        resp.denied = true;
    }
    const uint8_t beat_count = has_line_data ? kTlBlockBytes / kTlBeatBytes : 1;
    for (uint8_t beat = 0; beat < beat_count; ++beat) {
        TlChannelD payload = resp;
        if (has_line_data) {
            std::memcpy(&payload.data, response_line.data() + beat * kTlBeatBytes, kTlBeatBytes);
        }
        d_queue_.push_back(TimedDBeat{
            .payload = payload,
            // A latency of one exposes the first beat in the request-completion cycle. Each
            // following beat occupies its own D-channel transfer cycle.
            .ready_cycle = cycle_ + response_latency_ - 1 + beat,
            .sequence = request.sequence,
            .beat_index = beat,
            .beat_count = beat_count,
        });
    }
}

auto TileLinkBus::try_get_timed_response(TlSourceId source_id, TimedResponse& resp) -> bool {
    SmpLockGuard lock(bus_mutex_, machine_.configuration().execution.smp_multithreaded);
    return consume_d_beat(source_id, true, resp);
}

void TileLinkBus::cancel_source(TlSourceId source_id) {
    SmpLockGuard lock(bus_mutex_, machine_.configuration().execution.smp_multithreaded);
    std::erase_if(req_queue_, [source_id](const TimedRequest& request) {
        return request.payload.source == source_id;
    });
    std::erase_if(d_queue_, [source_id](const TimedDBeat& response) {
        return response.payload.source == source_id;
    });
    d_assemblies_.erase(source_id);
    protocol_checker_.cancel(source_id);
}

auto TileLinkBus::get_response(TlSourceId source_id, TlChannelD& resp) -> bool {
    SmpLockGuard lock(bus_mutex_, machine_.configuration().execution.smp_multithreaded);
    // Functional engines use the transaction adapter and drain all constituent beats without
    // advancing simulated time. Cycle engines consume at most one ready beat per call.
    const auto pop_response = [&]() -> bool {
        TimedResponse response{};
        while (consume_d_beat(source_id, false, response)) {
            resp = response.payload;
            return true;
        }
        return false;
    };
    while (true) {
        if (pop_response()) return true;
        if (std::ranges::any_of(d_queue_, [source_id](const auto& item) {
                return item.payload.source == source_id;
            })) {
            continue;
        }
        if (req_queue_.empty()) {
            break;
        }
        const auto request = req_queue_.front();
        req_queue_.pop_front();
        process_request(request);
    }
    return false;
}

auto TileLinkBus::consume_d_beat(TlSourceId source_id, bool honor_ready, TimedResponse& response)
    -> bool {
    auto beat = std::ranges::find_if(d_queue_, [this, source_id, honor_ready](const auto& item) {
        return item.payload.source == source_id && (!honor_ready || item.ready_cycle <= cycle_);
    });
    if (beat == d_queue_.end()) return false;

    auto [assembly_it, inserted] = d_assemblies_.try_emplace(source_id);
    auto& assembly = assembly_it->second;
    if (inserted) {
        assembly.response.payload = beat->payload;
        assembly.response.ready_cycle = beat->ready_cycle;
        assembly.response.sequence = beat->sequence;
        assembly.response.has_line_data = beat->beat_count > 1;
        assembly.beat_count = beat->beat_count;
    }
    const bool stable = beat->sequence == assembly.response.sequence &&
                        beat->beat_count == assembly.beat_count &&
                        beat->beat_index == assembly.next_beat &&
                        beat->payload.opcode == assembly.response.payload.opcode &&
                        beat->payload.cap == assembly.response.payload.cap &&
                        beat->payload.size == assembly.response.payload.size &&
                        beat->payload.source == assembly.response.payload.source &&
                        beat->payload.sink == assembly.response.payload.sink &&
                        beat->payload.denied == assembly.response.payload.denied &&
                        beat->payload.corrupt == assembly.response.payload.corrupt;
    if (!stable) {
        assembly.response.payload.denied = true;
        simrv::log::warn("TileLink-C D multibeat stability violation for source {}", source_id);
    }
    if (assembly.response.has_line_data && beat->beat_index < assembly.beat_count) {
        std::memcpy(assembly.response.line_data.data() + beat->beat_index * kTlBeatBytes,
                    &beat->payload.data, kTlBeatBytes);
    } else {
        assembly.response.payload.data = beat->payload.data;
    }
    ++assembly.next_beat;
    d_queue_.erase(beat);

    if (assembly.next_beat != assembly.beat_count) return false;
    if (const auto valid = protocol_checker_.accept_d(assembly.response.payload); !valid) {
        simrv::log::warn("TileLink-C response violation: {}", valid.error());
        assembly.response.payload.denied = true;
    }
    response = assembly.response;
    d_assemblies_.erase(assembly_it);
    return true;
}

auto TileLinkBus::pending_responses() const noexcept -> size_t {
    std::unordered_set<TlSourceId> transactions;
    for (const auto& [source, unused] : d_assemblies_) {
        (void)unused;
        transactions.insert(source);
    }
    for (const auto& beat : d_queue_) {
        transactions.insert(beat.payload.source);
    }
    return transactions.size();
}

auto TileLinkBus::acquire_block(const TlChannelA& req, TlChannelD& resp,
                                std::array<Byte, CoherenceHub::kLineBytes>& line_data) -> bool {
    SmpLockGuard lock(bus_mutex_, machine_.configuration().execution.smp_multithreaded);
    if (const auto valid = protocol_checker_.accept_a(req); !valid) return false;
    const bool handled = coherence_hub_.handle_acquire(req, resp, line_data);
    if (!handled) {
        protocol_checker_.cancel(req.source);
        return false;
    }
    return protocol_checker_.accept_d(resp).has_value();
}

auto TileLinkBus::acquire_perm(const TlChannelA& req, TlChannelD& resp) -> bool {
    std::array<Byte, CoherenceHub::kLineBytes> unused{};
    return acquire_block(req, resp, unused);
}

void TileLinkBus::grant_ack(const TlChannelE& ack) {
    SmpLockGuard lock(bus_mutex_, machine_.configuration().execution.smp_multithreaded);
    if (const auto valid = protocol_checker_.accept_e(ack); !valid) {
        simrv::log::warn("TileLink-C GrantAck violation: {}", valid.error());
        return;
    }
    coherence_hub_.process_grant_ack(ack);
}

}  // namespace simrv::memory
