/**
 * @file TemplatedBus.cpp
 * @brief Explicit NTTP instantiations and shared implementation for TemplatedBus<Spec>.
 */
#include "simrv/memory/TemplatedBus.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <tuple>
#include <unordered_set>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

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

[[nodiscard]] constexpr auto axi_id_fits(AxiId id, uint8_t id_width_bits) noexcept -> bool {
    if (id_width_bits >= 32) return true;
    return id < (AxiId{1} << id_width_bits);
}

[[nodiscard]] constexpr auto axi_burst_valid(AxiBurst burst, uint8_t size, uint8_t len,
                                             uint16_t max_burst_beats, bool supports_burst,
                                             simrv::xlen::Address addr) noexcept -> bool {
    if (burst == AxiBurst::Reserved) return false;
    const uint32_t beats = static_cast<uint32_t>(len) + 1U;
    if (!supports_burst && beats > 1U) return false;
    if (beats > max_burst_beats) return false;
    const uint32_t beat_bytes = uint32_t{1} << size;
    if (size > 5 || beat_bytes == 0) return false;
    if (burst == AxiBurst::Wrap) {
        if (beats != 2 && beats != 4 && beats != 8 && beats != 16) return false;
        if ((addr & (beat_bytes - 1U)) != 0) return false;
    }
    return true;
}

[[nodiscard]] constexpr auto axi_beat_address(simrv::xlen::Address base_addr, uint32_t beat_bytes,
                                              uint32_t total_beats, AxiBurst burst,
                                              uint32_t beat_idx) noexcept -> simrv::xlen::Address {
    switch (burst) {
        case AxiBurst::Fixed:
            return base_addr;
        case AxiBurst::Incr:
            return base_addr + static_cast<simrv::xlen::Address>(beat_idx) * beat_bytes;
        case AxiBurst::Wrap: {
            const simrv::xlen::Address wrap_size =
                static_cast<simrv::xlen::Address>(beat_bytes) * total_beats;
            const simrv::xlen::Address wrap_boundary = base_addr & ~(wrap_size - 1U);
            const simrv::xlen::Address aligned =
                base_addr + static_cast<simrv::xlen::Address>(beat_idx) * beat_bytes;
            return wrap_boundary + (aligned & (wrap_size - 1U));
        }
        default:
            return base_addr;
    }
}

[[nodiscard]] constexpr auto size_to_axi_size(uint8_t size_bytes) noexcept -> uint8_t {
    switch (size_bytes) {
        case 1:
            return 0;
        case 2:
            return 1;
        case 4:
            return 2;
        case 8:
            return 3;
        default:
            return 3;
    }
}

}  // namespace

