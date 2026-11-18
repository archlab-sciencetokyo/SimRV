/**
 * @file TemplatedBus.cpp
 * @brief Explicit NTTP instantiations and shared implementation for TemplatedBus<Spec>.
 */
#include "simrv/memory/TemplatedBus.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::memory {

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

template <BusProtocolSpec Spec>
TemplatedBus<Spec>::TemplatedBus(simrv::core::Machine& machine, BusProtocolSpec spec)
    : machine_(machine), spec_(spec), coherence_hub_(machine) {}

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
auto TemplatedBus<Spec>::contention_stats() const -> ContentionStats {
    const auto current_cycle = cycle_.load(std::memory_order_relaxed);
    const auto dma_config = machine_.dma_engine().config();
    return ContentionStats{
        .dma_active = machine_.dma_engine().is_transfer_active(current_cycle),
        .dma_penalty_cycles = dma_config.memory_contention_penalty,
        .delayed_requests = dma_delayed_requests_.load(std::memory_order_relaxed),
        .added_latency_cycles = dma_added_latency_cycles_.load(std::memory_order_relaxed),
        .queue_contended_requests = queue_contended_requests_.load(std::memory_order_relaxed),
        .queue_wait_cycles = queue_wait_cycles_.load(std::memory_order_relaxed),
        .ifetch_contended_requests = ifetch_contended_requests_.load(std::memory_order_relaxed),
        .data_contended_requests = data_contended_requests_.load(std::memory_order_relaxed),
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
        const auto ready_at = request.submitted_cycle + request.request_latency;
        if (current_cycle > ready_at) {
            queue_wait_cycles_.fetch_add(current_cycle - ready_at, std::memory_order_relaxed);
        }
        process_request(request);
    }
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

#include "specs/Axi4Spec.tpp"
#include "specs/TileLinkSpec.tpp"

auto make_system_bus(simrv::core::Machine& machine, const BusProtocolSpec& spec)
    -> std::unique_ptr<Bus> {
    if (!is_supported_bus_spec(spec)) {
        throw std::invalid_argument("unsupported bus protocol specification");
    }
    switch (spec.protocol) {
        case BusProtocol::TileLinkC:
            return std::make_unique<TemplatedBus<kDefaultTileLinkCSpec>>(machine);
        case BusProtocol::TileLinkUH:
            return std::make_unique<TemplatedBus<kDefaultTileLinkUHSpec>>(machine);
        case BusProtocol::TileLinkUL:
            return std::make_unique<TemplatedBus<kDefaultTileLinkULSpec>>(machine);
        case BusProtocol::Axi4:
            return std::make_unique<TemplatedBus<kDefaultAxi4Spec>>(machine, spec);
        case BusProtocol::Axi4Lite:
            return std::make_unique<TemplatedBus<kDefaultAxi4LiteSpec>>(machine, spec);
        case BusProtocol::Axi4LiteUnified:
            return std::make_unique<TemplatedBus<kAxi4LiteUnifiedSpec>>(machine, spec);
    }
    return std::make_unique<TemplatedBus<kDefaultTileLinkCSpec>>(machine);
}

template class TemplatedBus<kDefaultTileLinkCSpec>;
template class TemplatedBus<kDefaultTileLinkUHSpec>;
template class TemplatedBus<kDefaultTileLinkULSpec>;
template class TemplatedBus<kDefaultAxi4Spec>;
template class TemplatedBus<kDefaultAxi4LiteSpec>;
template class TemplatedBus<kAxi4LiteUnifiedSpec>;

}  // namespace simrv::memory
