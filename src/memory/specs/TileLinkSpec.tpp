using simrv::isa::Funct3;

/** Protocol-specific TemplatedBus implementation. */

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::try_get_timed_response(TlSourceId source_id, TimedResponse& resp) -> bool {
    SmpLockGuard lock(bus_mutex_, is_smp_enabled());
    return consume_d_beat(source_id, true, resp);
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::cancel_source(TlSourceId source_id) {
    SmpLockGuard lock(bus_mutex_, is_smp_enabled());
    std::erase_if(req_queue_, [source_id](const TimedRequest& request) {
        return request.payload.source == source_id;
    });
    has_pending_requests_.store(!req_queue_.empty(), std::memory_order_release);
    std::erase_if(d_queue_, [source_id](const TimedDBeat& response) {
        return response.payload.source == source_id;
    });
    d_assemblies_.erase(source_id);
    if constexpr (Spec.is_tilelink()) protocol_checker_.cancel(source_id);
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::send_request(const TlChannelA& req) -> bool {
    auto normalized = req;
    if (normalized.mask == 0 && normalized.opcode != TlOpcodeA::PutPartialData) {
        normalized.mask = TlChannelA::compute_mask(normalized.size, normalized.address);
    }
    return enqueue_request(normalized, false);
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::enqueue_request(const TlChannelA& req, bool source_reserved,
                                         const ARequestAssembly* assembly) -> bool {
    SmpLockGuard lock(bus_mutex_, is_smp_enabled());

    if constexpr (!Spec.supports_coherence) {
        if (req.opcode == TlOpcodeA::AcquireBlock || req.opcode == TlOpcodeA::AcquirePerm) {
            simrv::log::warn("{} bus does not support coherence opcodes",
                             bus_protocol_name(Spec.protocol));
            return false;
        }
    }
    if constexpr (!Spec.supports_atomics && !Spec.supports_internal_atomics) {
        if (req.opcode == TlOpcodeA::ArithmeticData || req.opcode == TlOpcodeA::LogicalData) {
            simrv::log::warn("{} bus does not support atomic opcodes",
                             bus_protocol_name(Spec.protocol));
            return false;
        }
    }
    if constexpr (!Spec.supports_burst) {
        const bool block_access =
            Spec.supports_block_access && req.size <= kTlBlockSize &&
            (req.opcode == TlOpcodeA::Get || req.opcode == TlOpcodeA::PutFullData ||
             req.opcode == TlOpcodeA::PutPartialData);
        if (req.size > 3 && !block_access) {
            simrv::log::warn("{} bus does not support multi-beat bursts (size={})",
                             bus_protocol_name(Spec.protocol), req.size);
            return false;
        }
    }

    if constexpr (Spec.is_tilelink()) {
        if (!source_reserved) {
            if (const auto valid = protocol_checker_.accept_a(req); !valid) {
                simrv::log::warn("{} request rejected: {}", bus_protocol_name(Spec.protocol),
                                 valid.error());
                return false;
            }
        }
    } else {
        if (req.opcode == TlOpcodeA::Intent) {
            simrv::log::warn("{} bus does not support Hint/Intent opcodes",
                             bus_protocol_name(Spec.protocol));
            return false;
        }
        trace_axi_request(req, cycle_.load(std::memory_order_relaxed));
    }

    const bool data_port = (req.source & 1u) == static_cast<TlSourceId>(TlPort::Data);
    if (!req_queue_.empty() || !d_queue_.empty()) {
        queue_contended_requests_.fetch_add(1, std::memory_order_relaxed);
        if (data_port) {
            data_contended_requests_.fetch_add(1, std::memory_order_relaxed);
        } else {
            ifetch_contended_requests_.fetch_add(1, std::memory_order_relaxed);
        }
    }
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
    uint32_t req_lat = data_port ? data_request_latency_ : request_latency_;
    uint32_t shared_hit_lat = 0;
    if (req.opcode == TlOpcodeA::Get || req.opcode == TlOpcodeA::PutFullData ||
        req.opcode == TlOpcodeA::PutPartialData) {
        if (coherence_hub_.service_uncached_shared_cache(req.hart, req.address, shared_hit_lat)) {
            req_lat = 1;
        }
    }
    TimedRequest timed_request{.payload = req,
                               .submitted_cycle = logical_cycle,
                               .request_latency = req_lat,
                               .sequence = next_sequence_++,
                               .shared_cache_hit_latency = shared_hit_lat};
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
    if (assembly) {
        timed_request.data = assembly->data;
        timed_request.masks = assembly->masks;
        timed_request.has_multibeat_data = true;
        timed_request.multibeat_data_corrupt = assembly->corrupt;
    }
    req_queue_.insert(position, std::move(timed_request));
    has_pending_requests_.store(true, std::memory_order_release);
    return true;
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::process_request(const TimedRequest& request) {
    auto req = request.payload;
    req.corrupt = req.corrupt || request.multibeat_data_corrupt;

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

    if constexpr (Spec.protocol == BusProtocol::Axi4LiteUnified) {
        handled = process_axi_lite_unified_request(request, resp, response_line, has_line_data,
                                                   cache_service_latency);
    }

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
        if (!valid_size && req.size <= kTlBlockSize && req.opcode == TlOpcodeA::Get &&
            machine_.ram_data() != nullptr &&
            machine_.memory_geometry().contains(req.address, size_t{1} << req.size)) {
            if constexpr (Spec.supports_coherence)
                coherence_hub_.invalidate_line_external(req.address);
            std::memcpy(response_line.data(), machine_.ram_view().unchecked_ptr(req.address),
                        size_t{1} << req.size);
            resp.opcode = TlOpcodeD::AccessAckData;
            has_line_data = true;
            ++read_count_;
            handled = true;
        } else if (!valid_size && request.has_multibeat_data && req.size <= kTlBlockSize &&
                   !req.corrupt &&
                   (req.opcode == TlOpcodeA::PutFullData ||
                    req.opcode == TlOpcodeA::PutPartialData) &&
                   machine_.ram_data() != nullptr &&
                   machine_.memory_geometry().contains(req.address, size_t{1} << req.size)) {
            if constexpr (Spec.supports_coherence)
                coherence_hub_.invalidate_line_external(req.address);
            auto* destination = machine_.ram_view().unchecked_ptr(req.address);
            const size_t bytes = size_t{1} << req.size;
            machine_.memory().reservation_table().invalidate_range(req.address, bytes);
            for (size_t i = 0; i < bytes; ++i) {
                if ((request.masks[i / kTlBeatBytes] & (1U << (i % kTlBeatBytes))) != 0) {
                    destination[i] = request.data[i];
                }
            }
            resp.opcode = TlOpcodeD::AccessAck;
            ++write_count_;
            handled = true;
        } else if (!valid_size) {
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
        } else if ((Spec.supports_atomics || Spec.supports_internal_atomics) &&
                   (req.opcode == TlOpcodeA::LogicalData ||
                    req.opcode == TlOpcodeA::ArithmeticData) &&
                   !req.corrupt) {
            resp.opcode = TlOpcodeD::AccessAckData;
            if (machine_.memory_geometry().contains(req.address, transfer_bytes) &&
                machine_.ram_data() != nullptr) {
                if constexpr (Spec.supports_coherence) {
                    coherence_hub_.invalidate_line_external(req.address);
                }
                uint64_t previous = 0;
                auto* location = machine_.ram_view().unchecked_ptr(req.address);
                std::memcpy(&previous, location, transfer_bytes);
                const unsigned bits = static_cast<unsigned>(transfer_bytes * 8);
                const uint64_t mask = bits == 64 ? ~uint64_t{0} : (uint64_t{1} << bits) - 1;
                const uint64_t operand = req.data & mask;
                const uint64_t sign = uint64_t{1} << (bits - 1);
                uint64_t result = previous;
                bool valid_op = true;
                if (req.opcode == TlOpcodeA::LogicalData) {
                    switch (req.logical) {
                        case TlLogical::Xor:
                            result = previous ^ operand;
                            break;
                        case TlLogical::Or:
                            result = previous | operand;
                            break;
                        case TlLogical::And:
                            result = previous & operand;
                            break;
                        case TlLogical::Swap:
                            result = operand;
                            break;
                        default:
                            valid_op = false;
                            break;
                    }
                } else {
                    switch (req.arithmetic) {
                        case TlArithmetic::Min:
                            result = (previous ^ sign) < (operand ^ sign) ? previous : operand;
                            break;
                        case TlArithmetic::Max:
                            result = (previous ^ sign) > (operand ^ sign) ? previous : operand;
                            break;
                        case TlArithmetic::MinU:
                            result = std::min(previous, operand);
                            break;
                        case TlArithmetic::MaxU:
                            result = std::max(previous, operand);
                            break;
                        case TlArithmetic::Add:
                            result = (previous + operand) & mask;
                            break;
                        default:
                            valid_op = false;
                            break;
                    }
                }
                resp.data = previous;
                resp.denied = !valid_op;
                if (valid_op) {
                    machine_.memory().reservation_table().invalidate_range(req.address,
                                                                           transfer_bytes);
                    std::memcpy(location, &result, transfer_bytes);
                    ++read_count_;
                    ++write_count_;
                }
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
                machine_.memory().reservation_table().invalidate_range(req.address, transfer_bytes);
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

    const uint8_t beat_count = has_line_data ? (uint8_t{1} << (req.size - kTlBeatSize)) : 1;
    const bool data_port = (req.source & 1u) == static_cast<TlSourceId>(TlPort::Data);
    uint32_t response_latency =
        request.shared_cache_hit_latency != 0
            ? request.shared_cache_hit_latency
            : (data_port && data_response_count_ == 0 && startup_data_response_latency_ != 0
                   ? startup_data_response_latency_
                   : (data_port ? data_response_latency_ : response_latency_));
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
        trace_axi_response(req, resp, current_cycle + response_latency - 1);
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
        if ((req.opcode != TlOpcodeC::Release && req.opcode != TlOpcodeC::ReleaseData) ||
            (req.opcode == TlOpcodeC::ReleaseData && data == nullptr)) {
            return false;
        }
        if (const auto valid = protocol_checker_.accept_c(req); !valid) {
            simrv::log::warn("TileLink-C release violation: {}", valid.error());
            return false;
        }
        record_transaction(TileLinkChannel::C, to_string(req.opcode), req.source, 0, req.address,
                           to_string(req.report));
        const bool handled = coherence_hub_.handle_release(req, resp, data);
        if (handled) {
            if (const auto valid = protocol_checker_.accept_d(resp); !valid) {
                simrv::log::warn("TileLink-C release response violation: {}", valid.error());
                protocol_checker_.cancel(req.source);
                return false;
            }
            record_transaction(TileLinkChannel::D, to_string(resp.opcode), resp.source, resp.sink,
                               req.address);
        }
        if (!handled) protocol_checker_.cancel(req.source);
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
auto TemplatedBus<Spec>::tilelink_cycle(const TlCycleInput& input) -> TlCycleOutput {
    TlCycleOutput output{};
    if constexpr (Spec.is_tilelink()) {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        output.a_ready =
            !cycle_acquire_ && (a_request_.has_value() ||
                                protocol_checker_.outstanding_sources() < spec_.max_outstanding);
        output.c_ready = Spec.supports_coherence &&
                         (c_release_.has_value() || protocol_checker_.outstanding_probes() != 0 ||
                          protocol_checker_.outstanding_releases() < spec_.max_outstanding);
        output.e_ready = Spec.supports_coherence;
        if (!cycle_probes_.empty()) {
            output.b_valid = true;
            output.b = cycle_probes_.front();
            if (input.b_ready) {
                if (const auto valid = protocol_checker_.accept_b(output.b); valid) {
                    record_transaction(TileLinkChannel::B, to_string(output.b.opcode),
                                       output.b.source, 0, output.b.address,
                                       to_string(output.b.cap));
                    cycle_probes_.pop_front();
                } else {
                    simrv::log::warn("TileLink-C probe violation: {}", valid.error());
                    output.b_valid = false;
                }
            }
        }
        if (!d_queue_.empty() &&
            d_queue_.front().ready_cycle <= cycle_.load(std::memory_order_relaxed)) {
            const auto& beat = d_queue_.front();
            output.d_valid = true;
            output.d = beat.payload;
            if (input.d_ready) {
                if (beat.beat_index + 1U == beat.beat_count) {
                    if (const auto valid = protocol_checker_.accept_d(beat.payload); !valid) {
                        simrv::log::warn("{} response violation: {}",
                                         bus_protocol_name(Spec.protocol), valid.error());
                    }
                }
                d_queue_.pop_front();
            }
        }
        if constexpr (Spec.supports_coherence) {
            if (input.c_valid && output.c_ready) {
                const auto& c = input.c;
                const bool probe_ack =
                    c.opcode == TlOpcodeC::ProbeAck || c.opcode == TlOpcodeC::ProbeAckData;
                if (!c_release_) {
                    const auto* probe = probe_ack ? protocol_checker_.probe_for(c.source) : nullptr;
                    const bool valid_probe =
                        probe != nullptr && cycle_acquire_ && probe->address == c.address &&
                        probe->size == c.size && probe->target == c.hart &&
                        ((probe->cap == TlCap::ToN &&
                          (c.report == TlReport::TtoN || c.report == TlReport::BtoN ||
                           c.report == TlReport::NtoN)) ||
                         (probe->cap == TlCap::ToB && c.report != TlReport::TtoT)) &&
                        (c.opcode != TlOpcodeC::ProbeAckData ||
                         ((c.report == TlReport::TtoB || c.report == TlReport::TtoN) &&
                          c.mask == TlChannelA::compute_mask(c.size, c.address)));
                    const bool is_release =
                        c.opcode == TlOpcodeC::Release || c.opcode == TlOpcodeC::ReleaseData;
                    if (c.size != kTlBlockSize || c.hart.raw() >= 64 ||
                        ((c.opcode == TlOpcodeC::ProbeAck || c.opcode == TlOpcodeC::Release) &&
                         c.corrupt) ||
                        (c.opcode == TlOpcodeC::ReleaseData &&
                         c.mask != TlChannelA::compute_mask(c.size, c.address)) ||
                        (probe_ack ? !valid_probe
                                   : (!is_release || !protocol_checker_.accept_c(c)))) {
                        output.c_ready = false;
                    } else {
                        c_release_.emplace();
                        c_release_->first = c;
                    }
                } else {
                    const auto& first = c_release_->first;
                    if (c.opcode != first.opcode || c.report != first.report ||
                        c.size != first.size || c.source != first.source || c.hart != first.hart ||
                        c.address != first.address) {
                        output.c_ready = false;
                    }
                }
                if (output.c_ready) {
                    auto& release = *c_release_;
                    const bool has_data =
                        c.opcode == TlOpcodeC::ReleaseData || c.opcode == TlOpcodeC::ProbeAckData;
                    if (has_data) {
                        std::memcpy(release.data.data() + release.next_beat * kTlBeatBytes, &c.data,
                                    kTlBeatBytes);
                        release.corrupt = release.corrupt || c.corrupt;
                    }
                    ++release.next_beat;
                    if (!has_data || release.next_beat == kTlBlockBytes / kTlBeatBytes) {
                        if (probe_ack) {
                            const auto* probe = protocol_checker_.probe_for(c.source);
                            auto response = release.first;
                            response.corrupt = release.corrupt;
                            if (probe == nullptr || !cycle_acquire_ ||
                                !coherence_hub_.complete_cycle_probe(
                                    *probe, response,
                                    has_data && !release.corrupt ? &release.data : nullptr)) {
                                --release.next_beat;
                                output.c_ready = false;
                            } else {
                                (void)protocol_checker_.accept_c(release.first);
                                record_transaction(TileLinkChannel::C, to_string(c.opcode),
                                                   c.source, 0, c.address, to_string(c.report));
                                cycle_acquire_->failed = cycle_acquire_->failed || release.corrupt;
                                --cycle_acquire_->remaining_probes;
                                c_release_.reset();
                                if (cycle_acquire_->remaining_probes == 0) {
                                    const auto pending = cycle_acquire_->request;
                                    if (cycle_acquire_->failed) {
                                        TlChannelD denied{};
                                        denied.opcode = pending.opcode == TlOpcodeA::AcquireBlock
                                                            ? TlOpcodeD::GrantData
                                                            : TlOpcodeD::Grant;
                                        denied.cap = TlCap::ToN;
                                        denied.size = pending.size;
                                        denied.source = pending.source;
                                        denied.denied = true;
                                        denied.corrupt = pending.opcode == TlOpcodeA::AcquireBlock;
                                        const uint8_t count =
                                            denied.corrupt
                                                ? static_cast<uint8_t>(
                                                      uint8_t{1} << (pending.size - kTlBeatSize))
                                                : 1;
                                        for (uint8_t i = 0; i < count; ++i) {
                                            d_queue_.push_back(TimedDBeat{
                                                .payload = denied,
                                                .ready_cycle =
                                                    cycle_.load(std::memory_order_relaxed) +
                                                    response_latency_ + i,
                                                .sequence = next_sequence_++,
                                                .beat_index = i,
                                                .beat_count = count});
                                        }
                                    } else if (!enqueue_request(pending, true)) {
                                        protocol_checker_.cancel(pending.source);
                                    }
                                    cycle_acquire_.reset();
                                }
                            }
                        } else {
                            TlChannelD response{};
                            const bool handled = coherence_hub_.handle_release(
                                release.first, response,
                                has_data && !release.corrupt ? &release.data : nullptr);
                            if (handled) {
                                record_transaction(TileLinkChannel::C, to_string(c.opcode),
                                                   c.source, 0, c.address, to_string(c.report));
                                d_queue_.push_back(TimedDBeat{
                                    .payload = response,
                                    .ready_cycle =
                                        cycle_.load(std::memory_order_relaxed) + response_latency_,
                                    .sequence = next_sequence_++,
                                    .beat_index = 0,
                                    .beat_count = 1});
                            } else {
                                protocol_checker_.cancel(c.source);
                            }
                            c_release_.reset();
                        }
                    }
                }
            }
        }
        if (input.e_valid && output.e_ready) grant_ack(input.e);
        if (input.a_valid && output.a_ready) {
            const auto& a = input.a;
            const auto natural_mask = TlChannelA::compute_mask(a.size, a.address);
            const bool partial_put = a.opcode == TlOpcodeA::PutPartialData;
            if (partial_put ? ((a.mask & ~natural_mask) != 0) : a.mask != natural_mask) {
                output.a_ready = false;
            }
            if constexpr (Spec.supports_coherence) {
                if (!a_request_ &&
                    (a.opcode == TlOpcodeA::AcquireBlock || a.opcode == TlOpcodeA::AcquirePerm)) {
                    if (a.hart.raw() >= 64 || a.hart >= machine_.num_harts()) {
                        output.a_ready = false;
                    } else {
                        const auto dir = coherence_hub_.get_directory_state(a.address);
                        const bool branch =
                            a.opcode == TlOpcodeA::AcquireBlock && a.grow == TlGrow::NtoB;
                        uint64_t targets = 0;
                        TlCap cap = branch ? TlCap::ToB : TlCap::ToN;
                        if (dir.owner_hart && (*dir.owner_hart != a.hart || branch)) {
                            targets = uint64_t{1} << dir.owner_hart->raw();
                        } else if (!branch) {
                            targets = dir.sharers_mask & ~(uint64_t{1} << a.hart.raw());
                        }
                        if (targets != 0) {
                            if (const auto valid = protocol_checker_.accept_a(a); !valid) {
                                output.a_ready = false;
                            } else {
                                cycle_acquire_ = PendingCycleAcquire{
                                    .request = a,
                                    .remaining_probes = static_cast<size_t>(std::popcount(targets)),
                                    .probe_targets = targets};
                                for (unsigned hart = 0; hart < 64; ++hart) {
                                    if ((targets & (uint64_t{1} << hart)) != 0) {
                                        cycle_probes_.push_back(TlChannelB{
                                            .opcode = TlOpcodeB::ProbeBlock,
                                            .cap = cap,
                                            .size = a.size,
                                            .source = static_cast<TlSourceId>(hart),
                                            .address = a.address,
                                            .mask = TlChannelA::compute_mask(a.size, a.address),
                                            .target = simrv::xlen::HartId{hart}});
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (cycle_acquire_ || !output.a_ready) {
                advance_cycle();
                return output;
            }
            const bool multibeat_put =
                a.size > kTlBeatSize &&
                (a.opcode == TlOpcodeA::PutFullData || a.opcode == TlOpcodeA::PutPartialData);
            if (a_request_ || multibeat_put) {
                if (!a_request_) {
                    if (!Spec.supports_burst || a.size > kTlBlockSize ||
                        (a.opcode == TlOpcodeA::PutFullData && a.mask != 0xff) ||
                        !protocol_checker_.accept_a(a)) {
                        output.a_ready = false;
                    } else {
                        a_request_.emplace();
                        a_request_->first = a;
                    }
                } else {
                    auto expected = a_request_->first;
                    auto actual = a;
                    expected.data = actual.data = 0;
                    expected.mask = actual.mask = 0;
                    expected.corrupt = actual.corrupt = false;
                    if (expected != actual ||
                        (a.opcode == TlOpcodeA::PutFullData && a.mask != 0xff)) {
                        output.a_ready = false;
                    }
                }
                if (output.a_ready) {
                    auto& assembly = *a_request_;
                    std::memcpy(assembly.data.data() + assembly.next_beat * kTlBeatBytes, &a.data,
                                kTlBeatBytes);
                    assembly.masks[assembly.next_beat] = a.mask;
                    assembly.corrupt = assembly.corrupt || a.corrupt;
                    ++assembly.next_beat;
                    if (assembly.next_beat == (uint8_t{1} << (a.size - kTlBeatSize))) {
                        if (!enqueue_request(assembly.first, true, &assembly)) {
                            protocol_checker_.cancel(a.source);
                        }
                        a_request_.reset();
                    }
                }
            } else {
                output.a_ready = send_request(a);
            }
        }
        advance_cycle();
    }
    return output;
}