template <BusProtocolSpec Spec>
TemplatedBus<Spec>::TemplatedBus(simrv::core::Machine& machine)
    : machine_(machine), coherence_hub_(machine) {}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::record_transaction(TileLinkChannel ch, std::string_view opcode,
                                            TlSourceId source, TlSinkId sink,
                                            simrv::xlen::PhysAddr address, std::string_view detail,
                                            const simrv::core::BusTraceContext* trace_context) {
    if (transaction_history_.size() >= kMaxTransactionHistory) {
        transaction_history_.pop_front();
    }
    const auto current_cycle = cycle_.load(std::memory_order_relaxed);
    transaction_history_.push_back(TlTransactionRecord{
        .cycle = current_cycle,
        .channel = ch,
        .opcode = opcode,
        .source = source,
        .sink = sink,
        .address = address,
        .detail = detail,
    });
    if (machine_.trace().is_architecture_trace_enabled()) {
        machine_.trace().log_bus_transaction(
            current_cycle, to_char(ch), opcode, source, sink, address, detail,
            trace_context != nullptr ? *trace_context : simrv::core::BusTraceContext{});
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::add_node(TileLinkNode* node) -> bool {
    return router_.register_device(node);
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::configure_timing(uint32_t request_latency, uint32_t response_latency,
                                          uint32_t data_request_latency,
                                          uint32_t data_response_latency,
                                          uint32_t startup_data_response_latency) {
    SmpLockGuard lock(bus_mutex_, is_smp_enabled());
    request_latency_ = std::max(1u, request_latency);
    response_latency_ = std::max(1u, response_latency);
    data_request_latency_ = data_request_latency == 0 ? request_latency_ : data_request_latency;
    data_response_latency_ = data_response_latency == 0 ? response_latency_ : data_response_latency;
    startup_data_response_latency_ = startup_data_response_latency;
    data_response_count_ = 0;
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::send_request(const TlChannelA& req) -> bool {
    SmpLockGuard lock(bus_mutex_, is_smp_enabled());

    if constexpr (!Spec.supports_coherence) {
        if (req.opcode == TlOpcodeA::AcquireBlock || req.opcode == TlOpcodeA::AcquirePerm) {
            simrv::log::warn("{} bus does not support coherence opcodes",
                             bus_protocol_name(Spec.protocol));
            return false;
        }
    }
    if constexpr (!Spec.supports_atomics) {
        if (req.opcode == TlOpcodeA::ArithmeticData || req.opcode == TlOpcodeA::LogicalData) {
            simrv::log::warn("{} bus does not support atomic opcodes",
                             bus_protocol_name(Spec.protocol));
            return false;
        }
    }
    if constexpr (!Spec.supports_burst) {
        if (req.size > 3) {
            simrv::log::warn("{} bus does not support multi-beat bursts (size={})",
                             bus_protocol_name(Spec.protocol), req.size);
            return false;
        }
    }

    if constexpr (Spec.is_tilelink()) {
        if (const auto valid = protocol_checker_.accept_a(req); !valid) {
            simrv::log::warn("{} request rejected: {}", bus_protocol_name(Spec.protocol),
                             valid.error());
            return false;
        }
    } else {
        if (req.opcode == TlOpcodeA::Intent) {
            simrv::log::warn("{} bus does not support Hint/Intent opcodes",
                             bus_protocol_name(Spec.protocol));
            return false;
        }
        if (axi_tracer_.is_enabled()) {
            const auto current_cycle = cycle_.load(std::memory_order_relaxed);
            const uint8_t burst_len =
                req.size > kTlBeatSize ? static_cast<uint8_t>((1u << (req.size - kTlBeatSize)) - 1u)
                                       : 0u;
            if (req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData) {
                axi_tracer_.trace_aw(current_cycle, Axi4Aw{.id = req.source,
                                                           .addr = req.address.raw(),
                                                           .len = burst_len,
                                                           .size = std::min<uint8_t>(req.size, 3),
                                                           .burst = AxiBurst::Incr});
                axi_tracer_.trace_w(current_cycle,
                                    Axi4W{.data = static_cast<simrv::xlen::Word>(req.data),
                                          .strb = static_cast<uint32_t>(req.mask),
                                          .last = true});
            } else {
                axi_tracer_.trace_ar(current_cycle, Axi4Ar{.id = req.source,
                                                           .addr = req.address.raw(),
                                                           .len = burst_len,
                                                           .size = std::min<uint8_t>(req.size, 3),
                                                           .burst = AxiBurst::Incr});
            }
        }
    }

    const bool data_port = (req.source & 1u) == static_cast<TlSourceId>(TlPort::Data);
    simrv::core::BusTraceContext trace_context;
    const bool capture_trace_context = machine_.trace().is_architecture_trace_enabled() &&
                                       machine_.configuration().debug.trace_level >= 4;
    const auto current_cycle = cycle_.load(std::memory_order_relaxed);
    if (capture_trace_context) {
        const auto* target = router_.resolve_device(req.address);
        trace_context = {
            .sequence = next_sequence_,
            .request_cycle = current_cycle,
            .completion_cycle = std::nullopt,
            .latency_cycles = std::nullopt,
            .hart = req.hart,
            .transfer_size = req.size,
            .burst_length = static_cast<uint8_t>(
                req.size > kTlBeatSize ? (1u << (req.size - kTlBeatSize)) : 1u),
            .data = req.data,
            .byte_enable = req.mask,
            .denied = std::nullopt,
            .corrupt = std::nullopt,
            .master = std::format("hart-{}-{}", req.hart.raw(), data_port ? "data" : "instruction"),
            .target = target != nullptr                                  ? target->name()
                      : machine_.memory_geometry().contains(req.address) ? "ram"
                                                                         : "unmapped",
        };
    }
    record_transaction(TileLinkChannel::A, to_string(req.opcode), req.source, 0, req.address,
                       to_string(req.grow), capture_trace_context ? &trace_context : nullptr);
    const auto logical_cycle =
        req.logical_cycle == std::numeric_limits<Cycle>::max() ? current_cycle : req.logical_cycle;
    TimedRequest timed_request{
        .payload = req,
        .submitted_cycle = logical_cycle,
        .request_latency = data_port ? data_request_latency_ : request_latency_,
        .sequence = next_sequence_++};
    const auto position = std::ranges::upper_bound(
        req_queue_, timed_request, [](const TimedRequest& lhs, const TimedRequest& rhs) {
            const auto lhs_issue =
                lhs.payload.logical_issue_sequence == std::numeric_limits<uint64_t>::max()
                    ? lhs.sequence
                    : lhs.payload.logical_issue_sequence;
            const auto rhs_issue =
                rhs.payload.logical_issue_sequence == std::numeric_limits<uint64_t>::max()
                    ? rhs.sequence
                    : rhs.payload.logical_issue_sequence;
            return std::tuple{lhs.submitted_cycle, lhs.payload.hart.raw(), lhs_issue,
                              lhs.sequence} <
                   std::tuple{rhs.submitted_cycle, rhs.payload.hart.raw(), rhs_issue, rhs.sequence};
        });
    req_queue_.insert(position, std::move(timed_request));
    has_pending_requests_.store(true, std::memory_order_release);
    return true;
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::contention_stats() const -> ContentionStats {
    const auto current_cycle = cycle_.load(std::memory_order_relaxed);
    const auto dma_config = machine_.dma_engine().config();
    return ContentionStats{
        .dma_active = machine_.dma_engine().is_transfer_active(current_cycle),
        .dma_penalty_cycles = dma_config.memory_contention_penalty,
        .delayed_requests = dma_delayed_requests_.load(std::memory_order_relaxed),
        .added_latency_cycles = dma_added_latency_cycles_.load(std::memory_order_relaxed),
    };
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::advance_cycle_slow() {
    SmpLockGuard lock(bus_mutex_, is_smp_enabled());
    const auto current_cycle = cycle_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!req_queue_.empty() &&
        req_queue_.front().submitted_cycle + req_queue_.front().request_latency <= current_cycle) {
        auto request = std::move(req_queue_.front());
        req_queue_.pop_front();
        has_pending_requests_.store(!req_queue_.empty(), std::memory_order_release);
        process_request(request);
    }
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::process_request(const TimedRequest& request) {
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

    const simrv::xlen::Instruction funct3 = req.size;
    const bool valid_size = req.size <= 3;
    const size_t transfer_bytes = valid_size ? (size_t{1} << req.size) : 0;
    bool handled = false;
    uint32_t cache_service_latency = 1;
    std::array<simrv::xlen::Byte, CoherenceHub::kLineBytes> response_line{};
    bool has_line_data = false;

    if constexpr (Spec.supports_coherence) {
        if (req.opcode == TlOpcodeA::AcquireBlock || req.opcode == TlOpcodeA::AcquirePerm) {
            handled =
                coherence_hub_.handle_acquire(req, resp, response_line, cache_service_latency);
            has_line_data = req.opcode == TlOpcodeA::AcquireBlock && handled && !resp.failed();
        } else if (req.opcode == TlOpcodeA::Intent) {
            const size_t block_bytes = (size_t{1} << std::min<uint8_t>(req.size, kTlBlockSize));
            if (machine_.memory_geometry().contains(req.address, block_bytes) &&
                machine_.ram_data() != nullptr) {
                handled = coherence_hub_.handle_intent(req, resp);
            } else {
                resp.opcode = TlOpcodeD::HintAck;
                resp.denied = true;
                handled = true;
            }
        }
    }

    if (!handled) {
        if (!valid_size) {
            if (req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData) {
                resp.opcode = TlOpcodeD::AccessAck;
            } else {
                resp.opcode = TlOpcodeD::AccessAckData;
            }
            resp.denied = true;
            handled = true;
        } else if (req.opcode == TlOpcodeA::Get) {
            resp.opcode = TlOpcodeD::AccessAckData;
            if (machine_.memory_geometry().contains(req.address, transfer_bytes) &&
                machine_.ram_data() != nullptr) {
                if constexpr (Spec.supports_coherence) {
                    coherence_hub_.invalidate_line_external(req.address);
                }
                resp.data = simrv::memory::ram_read_fast(req.address, funct3, machine_.ram_view());
                ++read_count_;
                handled = true;
            }
        } else if (Spec.supports_atomics && req.opcode == TlOpcodeA::LogicalData &&
                   req.logical == TlLogical::Or && !req.corrupt) {
            resp.opcode = TlOpcodeD::AccessAckData;
            if (machine_.memory_geometry().contains(req.address, transfer_bytes) &&
                machine_.ram_data() != nullptr) {
                if constexpr (Spec.supports_coherence) {
                    coherence_hub_.invalidate_line_external(req.address);
                }
                const simrv::xlen::Word previous =
                    simrv::memory::ram_read_fast(req.address, funct3, machine_.ram_view());
                resp.data = previous;
                simrv::memory::ram_write_fast(req.address, previous | req.data, funct3,
                                              machine_.ram_view());
                ++read_count_;
                ++write_count_;
                handled = true;
            }
        } else if ((req.opcode == TlOpcodeA::PutFullData ||
                    req.opcode == TlOpcodeA::PutPartialData) &&
                   !req.corrupt) {
            resp.opcode = TlOpcodeD::AccessAck;
            const bool is_tohost_write =
                simrv::xlen::kIsXLen64
                    ? (funct3 == static_cast<simrv::xlen::Instruction>(Funct3::Sw) ||
                       funct3 == static_cast<simrv::xlen::Instruction>(Funct3::Sd))
                    : (funct3 == static_cast<simrv::xlen::Instruction>(Funct3::Sw));
            if (is_tohost_write) {
                const bool is_guest_xlen32 =
                    (machine_.primary_hart().state().regs.xlen == 32) || !simrv::xlen::kIsXLen64;
                if (req.address == machine_.isa_test_tohost() || req.address == 0x80001000 ||
                    req.address == 0x40008000) {
                    const bool low_word =
                        funct3 == static_cast<simrv::xlen::Instruction>(Funct3::Sw);
                    machine_.publish_tohost_write(
                        low_word || is_guest_xlen32 ? ((machine_.tohost & 0xFFFFFFFF00000000ULL) |
                                                       (req.data & 0xFFFFFFFFULL))
                                                    : req.data,
                        low_word && !is_guest_xlen32 ? core::Machine::TohostWriteKind::LowWord
                                                     : core::Machine::TohostWriteKind::Full);
                } else if (funct3 == static_cast<simrv::xlen::Instruction>(Funct3::Sw) &&
                           (req.address == machine_.isa_test_tohost() + 4 ||
                            req.address == 0x80001004 || req.address == 0x40008004)) {
                    machine_.publish_tohost_write(
                        (machine_.tohost & 0x00000000FFFFFFFFULL) |
                            (static_cast<uint64_t>(req.data & 0xFFFFFFFFULL) << 32),
                        core::Machine::TohostWriteKind::HighWord);
                }
            }

            if (machine_.memory_geometry().contains(req.address, transfer_bytes) &&
                machine_.ram_data() != nullptr) {
                if constexpr (Spec.supports_coherence) {
                    coherence_hub_.invalidate_line_external(req.address);
                }
                if (req.opcode == TlOpcodeA::PutPartialData) {
                    auto* destination = machine_.ram_view().unchecked_ptr(req.address);
                    const auto lane_base =
                        static_cast<unsigned>(req.address.raw() & (kTlBeatBytes - 1u));
                    for (size_t byte = 0; byte < transfer_bytes; ++byte) {
                        const auto lane = static_cast<unsigned>(lane_base + byte);
                        if ((req.mask & (TlMask{1} << lane)) != 0) {
                            destination[byte] =
                                static_cast<simrv::xlen::Byte>((req.data >> (byte * 8u)) & 0xffu);
                        }
                    }
                } else {
                    simrv::memory::ram_write_fast(req.address, req.data, funct3,
                                                  machine_.ram_view());
                }
                ++write_count_;
                handled = true;
            }
        } else {
            if (req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData) {
                resp.opcode = TlOpcodeD::AccessAck;
            } else {
                resp.opcode = TlOpcodeD::AccessAckData;
            }
            resp.denied = true;
            handled = true;
        }
    }

    if (!handled) {
        handled = router_.route_request(req, resp);
    }
    if (!handled) {
        resp.denied = true;
    }

    const uint8_t beat_count = has_line_data ? kTlBlockBytes / kTlBeatBytes : 1;
    const bool data_port = (req.source & 1u) == static_cast<TlSourceId>(TlPort::Data);
    uint32_t response_latency =
        data_port && data_response_count_ == 0 && startup_data_response_latency_ != 0
            ? startup_data_response_latency_
            : (data_port ? data_response_latency_ : response_latency_);
    response_latency += cache_service_latency - 1;
    const auto current_cycle = cycle_.load(std::memory_order_relaxed);
    const auto dma_config = machine_.dma_engine().config();
    if (machine_.dma_engine().is_transfer_active(current_cycle) &&
        dma_config.memory_contention_penalty != 0) {
        response_latency += dma_config.memory_contention_penalty;
        dma_delayed_requests_.fetch_add(1, std::memory_order_relaxed);
        dma_added_latency_cycles_.fetch_add(dma_config.memory_contention_penalty,
                                            std::memory_order_relaxed);
    }
    if (data_port) ++data_response_count_;

    for (uint8_t beat = 0; beat < beat_count; ++beat) {
        TlChannelD payload = resp;
        if (has_line_data) {
            std::memcpy(&payload.data, response_line.data() + beat * kTlBeatBytes, kTlBeatBytes);
        }
        d_queue_.push_back(TimedDBeat{
            .payload = payload,
            .ready_cycle = current_cycle + response_latency - 1 + beat,
            .sequence = request.sequence,
            .beat_index = beat,
            .beat_count = beat_count,
        });
    }

    if constexpr (Spec.is_axi()) {
        if (axi_tracer_.is_enabled()) {
            const auto axi_resp = resp.failed() ? AxiResp::Decerr : AxiResp::Okay;
            if (req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData) {
                axi_tracer_.trace_b(current_cycle + response_latency - 1,
                                    Axi4B{.id = resp.source, .resp = axi_resp});
            } else {
                axi_tracer_.trace_r(current_cycle + response_latency - 1,
                                    Axi4R{.id = resp.source,
                                          .data = static_cast<simrv::xlen::Word>(resp.data),
                                          .resp = axi_resp,
                                          .last = true});
            }
        }
    }

    simrv::core::BusTraceContext response_context;
    const bool capture_trace_context = machine_.trace().is_architecture_trace_enabled() &&
                                       machine_.configuration().debug.trace_level >= 4;
    if (capture_trace_context) {
        const auto* target = router_.resolve_device(req.address);
        response_context = {
            .sequence = request.sequence,
            .request_cycle = request.submitted_cycle,
            .completion_cycle = current_cycle + response_latency - 1,
            .latency_cycles = current_cycle + response_latency - 1 - request.submitted_cycle,
            .hart = req.hart,
            .transfer_size = req.size,
            .burst_length = beat_count,
            .data = resp.data,
            .byte_enable = req.mask,
            .denied = resp.denied,
            .corrupt = resp.corrupt,
            .master = std::format("hart-{}-{}", req.hart.raw(), data_port ? "data" : "instruction"),
            .target = target != nullptr                                  ? target->name()
                      : machine_.memory_geometry().contains(req.address) ? "ram"
                                                                         : "unmapped",
        };
    }
    record_transaction(TileLinkChannel::D, to_string(resp.opcode), resp.source, resp.sink,
                       req.address, resp.failed() ? "Denied" : to_string(resp.cap),
                       capture_trace_context ? &response_context : nullptr);
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::try_get_timed_response(TlSourceId source_id, TimedResponse& resp) -> bool {
    SmpLockGuard lock(bus_mutex_, is_smp_enabled());
    return consume_d_beat(source_id, true, resp);
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::cancel_source(TlSourceId source_id) {
    SmpLockGuard lock(bus_mutex_, is_smp_enabled_);
    std::erase_if(req_queue_, [source_id](const TimedRequest& request) {
        return request.payload.source == source_id;
    });
    has_pending_requests_.store(!req_queue_.empty(), std::memory_order_release);
    std::erase_if(d_queue_, [source_id](const TimedDBeat& response) {
        return response.payload.source == source_id;
    });
    d_assemblies_.erase(source_id);
    if constexpr (Spec.is_tilelink()) {
        protocol_checker_.cancel(source_id);
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::get_response(TlSourceId source_id, TlChannelD& resp) -> bool {
    SmpLockGuard lock(bus_mutex_, is_smp_enabled());
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

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::consume_d_beat(TlSourceId source_id, bool honor_ready,
                                        TimedResponse& response) -> bool {
    if (d_queue_.empty()) return false;
    const auto current_cycle = cycle_.load(std::memory_order_relaxed);
    auto beat =
        std::ranges::find_if(d_queue_, [source_id, honor_ready, current_cycle](const auto& item) {
            return item.payload.source == source_id &&
                   (!honor_ready || item.ready_cycle <= current_cycle);
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
        simrv::log::warn("{} multibeat stability violation for source {}",
                         bus_protocol_name(Spec.protocol), source_id);
    }
    if (assembly.response.has_line_data && beat->beat_index < assembly.beat_count) {
        std::memcpy(assembly.response.line_data.data() + beat->beat_index * kTlBeatBytes,
                    &beat->payload.data, kTlBeatBytes);
    } else {
        assembly.response.payload.data = beat->payload.data;
    }
    ++assembly.next_beat;
    if (beat == d_queue_.begin()) {
        d_queue_.pop_front();
    } else {
        d_queue_.erase(beat);
    }

    if (assembly.next_beat != assembly.beat_count) return false;
    if constexpr (Spec.is_tilelink()) {
        if (const auto valid = protocol_checker_.accept_d(assembly.response.payload); !valid) {
            simrv::log::warn("{} response violation: {}", bus_protocol_name(Spec.protocol),
                             valid.error());
            assembly.response.payload.denied = true;
        }
    }
    response = assembly.response;
    d_assemblies_.erase(source_id);
    return true;
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::pending_responses() const noexcept -> size_t {
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

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::acquire_block(
    const TlChannelA& req, TlChannelD& resp,
    std::array<simrv::xlen::Byte, CoherenceHub::kLineBytes>& line_data) -> bool {
    if constexpr (!Spec.supports_coherence) {
        (void)req;
        (void)resp;
        (void)line_data;
        return false;
    } else {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        if (const auto valid = protocol_checker_.accept_a(req); !valid) return false;
        record_transaction(TileLinkChannel::A, to_string(req.opcode), req.source, 0, req.address,
                           to_string(req.grow));
        uint32_t cache_service_latency = 1;
        const bool handled =
            coherence_hub_.handle_acquire(req, resp, line_data, cache_service_latency);
        if (!handled) {
            protocol_checker_.cancel(req.source);
            return false;
        }
        record_transaction(TileLinkChannel::D, to_string(resp.opcode), resp.source, resp.sink,
                           req.address, resp.failed() ? "Denied" : to_string(resp.cap));
        return protocol_checker_.accept_d(resp).has_value();
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::acquire_perm(const TlChannelA& req, TlChannelD& resp) -> bool {
    if constexpr (!Spec.supports_coherence) {
        (void)req;
        (void)resp;
        return false;
    } else {
        std::array<simrv::xlen::Byte, CoherenceHub::kLineBytes> unused{};
        return acquire_block(req, resp, unused);
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::release_line(
    const TlChannelC& req, TlChannelD& resp,
    const std::array<simrv::xlen::Byte, CoherenceHub::kLineBytes>* data) -> bool {
    if constexpr (!Spec.supports_coherence) {
        (void)req;
        (void)resp;
        (void)data;
        return false;
    } else {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        (void)protocol_checker_.accept_c(req);
        record_transaction(TileLinkChannel::C, to_string(req.opcode), req.source, 0, req.address,
                           to_string(req.report));
        const bool handled = coherence_hub_.handle_release(req, resp, data);
        if (handled) {
            (void)protocol_checker_.accept_d(resp);
            record_transaction(TileLinkChannel::D, to_string(resp.opcode), resp.source, resp.sink,
                               req.address);
        }
        return handled;
    }
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::mark_modified(simrv::xlen::PhysAddr line_base, simrv::xlen::HartId hart) {
    if constexpr (Spec.supports_coherence) {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        coherence_hub_.mark_modified(line_base, hart);
    } else {
        (void)line_base;
        (void)hart;
    }
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::grant_ack(const TlChannelE& ack) {
    if constexpr (Spec.supports_coherence) {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        record_transaction(TileLinkChannel::E, to_string(ack.opcode), 0, ack.sink,
                           simrv::xlen::PhysAddr{});
        if (const auto valid = protocol_checker_.accept_e(ack); !valid) {
            simrv::log::warn("TileLink-C GrantAck violation: {}", valid.error());
            return;
        }
        coherence_hub_.process_grant_ack(ack);
    } else {
        (void)ack;
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::send_ar(const Axi4Ar& ar) -> bool {
    if constexpr (!Spec.is_axi()) {
        (void)ar;
        return false;
    } else {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        if (!axi_burst_valid(ar.burst, ar.size, ar.len, Spec.max_burst_beats, Spec.supports_burst,
                             ar.addr) ||
            !axi_id_fits(ar.id, Spec.id_width_bits) || pending_ar_.size() >= Spec.max_outstanding) {
            return false;
        }
        const auto current_cycle = cycle_.load(std::memory_order_relaxed);
        axi_tracer_.trace_ar(current_cycle, ar);

        const uint32_t beat_bytes = uint32_t{1} << ar.size;
        const uint32_t total_beats = static_cast<uint32_t>(ar.len) + 1U;
        for (uint32_t i = 0; i < total_beats; ++i) {
            const auto beat_addr = axi_beat_address(ar.addr, beat_bytes, total_beats, ar.burst, i);
            simrv::xlen::Word val{0};
            AxiResp resp = AxiResp::Okay;
            const auto geometry = machine_.memory_geometry();
            if (machine_.ram_data() != nullptr &&
                geometry.contains(simrv::xlen::PhysAddr{beat_addr}, beat_bytes)) {
                std::memcpy(&val, machine_.ram_data() + (beat_addr - geometry.dram_base.raw()),
                            std::min<std::size_t>(beat_bytes, sizeof(val)));
                ++read_count_;
            } else {
                TlChannelA req{};
                req.opcode = TlOpcodeA::Get;
                req.address = simrv::xlen::PhysAddr{beat_addr};
                req.size = ar.size;
                req.source = static_cast<TlSourceId>(ar.id);
                req.mask = TlChannelA::compute_mask(req.size, req.address);
                TlChannelD d_resp{};
                if (!router_.route_request(req, d_resp) || d_resp.failed()) {
                    resp = AxiResp::Decerr;
                } else {
                    val = d_resp.data;
                }
            }
            pending_r_.push_back(Axi4R{
                .id = ar.id,
                .data = val,
                .resp = resp,
                .last = (i == ar.len),
            });
        }
        pending_ar_.push_back(ar);
        return true;
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::recv_r(Axi4R& r) -> bool {
    if constexpr (!Spec.is_axi()) {
        (void)r;
        return false;
    } else {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        if (pending_r_.empty()) return false;
        r = pending_r_.front();
        pending_r_.erase(pending_r_.begin());
        axi_tracer_.trace_r(cycle_.load(std::memory_order_relaxed), r);
        if (r.last && !pending_ar_.empty()) {
            pending_ar_.erase(pending_ar_.begin());
        }
        return true;
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::send_aw(const Axi4Aw& aw) -> bool {
    if constexpr (!Spec.is_axi()) {
        (void)aw;
        return false;
    } else {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        if (!axi_burst_valid(aw.burst, aw.size, aw.len, Spec.max_burst_beats, Spec.supports_burst,
                             aw.addr) ||
            !axi_id_fits(aw.id, Spec.id_width_bits) || pending_aw_.size() >= Spec.max_outstanding) {
            return false;
        }
        axi_tracer_.trace_aw(cycle_.load(std::memory_order_relaxed), aw);
        pending_aw_.push_back(aw);
        return true;
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::send_w(const Axi4W& w) -> bool {
    if constexpr (!Spec.is_axi()) {
        (void)w;
        return false;
    } else {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        if (pending_aw_.empty()) return false;
        axi_tracer_.trace_w(cycle_.load(std::memory_order_relaxed), w);
        pending_w_.push_back(w);
        if (w.last) {
            const auto aw = pending_aw_.front();
            pending_aw_.erase(pending_aw_.begin());
            AxiResp resp = AxiResp::Okay;
            const uint32_t beat_bytes = uint32_t{1} << aw.size;
            const uint32_t expected_beats = static_cast<uint32_t>(aw.len) + 1U;
            const bool complete_burst = pending_w_.size() == expected_beats;
            if (!complete_burst) {
                resp = AxiResp::Slverr;
            }
            const auto geometry = machine_.memory_geometry();
            for (size_t i = 0; complete_burst && i < pending_w_.size(); ++i) {
                const auto beat_addr = axi_beat_address(aw.addr, beat_bytes, expected_beats,
                                                        aw.burst, static_cast<uint32_t>(i));
                const auto& beat = pending_w_[i];
                if (machine_.ram_data() != nullptr &&
                    geometry.contains(simrv::xlen::PhysAddr{beat_addr}, beat_bytes)) {
                    auto* dst = machine_.ram_data() + (beat_addr - geometry.dram_base.raw());
                    for (uint32_t b = 0; b < beat_bytes && b < sizeof(beat.data); ++b) {
                        if ((beat.strb & (1U << b)) != 0U) {
                            dst[b] =
                                static_cast<simrv::xlen::Byte>((beat.data >> (b * 8U)) & 0xFFU);
                        }
                    }
                    ++write_count_;
                } else {
                    TlChannelA req{};
                    req.opcode = TlOpcodeA::PutFullData;
                    req.address = simrv::xlen::PhysAddr{beat_addr};
                    req.size = aw.size;
                    req.source = static_cast<TlSourceId>(aw.id);
                    req.data = beat.data;
                    req.mask = beat.strb;
                    TlChannelD d_resp{};
                    if (!router_.route_request(req, d_resp) || d_resp.failed()) {
                        resp = AxiResp::Decerr;
                    }
                }
            }
            pending_w_.clear();
            pending_b_.push_back(Axi4B{.id = aw.id, .resp = resp});
        }
        return true;
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::recv_b(Axi4B& b) -> bool {
    if constexpr (!Spec.is_axi()) {
        (void)b;
        return false;
    } else {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        if (pending_b_.empty()) return false;
        b = pending_b_.front();
        pending_b_.erase(pending_b_.begin());
        axi_tracer_.trace_b(cycle_.load(std::memory_order_relaxed), b);
        return true;
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::axi_read(simrv::xlen::Address addr, uint8_t size_bytes, uint8_t len)
    -> std::pair<AxiResp, std::vector<simrv::xlen::Word>> {
    if constexpr (!Spec.is_axi()) {
        (void)addr;
        (void)size_bytes;
        (void)len;
        return {AxiResp::Decerr, {}};
    } else {
        Axi4Ar ar{};
        ar.id = 0;
        ar.addr = addr;
        ar.len = len;
        ar.size = size_to_axi_size(size_bytes);
        ar.burst = AxiBurst::Incr;
        if (!send_ar(ar)) {
            return {AxiResp::Decerr, {}};
        }
        std::vector<simrv::xlen::Word> out;
        out.reserve(static_cast<size_t>(len) + 1U);
        AxiResp worst = AxiResp::Okay;
        for (uint32_t i = 0; i <= len; ++i) {
            Axi4R r{};
            if (!recv_r(r)) return {AxiResp::Slverr, out};
            if (r.resp != AxiResp::Okay) worst = r.resp;
            out.push_back(r.data);
        }
        return {worst, out};
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::axi_write(simrv::xlen::Address addr,
                                   std::span<const simrv::xlen::Word> data, uint8_t size_bytes,
                                   uint32_t strb) -> AxiResp {
    if constexpr (!Spec.is_axi()) {
        (void)addr;
        (void)data;
        (void)size_bytes;
        (void)strb;
        return AxiResp::Decerr;
    } else {
        if (data.empty() || data.size() > Spec.max_burst_beats) {
            return AxiResp::Slverr;
        }
        Axi4Aw aw{};
        aw.id = 0;
        aw.addr = addr;
        aw.len = static_cast<uint8_t>(data.size() - 1U);
        aw.size = size_to_axi_size(size_bytes);
        aw.burst = AxiBurst::Incr;
        if (!send_aw(aw)) {
            return AxiResp::Decerr;
        }
        for (size_t i = 0; i < data.size(); ++i) {
            Axi4W w{};
            w.data = data[i];
            w.strb = strb;
            w.last = (i + 1U == data.size());
            if (!send_w(w)) return AxiResp::Slverr;
        }
        Axi4B b{};
        if (!recv_b(b)) return AxiResp::Slverr;
        return b.resp;
    }
}

auto make_system_bus(simrv::core::Machine& machine, const BusProtocolSpec& spec)
    -> std::unique_ptr<Bus> {
    switch (spec.protocol) {
        case BusProtocol::TileLinkC:
            return std::make_unique<TemplatedBus<kDefaultTileLinkCSpec>>(machine);
        case BusProtocol::TileLinkUH:
            return std::make_unique<TemplatedBus<kDefaultTileLinkUHSpec>>(machine);
        case BusProtocol::TileLinkUL:
            return std::make_unique<TemplatedBus<kDefaultTileLinkULSpec>>(machine);
        case BusProtocol::Axi4:
            return std::make_unique<TemplatedBus<kDefaultAxi4Spec>>(machine);
        case BusProtocol::Axi4Lite:
            return std::make_unique<TemplatedBus<kDefaultAxi4LiteSpec>>(machine);
    }
    return std::make_unique<TemplatedBus<kDefaultTileLinkCSpec>>(machine);
}

template class TemplatedBus<kDefaultTileLinkCSpec>;
template class TemplatedBus<kDefaultTileLinkUHSpec>;
template class TemplatedBus<kDefaultTileLinkULSpec>;
template class TemplatedBus<kDefaultAxi4Spec>;
template class TemplatedBus<kDefaultAxi4LiteSpec>;

}  // namespace simrv::memory
