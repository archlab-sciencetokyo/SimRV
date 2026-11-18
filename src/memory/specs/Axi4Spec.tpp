/** Protocol-specific TemplatedBus implementation. */

namespace {

[[nodiscard]] constexpr auto axi_id_fits(AxiId id, uint8_t id_width_bits) noexcept -> bool {
    if (id_width_bits >= 32) return true;
    return id < (AxiId{1} << id_width_bits);
}

[[nodiscard]] constexpr auto axi_exclusive_bytes(uint8_t size, uint8_t len,
                                                 simrv::xlen::Address address) noexcept -> size_t {
    const uint32_t beats = static_cast<uint32_t>(len) + 1U;
    if (size > 7 || beats > 16 || !std::has_single_bit(beats)) return 0;
    const uint32_t bytes = (uint32_t{1} << size) * beats;
    if (bytes > 128 || (address & (bytes - 1U)) != 0) return 0;
    return bytes;
}

[[nodiscard]] constexpr auto axi_burst_valid(AxiBurst burst, uint8_t size, uint8_t len,
                                             uint16_t max_burst_beats, bool supports_burst,
                                             uint32_t bus_bytes, simrv::xlen::Address addr,
                                             bool supports_wide_data) noexcept -> bool {
    if (burst == AxiBurst::Reserved) return false;
    const uint32_t beats = static_cast<uint32_t>(len) + 1U;
    if (!supports_burst && beats > 1U) return false;
    if (beats > max_burst_beats) return false;
    if (size >= 32) return false;
    const uint32_t beat_bytes = uint32_t{1} << size;
    if (beat_bytes > 16 || (beat_bytes > sizeof(simrv::xlen::Word) && !supports_wide_data) ||
        beat_bytes > bus_bytes) {
        return false;
    }
    if (burst == AxiBurst::Fixed && beats > 16) return false;
    if (burst == AxiBurst::Wrap) {
        if (beats != 2 && beats != 4 && beats != 8 && beats != 16) return false;
        if ((addr & (beat_bytes - 1U)) != 0) return false;
    }
    if (burst == AxiBurst::Incr) {
        const uint64_t offset = static_cast<uint64_t>(addr) & 0xfffU;
        const uint64_t aligned_offset = offset & ~static_cast<uint64_t>(beat_bytes - 1U);
        if (aligned_offset + static_cast<uint64_t>(beat_bytes) * beats > 4096U) return false;
    }
    return true;
}

[[nodiscard]] constexpr auto axi_write_byte(const Axi4W& data, uint32_t lane) noexcept
    -> simrv::xlen::Byte {
    if (data.data_bytes_valid) return data.data_bytes[lane];
    if (lane >= sizeof(simrv::xlen::Word)) return simrv::xlen::Byte{0};
    return static_cast<simrv::xlen::Byte>((data.data >> (lane * 8U)) & 0xffU);
}

[[nodiscard]] constexpr auto axi_read_byte(const Axi4R& data, uint32_t lane) noexcept
    -> simrv::xlen::Byte {
    if (data.data_bytes_valid) return data.data_bytes[lane];
    if (lane >= sizeof(simrv::xlen::Word)) return simrv::xlen::Byte{0};
    return static_cast<simrv::xlen::Byte>((data.data >> (lane * 8U)) & 0xffU);
}

[[nodiscard]] constexpr auto axi_beat_address(simrv::xlen::Address base_addr, uint32_t beat_bytes,
                                              uint32_t total_beats, AxiBurst burst,
                                              uint32_t beat_idx) noexcept -> simrv::xlen::Address {
    switch (burst) {
        case AxiBurst::Fixed:
            return base_addr;
        case AxiBurst::Incr:
            if (beat_idx == 0) return base_addr;
            return (base_addr & ~static_cast<simrv::xlen::Address>(beat_bytes - 1U)) +
                   static_cast<simrv::xlen::Address>(beat_idx) * beat_bytes;
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
        case 16:
            return 4;
        default:
            return 3;
    }
}

}  // namespace

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::trace_axi_request(const TlChannelA& req, uint64_t cycle) {
    if (!axi_tracer_.is_enabled()) return;
    const uint8_t burst_len =
        req.size > kTlBeatSize ? static_cast<uint8_t>((1u << (req.size - kTlBeatSize)) - 1u) : 0u;
    if (req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData) {
        axi_tracer_.trace_aw(cycle, Axi4Aw{.id = req.source,
                                           .addr = req.address.raw(),
                                           .len = burst_len,
                                           .size = std::min<uint8_t>(req.size, 3),
                                           .burst = AxiBurst::Incr});
        axi_tracer_.trace_w(cycle, Axi4W{.data = static_cast<simrv::xlen::Word>(req.data),
                                         .strb = static_cast<uint32_t>(req.mask),
                                         .last = true});
    } else {
        axi_tracer_.trace_ar(cycle, Axi4Ar{.id = req.source,
                                           .addr = req.address.raw(),
                                           .len = burst_len,
                                           .size = std::min<uint8_t>(req.size, 3),
                                           .burst = AxiBurst::Incr});
    }
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::trace_axi_response(const TlChannelA& req, const TlChannelD& resp,
                                            uint64_t cycle) {
    if (!axi_tracer_.is_enabled()) return;
    const auto axi_resp = resp.failed() ? AxiResp::Decerr : AxiResp::Okay;
    if (req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData) {
        axi_tracer_.trace_b(cycle, Axi4B{.id = resp.source, .resp = axi_resp});
    } else {
        axi_tracer_.trace_r(cycle, Axi4R{.id = resp.source,
                                         .data = static_cast<simrv::xlen::Word>(resp.data),
                                         .resp = axi_resp,
                                         .last = true});
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::process_axi_lite_unified_request(
    const TimedRequest& request, TlChannelD& response,
    std::array<simrv::xlen::Byte, CoherenceHub::kLineBytes>& response_line, bool& has_line_data,
    uint32_t& cache_service_latency) -> bool {
    const auto& req = request.payload;
    const bool is_read = req.opcode == TlOpcodeA::Get;
    const bool is_write =
        req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData;
    if (!is_read && !is_write) return false;

    response.opcode = is_read ? TlOpcodeD::AccessAckData : TlOpcodeD::AccessAck;
    if (req.corrupt || request.multibeat_data_corrupt) {
        response.denied = true;
        response.corrupt = req.corrupt || request.multibeat_data_corrupt;
        return true;
    }

    const size_t bytes = size_t{1} << req.size;
    size_t transactions = 0;
    bool okay = true;

    if (is_read) {
        const size_t target_width = router_.resolve_device(req.address) != nullptr
                                        ? std::min<uint32_t>(4, spec_.beat_bytes)
                                        : spec_.beat_bytes;
        const bool line_response = bytes > kTlBeatBytes;
        has_line_data = line_response;
        for (size_t offset = 0; offset < bytes;) {
            const auto address = req.address.raw() + offset;
            const size_t width = router_.resolve_device(simrv::xlen::PhysAddr{address}) != nullptr
                                     ? std::min<uint32_t>(4, spec_.beat_bytes)
                                     : target_width;
            const size_t chunk = std::min(width, bytes - offset);
            if (!std::has_single_bit(chunk) || (address & (chunk - 1U)) != 0) {
                okay = false;
                break;
            }
            Axi4Ar ar{.id = 0,
                      .addr = static_cast<simrv::xlen::Address>(address),
                      .len = 0,
                      .size = size_to_axi_size(static_cast<uint8_t>(chunk)),
                      .burst = AxiBurst::Incr};
            if (!send_ar(ar)) {
                okay = false;
                break;
            }
            Axi4R r{};
            if (!recv_r(r) || r.resp != AxiResp::Okay) {
                okay = false;
                break;
            }
            const size_t lane = static_cast<size_t>(address % spec_.beat_bytes);
            for (size_t i = 0; i < chunk; ++i) {
                const auto byte = axi_read_byte(r, static_cast<uint32_t>(lane + i));
                if (line_response) {
                    response_line[offset + i] = byte;
                } else {
                    response.data |= static_cast<uint64_t>(byte) << ((offset + i) * 8U);
                }
            }
            ++transactions;
            offset += chunk;
        }
    } else {
        const auto end_address = req.address.raw() + bytes;
        auto word_address =
            req.address.raw() & ~static_cast<simrv::xlen::Address>(spec_.beat_bytes - 1U);
        while (word_address < end_address) {
            const size_t width =
                router_.resolve_device(simrv::xlen::PhysAddr{word_address}) != nullptr
                    ? std::min<uint32_t>(4, spec_.beat_bytes)
                    : spec_.beat_bytes;
            word_address = req.address.raw() & ~static_cast<simrv::xlen::Address>(width - 1U);
            if (word_address >= end_address) break;
            std::array<simrv::xlen::Byte, 16> lane_data{};
            uint32_t strobe = 0;
            for (size_t lane = 0; lane < width; ++lane) {
                const auto address = word_address + lane;
                if (address < req.address.raw() || address >= end_address) continue;
                const size_t offset = static_cast<size_t>(address - req.address.raw());
                const size_t tl_lane = static_cast<size_t>(address & (kTlBeatBytes - 1U));
                bool enabled = false;
                simrv::xlen::Byte byte{};
                if (request.has_multibeat_data) {
                    enabled = (request.masks[offset / kTlBeatBytes] &
                               (TlMask{1} << (offset % kTlBeatBytes))) != 0;
                    byte = request.data[offset];
                } else if (req.opcode == TlOpcodeA::PutPartialData) {
                    enabled = (req.mask & (TlMask{1} << tl_lane)) != 0;
                    byte = static_cast<simrv::xlen::Byte>((req.data >> (tl_lane * 8U)) & 0xffU);
                } else {
                    enabled = (req.mask & (TlMask{1} << tl_lane)) != 0;
                    byte = static_cast<simrv::xlen::Byte>((req.data >> (offset * 8U)) & 0xffU);
                }
                if (!enabled) continue;
                const size_t bus_lane = static_cast<size_t>(address % spec_.beat_bytes);
                lane_data[bus_lane] = byte;
                strobe |= uint32_t{1} << bus_lane;
            }
            const auto next_word = word_address + width;
            if (strobe != 0) {
                Axi4Aw aw{.id = 0,
                          .addr = word_address,
                          .len = 0,
                          .size = size_to_axi_size(static_cast<uint8_t>(width)),
                          .burst = AxiBurst::Incr};
                Axi4W w{};
                w.strb = strobe;
                w.last = true;
                w.data_bytes = lane_data;
                w.data_bytes_valid = spec_.beat_bytes > sizeof(simrv::xlen::Word);
                for (size_t lane = 0;
                     lane < std::min<size_t>(spec_.beat_bytes, sizeof(simrv::xlen::Word)); ++lane) {
                    w.data |= static_cast<simrv::xlen::Word>(lane_data[lane]) << (lane * 8U);
                }
                axi_cycle_active_ = true;
                const bool address_accepted = send_aw(aw);
                const bool data_accepted = address_accepted && send_w(w);
                axi_cycle_active_ = false;
                if (!data_accepted) {
                    okay = false;
                } else {
                    process_axi_writes(1);
                    Axi4B b{};
                    okay = recv_b(b) && b.resp == AxiResp::Okay && okay;
                    ++transactions;
                }
            }
            word_address = next_word;
        }
    }

    response.denied = !okay;
    if (transactions > 1) {
        cache_service_latency += static_cast<uint32_t>(transactions - 1);
    }
    return true;
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::axi_cycle(const Axi4CycleInput& input) -> Axi4CycleOutput {
    Axi4CycleOutput output{};
    if constexpr (Spec.is_axi()) {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        output.r_valid = !pending_r_.empty();
        if (output.r_valid) output.r = pending_r_.front();
        output.b_valid = !pending_b_.empty();
        if (output.b_valid) output.b = pending_b_.front();
        output.ar_ready = pending_ar_.size() < spec_.max_outstanding;
        const bool write_capacity =
            pending_aw_.size() + pending_b_.size() < spec_.max_outstanding &&
            pending_w_.size() < static_cast<size_t>(spec_.max_outstanding) * spec_.max_burst_beats;
        if constexpr (Spec.unified_write_address_data) {
            const bool paired_write = input.aw_valid && input.w_valid;
            output.aw_ready = write_capacity && paired_write;
            output.w_ready = output.aw_ready;
        } else {
            output.aw_ready = pending_aw_.size() + pending_b_.size() < spec_.max_outstanding;
            output.w_ready = pending_w_.size() <
                             static_cast<size_t>(spec_.max_outstanding) * spec_.max_burst_beats;
        }
        if (input.r_ready && output.r_valid) {
            Axi4R consumed{};
            (void)recv_r(consumed);
        }
        if (input.b_ready && output.b_valid) {
            Axi4B consumed{};
            (void)recv_b(consumed);
        }
        axi_cycle_active_ = true;
        if (input.ar_valid && output.ar_ready) output.ar_ready = send_ar(input.ar);
        if constexpr (Spec.unified_write_address_data) {
            if (input.aw_valid && input.w_valid && output.aw_ready) {
                const bool accepted = send_aw(input.aw) && send_w(input.w);
                output.aw_ready = accepted;
                output.w_ready = accepted;
            }
        } else {
            if (input.aw_valid && output.aw_ready) output.aw_ready = send_aw(input.aw);
            if (input.w_valid && output.w_ready) output.w_ready = send_w(input.w);
        }
        axi_cycle_active_ = false;
        process_axi_writes(1);
        produce_axi_read_beat();
        advance_cycle();
    }
    return output;
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::send_ar(const Axi4Ar& ar) -> bool {
    if constexpr (!Spec.is_axi()) {
        (void)ar;
        return false;
    } else {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        const auto exclusive_bytes = axi_exclusive_bytes(ar.size, ar.len, ar.addr);
        if (ar.lock > 1 || (ar.lock != 0 && Spec.protocol != BusProtocol::Axi4) ||
            (ar.lock == 1 && exclusive_bytes == 0) ||
            !axi_burst_valid(ar.burst, ar.size, ar.len, spec_.max_burst_beats, spec_.supports_burst,
                             spec_.beat_bytes, ar.addr, Spec.unified_write_address_data) ||
            !axi_id_fits(ar.id, spec_.id_width_bits) ||
            pending_ar_.size() >= spec_.max_outstanding) {
            return false;
        }
        const auto current_cycle = cycle_.load(std::memory_order_relaxed);
        axi_tracer_.trace_ar(current_cycle, ar);
        pending_ar_.push_back(ar);
        return true;
    }
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::produce_axi_read_beat() {
    if constexpr (Spec.is_axi()) {
        if (pending_ar_.empty() || !pending_r_.empty()) return;
        const auto& ar = pending_ar_.front();
        const uint32_t beat_bytes = uint32_t{1} << ar.size;
        const uint32_t bus_bytes =
            Spec.unified_write_address_data
                ? spec_.beat_bytes
                : std::min<uint32_t>(spec_.beat_bytes, sizeof(simrv::xlen::Word));
        const uint32_t total_beats = static_cast<uint32_t>(ar.len) + 1U;
        const uint32_t i = axi_read_beat_;
        if (i >= total_beats) return;
        const auto beat_addr = axi_beat_address(ar.addr, beat_bytes, total_beats, ar.burst, i);
        const uint32_t active_bytes = beat_bytes - (beat_addr & (beat_bytes - 1U));
        const uint32_t lane = beat_addr % bus_bytes;
        Axi4R response{.id = ar.id, .resp = AxiResp::Okay, .last = (i == ar.len)};
        const auto exclusive_bytes = axi_exclusive_bytes(ar.size, ar.len, ar.addr);
        const bool exclusive_supported =
            ar.lock == 1 && exclusive_bytes != 0 && !machine_.is_smp_enabled() &&
            machine_.num_harts() == 1 && machine_.ram_data() != nullptr &&
            machine_.memory_geometry().contains(simrv::xlen::PhysAddr{ar.addr}, exclusive_bytes);
        const auto geometry = machine_.memory_geometry();
        auto place_byte = [&](uint32_t bus_lane, simrv::xlen::Byte byte) {
            if (bus_lane < sizeof(simrv::xlen::Word)) {
                response.data |= static_cast<simrv::xlen::Word>(byte) << (bus_lane * 8U);
            }
            if (bus_lane < response.data_bytes.size()) response.data_bytes[bus_lane] = byte;
        };
        if (machine_.ram_data() != nullptr &&
            geometry.contains(simrv::xlen::PhysAddr{beat_addr}, active_bytes)) {
            const auto* src = machine_.ram_data() + (beat_addr - geometry.dram_base.raw());
            for (uint32_t b = 0; b < active_bytes; ++b) place_byte(lane + b, src[b]);
            if (exclusive_supported) {
                if (i == 0) {
                    machine_.memory().reservation_table().set_external_reservation(
                        ar.id, simrv::xlen::PhysAddr{ar.addr}, exclusive_bytes);
                    axi_exclusive_reads_[ar.id] = ar;
                }
                response.resp = AxiResp::Exokay;
            }
            ++read_count_;
        } else {
            const bool use_one_request = active_bytes == beat_bytes && active_bytes <= kTlBeatBytes;
            if (use_one_request) {
                TlChannelA req{};
                req.opcode = TlOpcodeA::Get;
                req.address = simrv::xlen::PhysAddr{beat_addr};
                req.size = ar.size;
                req.source = static_cast<TlSourceId>(ar.id);
                req.mask = TlChannelA::compute_mask(req.size, req.address);
                TlChannelD d_resp{};
                if (!router_.route_request(req, d_resp) || d_resp.failed()) {
                    response.resp = AxiResp::Decerr;
                } else {
                    for (uint32_t b = 0; b < active_bytes; ++b) {
                        place_byte(lane + b, static_cast<simrv::xlen::Byte>(
                                                 (d_resp.data >> (b * 8U)) & 0xffU));
                    }
                }
            } else {
                for (uint32_t b = 0; b < active_bytes; ++b) {
                    TlChannelA req{};
                    req.opcode = TlOpcodeA::Get;
                    req.address = simrv::xlen::PhysAddr{beat_addr + b};
                    req.size = 0;
                    req.source = static_cast<TlSourceId>(ar.id);
                    req.mask = TlChannelA::compute_mask(0, req.address);
                    TlChannelD d_resp{};
                    if (!router_.route_request(req, d_resp) || d_resp.failed()) {
                        response.resp = AxiResp::Decerr;
                        break;
                    }
                    place_byte(lane + b, static_cast<simrv::xlen::Byte>(d_resp.data & 0xffU));
                }
            }
        }
        response.data_bytes_valid = bus_bytes > sizeof(simrv::xlen::Word);
        pending_r_.push_back(response);
        ++axi_read_beat_;
    }
}

template <BusProtocolSpec Spec>
auto TemplatedBus<Spec>::recv_r(Axi4R& r) -> bool {
    if constexpr (!Spec.is_axi()) {
        (void)r;
        return false;
    } else {
        SmpLockGuard lock(bus_mutex_, is_smp_enabled());
        produce_axi_read_beat();
        if (pending_r_.empty()) return false;
        r = pending_r_.front();
        pending_r_.erase(pending_r_.begin());
        axi_tracer_.trace_r(cycle_.load(std::memory_order_relaxed), r);
        if (r.last && !pending_ar_.empty()) {
            pending_ar_.erase(pending_ar_.begin());
            axi_read_beat_ = 0;
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
        if constexpr (Spec.unified_write_address_data) {
            if (!axi_cycle_active_) return false;
        }
        const auto exclusive_bytes = axi_exclusive_bytes(aw.size, aw.len, aw.addr);
        if (aw.lock > 1 || (aw.lock != 0 && Spec.protocol != BusProtocol::Axi4) ||
            (aw.lock == 1 && exclusive_bytes == 0) ||
            !axi_burst_valid(aw.burst, aw.size, aw.len, spec_.max_burst_beats, spec_.supports_burst,
                             spec_.beat_bytes, aw.addr, Spec.unified_write_address_data) ||
            !axi_id_fits(aw.id, spec_.id_width_bits) ||
            pending_aw_.size() + pending_b_.size() >= spec_.max_outstanding) {
            return false;
        }
        axi_tracer_.trace_aw(cycle_.load(std::memory_order_relaxed), aw);
        pending_aw_.push_back(aw);
        if (!axi_cycle_active_) process_axi_writes(std::numeric_limits<size_t>::max());
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
        if constexpr (Spec.unified_write_address_data) {
            if (!axi_cycle_active_) return false;
        }
        const auto capacity = static_cast<size_t>(spec_.max_outstanding) * spec_.max_burst_beats;
        if (pending_w_.size() >= capacity) return false;
        axi_tracer_.trace_w(cycle_.load(std::memory_order_relaxed), w);
        pending_w_.push_back(w);
        if (!axi_cycle_active_) process_axi_writes(std::numeric_limits<size_t>::max());
        return true;
    }
}

template <BusProtocolSpec Spec>
void TemplatedBus<Spec>::process_axi_writes(size_t max_beats) {
    if constexpr (Spec.is_axi()) {
        while (!pending_aw_.empty() && !pending_w_.empty() && max_beats-- > 0) {
            const auto aw = pending_aw_.front();
            const auto exclusive_bytes = axi_exclusive_bytes(aw.size, aw.len, aw.addr);
            const bool exclusive_supported =
                aw.lock == 1 && exclusive_bytes != 0 && !machine_.is_smp_enabled() &&
                machine_.num_harts() == 1 && machine_.ram_data() != nullptr &&
                machine_.memory_geometry().contains(simrv::xlen::PhysAddr{aw.addr},
                                                    exclusive_bytes);
            if (exclusive_supported) {
                const size_t expected = static_cast<size_t>(aw.len) + 1U;
                const auto available = std::min(expected, pending_w_.size());
                const auto early_last =
                    std::find_if(pending_w_.begin(), pending_w_.begin() + available,
                                 [](const Axi4W& item) { return item.last; });
                const bool early =
                    early_last != pending_w_.begin() + available &&
                    static_cast<size_t>(early_last - pending_w_.begin()) + 1 < expected;
                if (!early && pending_w_.size() < expected) break;
                const size_t consumed =
                    early ? static_cast<size_t>(early_last - pending_w_.begin()) + 1 : expected;
                bool well_formed = !early && pending_w_[expected - 1].last;
                const uint32_t beat_bytes = uint32_t{1} << aw.size;
                const uint32_t bus_bytes =
                    Spec.unified_write_address_data
                        ? spec_.beat_bytes
                        : std::min<uint32_t>(spec_.beat_bytes, sizeof(simrv::xlen::Word));
                for (size_t i = 0; well_formed && i < expected; ++i) {
                    const auto address = axi_beat_address(aw.addr, beat_bytes, expected, aw.burst,
                                                          static_cast<uint32_t>(i));
                    const uint32_t lane = address % bus_bytes;
                    const uint32_t mask = ((uint32_t{1} << beat_bytes) - 1U) << lane;
                    well_formed = (pending_w_[i].strb & ~mask) == 0 &&
                                  pending_w_[i].last == (i + 1 == expected);
                }
                const auto found = axi_exclusive_reads_.find(aw.id);
                const bool attributes_match =
                    found != axi_exclusive_reads_.end() && found->second.addr == aw.addr &&
                    found->second.len == aw.len && found->second.size == aw.size &&
                    found->second.burst == aw.burst && found->second.cache == aw.cache &&
                    found->second.prot == aw.prot;
                const bool reserved =
                    machine_.memory().reservation_table().check_and_clear_external_reservation(
                        aw.id, simrv::xlen::PhysAddr{aw.addr},
                        axi_exclusive_bytes(aw.size, aw.len, aw.addr));
                if (found != axi_exclusive_reads_.end()) axi_exclusive_reads_.erase(found);
                AxiResp response = well_formed ? AxiResp::Okay : AxiResp::Slverr;
                if (well_formed && attributes_match && reserved) {
                    const auto geometry = machine_.memory_geometry();
                    for (size_t i = 0; i < expected; ++i) {
                        const auto address = axi_beat_address(aw.addr, beat_bytes, expected,
                                                              aw.burst, static_cast<uint32_t>(i));
                        const uint32_t lane = address % bus_bytes;
                        const auto& data = pending_w_[i];
                        if (data.strb != 0) {
                            machine_.memory().reservation_table().invalidate_range(
                                simrv::xlen::PhysAddr{address}, beat_bytes);
                        }
                        auto* dst = machine_.ram_data() + (address - geometry.dram_base.raw());
                        for (uint32_t b = 0; b < beat_bytes; ++b) {
                            if ((data.strb & (uint32_t{1} << (lane + b))) != 0) {
                                dst[b] = axi_write_byte(data, lane + b);
                            }
                        }
                        ++write_count_;
                    }
                    response = AxiResp::Exokay;
                }
                pending_w_.erase(pending_w_.begin(), pending_w_.begin() + consumed);
                pending_aw_.erase(pending_aw_.begin());
                pending_b_.push_back(Axi4B{.id = aw.id, .resp = response});
                continue;
            }
            const auto beat = pending_w_.front();
            pending_w_.erase(pending_w_.begin());
            const uint32_t beat_bytes = uint32_t{1} << aw.size;
            const uint32_t bus_bytes =
                Spec.unified_write_address_data
                    ? spec_.beat_bytes
                    : std::min<uint32_t>(spec_.beat_bytes, sizeof(simrv::xlen::Word));
            const uint32_t expected_beats = static_cast<uint32_t>(aw.len) + 1U;
            const bool expected_last = axi_write_beat_ + 1U == expected_beats;
            if (beat.last != expected_last) axi_write_response_ = AxiResp::Slverr;
            const auto geometry = machine_.memory_geometry();
            {
                const auto beat_addr = axi_beat_address(aw.addr, beat_bytes, expected_beats,
                                                        aw.burst, axi_write_beat_);
                const uint32_t active_bytes = beat_bytes - (beat_addr & (beat_bytes - 1U));
                const uint32_t lane = beat_addr % bus_bytes;
                const uint32_t lane_mask = ((uint32_t{1} << active_bytes) - 1U) << lane;
                if ((beat.strb & ~lane_mask) != 0U) {
                    axi_write_response_ = AxiResp::Slverr;
                } else if (machine_.ram_data() != nullptr &&
                           geometry.contains(simrv::xlen::PhysAddr{beat_addr}, active_bytes)) {
                    auto* dst = machine_.ram_data() + (beat_addr - geometry.dram_base.raw());
                    if (beat.strb != 0) {
                        machine_.memory().reservation_table().invalidate_range(
                            simrv::xlen::PhysAddr{beat_addr}, active_bytes);
                    }
                    for (uint32_t b = 0; b < active_bytes; ++b) {
                        if ((beat.strb & (1U << (lane + b))) != 0U) {
                            dst[b] = axi_write_byte(beat, lane + b);
                        }
                    }
                    ++write_count_;
                } else {
                    if (active_bytes == beat_bytes && bus_bytes <= sizeof(simrv::xlen::Word) &&
                        lane + active_bytes <= sizeof(simrv::xlen::Word)) {
                        TlChannelA req{};
                        req.opcode = beat.strb == lane_mask ? TlOpcodeA::PutFullData
                                                            : TlOpcodeA::PutPartialData;
                        req.address = simrv::xlen::PhysAddr{beat_addr};
                        req.size = aw.size;
                        req.source = static_cast<TlSourceId>(aw.id);
                        req.data = beat.data >> (lane * 8U);
                        req.mask = static_cast<TlMask>((beat.strb >> lane)
                                                       << (beat_addr & (kTlBeatBytes - 1U)));
                        TlChannelD d_resp{};
                        if (!router_.route_request(req, d_resp) || d_resp.failed()) {
                            axi_write_response_ = AxiResp::Decerr;
                        }
                    } else {
                        for (uint32_t b = 0; b < active_bytes; ++b) {
                            if ((beat.strb & (1U << (lane + b))) == 0) continue;
                            TlChannelA req{};
                            req.opcode = TlOpcodeA::PutFullData;
                            req.address = simrv::xlen::PhysAddr{beat_addr + b};
                            req.source = static_cast<TlSourceId>(aw.id);
                            req.data = std::to_integer<uint8_t>(axi_write_byte(beat, lane + b));
                            req.mask = TlChannelA::compute_mask(0, req.address);
                            TlChannelD d_resp{};
                            if (!router_.route_request(req, d_resp) || d_resp.failed()) {
                                axi_write_response_ = AxiResp::Decerr;
                            }
                        }
                    }
                }
            }
            ++axi_write_beat_;
            if (beat.last || expected_last) {
                pending_aw_.erase(pending_aw_.begin());
                pending_b_.push_back(Axi4B{.id = aw.id, .resp = axi_write_response_});
                axi_write_beat_ = 0;
                axi_write_response_ = AxiResp::Okay;
            }
        }
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
        if (size_bytes == 0 || (size_bytes & (size_bytes - 1U)) != 0 ||
            size_bytes > std::min<size_t>(spec_.beat_bytes, sizeof(simrv::xlen::Word))) {
            return {AxiResp::Decerr, {}};
        }
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
            const auto beat_addr =
                axi_beat_address(addr, size_bytes, uint32_t{len} + 1U, AxiBurst::Incr, i);
            const auto lane =
                beat_addr % std::min<uint32_t>(spec_.beat_bytes, sizeof(simrv::xlen::Word));
            out.push_back(r.data >> (lane * 8U));
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
        if (data.empty() || data.size() > spec_.max_burst_beats || size_bytes == 0 ||
            (size_bytes & (size_bytes - 1U)) != 0 ||
            size_bytes > std::min<size_t>(spec_.beat_bytes, sizeof(simrv::xlen::Word))) {
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
            const auto beat_addr =
                axi_beat_address(addr, size_bytes, static_cast<uint32_t>(data.size()),
                                 AxiBurst::Incr, static_cast<uint32_t>(i));
            const auto lane =
                beat_addr % std::min<uint32_t>(spec_.beat_bytes, sizeof(simrv::xlen::Word));
            const auto active_bytes = size_bytes - (beat_addr & (size_bytes - 1U));
            w.data = data[i] << (lane * 8U);
            w.strb = (strb & ((uint32_t{1} << active_bytes) - 1U)) << lane;
            w.last = (i + 1U == data.size());
            if (!send_w(w)) return AxiResp::Slverr;
        }
        Axi4B b{};
        if (!recv_b(b)) return AxiResp::Slverr;
        return b.resp;
    }
}
