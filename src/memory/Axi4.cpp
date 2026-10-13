/**
 * @file Axi4.cpp
 * @brief Implementation of AMBA AXI4 transaction tracer and TileLink bridge.
 */
#include "simrv/memory/Axi4.hpp"

#include <bit>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>

#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::memory {

namespace {

[[nodiscard]] auto valid_config(const pipeline::AxiConfig& config) noexcept -> bool {
    return config.data_width_bytes != 0 && config.data_width_bytes <= sizeof(simrv::xlen::Word) &&
           std::has_single_bit(config.data_width_bytes) && config.id_width_bits != 0 &&
           config.id_width_bits <= 32 && config.burst_length_max != 0 &&
           config.burst_length_max <= 256;
}

[[nodiscard]] auto id_fits(uint32_t id, uint32_t id_width_bits) noexcept -> bool {
    return id_width_bits == 32 || (id >> id_width_bits) == 0;
}

[[nodiscard]] auto burst_is_valid(AxiBurst burst, uint8_t size, uint8_t len,
                                  const pipeline::AxiConfig& config,
                                  simrv::xlen::Address address) noexcept -> bool {
    if (!valid_config(config) || size >= 32) return false;
    const uint32_t bytes = uint32_t{1} << size;
    const uint32_t beats = static_cast<uint32_t>(len) + 1;
    if (bytes > config.data_width_bytes || beats > config.burst_length_max) return false;
    switch (burst) {
        case AxiBurst::Fixed:
            return beats <= 16;
        case AxiBurst::Incr:
            return true;
        case AxiBurst::Wrap:
            return (beats == 2 || beats == 4 || beats == 8 || beats == 16) &&
                   (static_cast<uint64_t>(address) & (bytes - 1)) == 0;
        case AxiBurst::Reserved:
            return false;
    }
    return false;
}

[[nodiscard]] auto beat_address(simrv::xlen::Address start, uint32_t beat_bytes,
                                uint32_t beat_count, AxiBurst burst, uint32_t index) noexcept
    -> simrv::xlen::Address {
    const auto address = static_cast<uint64_t>(start);
    if (burst == AxiBurst::Fixed || index == 0) return start;

    const uint64_t aligned_start = address & ~static_cast<uint64_t>(beat_bytes - 1);
    if (burst == AxiBurst::Incr) {
        return static_cast<simrv::xlen::Address>(aligned_start +
                                                 static_cast<uint64_t>(index) * beat_bytes);
    }

    const uint64_t wrap_bytes = static_cast<uint64_t>(beat_bytes) * beat_count;
    const uint64_t wrap_base = address & ~(wrap_bytes - 1);
    const uint64_t offset =
        (address - wrap_base + static_cast<uint64_t>(index) * beat_bytes) % wrap_bytes;
    return static_cast<simrv::xlen::Address>(wrap_base + offset);
}

[[nodiscard]] auto encode_size(uint8_t size_bytes, uint32_t data_width_bytes) noexcept
    -> std::optional<uint8_t> {
    if (size_bytes == 0 || !std::has_single_bit(size_bytes) || size_bytes > data_width_bytes) {
        return std::nullopt;
    }
    return static_cast<uint8_t>(std::countr_zero(size_bytes));
}

}  // namespace

Axi4Tracer::Axi4Tracer(const std::string& path) { set_path(path); }

Axi4Tracer::~Axi4Tracer() {
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

void Axi4Tracer::enable(bool en) { enabled_ = en; }

void Axi4Tracer::set_path(const std::string& path) {
    const std::lock_guard lock(mutex_);
    if (out_.is_open()) out_.close();
    if (path.empty()) return;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    out_.open(path, std::ios::out | std::ios::trunc);
}

void Axi4Tracer::trace_aw(uint64_t cycle, const Axi4Aw& aw) {
    if (!enabled_ || !out_.is_open()) return;
    const std::lock_guard lock(mutex_);
    out_ << std::format("[{:>10}] AW: ID={:<2} ADDR=0x{:08x} LEN={:<2} SIZE={}B BURST={}\n", cycle,
                        aw.id, static_cast<uint64_t>(aw.addr), static_cast<uint32_t>(aw.len),
                        aw.size < 32 ? (uint32_t{1} << aw.size) : 0, to_string(aw.burst));
}

void Axi4Tracer::trace_w(uint64_t cycle, const Axi4W& w) {
    if (!enabled_ || !out_.is_open()) return;
    const std::lock_guard lock(mutex_);
    out_ << std::format("[{:>10}] W:        DATA=0x{:016x} STRB=0x{:02x} LAST={}\n", cycle,
                        static_cast<uint64_t>(w.data), w.strb, w.last ? 1 : 0);
}

void Axi4Tracer::trace_b(uint64_t cycle, const Axi4B& b) {
    if (!enabled_ || !out_.is_open()) return;
    const std::lock_guard lock(mutex_);
    out_ << std::format("[{:>10}] B:  ID={:<2} RESP={}\n", cycle, b.id, to_string(b.resp));
}

void Axi4Tracer::trace_ar(uint64_t cycle, const Axi4Ar& ar) {
    if (!enabled_ || !out_.is_open()) return;
    const std::lock_guard lock(mutex_);
    out_ << std::format("[{:>10}] AR: ID={:<2} ADDR=0x{:08x} LEN={:<2} SIZE={}B BURST={}\n", cycle,
                        ar.id, static_cast<uint64_t>(ar.addr), static_cast<uint32_t>(ar.len),
                        ar.size < 32 ? (uint32_t{1} << ar.size) : 0, to_string(ar.burst));
}

void Axi4Tracer::trace_r(uint64_t cycle, const Axi4R& r) {
    if (!enabled_ || !out_.is_open()) return;
    const std::lock_guard lock(mutex_);
    out_ << std::format("[{:>10}] R:  ID={:<2} DATA=0x{:016x} RESP={} LAST={}\n", cycle, r.id,
                        static_cast<uint64_t>(r.data), to_string(r.resp), r.last ? 1 : 0);
}

// -----------------------------------------------------------------------------
// Axi4Bridge Implementation
// -----------------------------------------------------------------------------

Axi4Bridge::Axi4Bridge(core::Machine* machine, pipeline::AxiConfig config)
    : machine_(machine), config_(config), tracer_{} {
    if (config_.trace_axi) {
        tracer_.enable(true);
    }
}

auto Axi4Bridge::send_ar(const Axi4Ar& ar) -> bool {
    if (!config_.enabled || !burst_is_valid(ar.burst, ar.size, ar.len, config_, ar.addr) ||
        !id_fits(ar.id, config_.id_width_bits) ||
        pending_ar_.size() >= config_.max_outstanding_reads) {
        return false;
    }
    const uint64_t current_cycle =
        machine_ != nullptr ? machine_->memory().system_bus().cycle() : 0;
    tracer_.trace_ar(current_cycle, ar);

    const uint32_t beat_bytes = uint32_t{1} << ar.size;
    const uint32_t total_beats = static_cast<uint32_t>(ar.len) + 1U;

    for (uint32_t i = 0; i < total_beats; ++i) {
        const auto beat_addr = beat_address(ar.addr, beat_bytes, total_beats, ar.burst, i);
        simrv::xlen::Word val{0};
        AxiResp resp = AxiResp::Okay;

        if (machine_ != nullptr && machine_->ram_data() != nullptr) {
            const auto geometry = machine_->memory_geometry();
            if (geometry.contains(beat_addr, beat_bytes)) {
                std::memcpy(&val, machine_->ram_data() + (beat_addr - geometry.dram_base),
                            std::min<std::size_t>(beat_bytes, sizeof(val)));
            } else {
                TlChannelA req{};
                req.opcode = TlOpcodeA::Get;
                req.address = beat_addr;
                req.size = ar.size;
                req.source = 0;
                req.mask = TlChannelA::compute_mask(req.size, req.address);
                if (machine_->memory().system_bus().send_request(req)) {
                    TlChannelD d_resp{};
                    if (machine_->memory().system_bus().get_response(req.source, d_resp)) {
                        if (d_resp.failed()) {
                            resp = AxiResp::Decerr;
                        } else {
                            val = d_resp.data;
                        }
                    } else {
                        resp = AxiResp::Slverr;
                    }
                } else {
                    resp = AxiResp::Decerr;
                }
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

auto Axi4Bridge::recv_r(Axi4R& r) -> bool {
    if (pending_r_.empty()) return false;
    r = pending_r_.front();
    pending_r_.erase(pending_r_.begin());

    const uint64_t current_cycle =
        machine_ != nullptr ? machine_->memory().system_bus().cycle() : 0;
    tracer_.trace_r(current_cycle, r);

    if (r.last && !pending_ar_.empty()) {
        pending_ar_.erase(pending_ar_.begin());
    }
    return true;
}

auto Axi4Bridge::send_aw(const Axi4Aw& aw) -> bool {
    if (!config_.enabled || !burst_is_valid(aw.burst, aw.size, aw.len, config_, aw.addr) ||
        !id_fits(aw.id, config_.id_width_bits) ||
        pending_aw_.size() >= config_.max_outstanding_writes) {
        return false;
    }
    const uint64_t current_cycle =
        machine_ != nullptr ? machine_->memory().system_bus().cycle() : 0;
    tracer_.trace_aw(current_cycle, aw);
    pending_aw_.push_back(aw);
    return true;
}

auto Axi4Bridge::send_w(const Axi4W& w) -> bool {
    if (pending_aw_.empty()) return false;
    const uint64_t current_cycle =
        machine_ != nullptr ? machine_->memory().system_bus().cycle() : 0;
    tracer_.trace_w(current_cycle, w);
    pending_w_.push_back(w);

    if (w.last) {
        const auto aw = pending_aw_.front();
        pending_aw_.erase(pending_aw_.begin());

        AxiResp resp = AxiResp::Okay;
        const uint32_t beat_bytes = uint32_t{1} << aw.size;
        const uint32_t expected_beats = static_cast<uint32_t>(aw.len) + 1;

        const bool complete_burst = pending_w_.size() == expected_beats;
        if (!complete_burst) {
            resp = AxiResp::Slverr;
        }

        for (std::size_t i = 0; complete_burst && i < pending_w_.size(); ++i) {
            const auto beat_addr = beat_address(aw.addr, beat_bytes, expected_beats, aw.burst,
                                                static_cast<uint32_t>(i));
            const auto& beat = pending_w_[i];
            const uint32_t beat_mask = beat_bytes == 32 ? std::numeric_limits<uint32_t>::max()
                                                        : (uint32_t{1} << beat_bytes) - 1;
            const uint32_t strobe = beat.strb & beat_mask;

            if (machine_ != nullptr && machine_->ram_data() != nullptr) {
                const auto geometry = machine_->memory_geometry();
                if (geometry.contains(beat_addr, beat_bytes)) {
                    auto* destination = machine_->ram_data() + (beat_addr - geometry.dram_base);
                    for (uint32_t lane = 0; lane < beat_bytes; ++lane) {
                        if ((strobe & (uint32_t{1} << lane)) != 0) {
                            std::memcpy(
                                destination + lane,
                                reinterpret_cast<const simrv::xlen::Byte*>(&beat.data) + lane, 1);
                        }
                    }
                } else {
                    const uint32_t natural_mask =
                        TlChannelA::compute_mask(aw.size, simrv::xlen::PhysAddr{beat_addr});
                    const uint32_t transfer_mask = strobe & natural_mask;
                    if (transfer_mask == 0) continue;
                    TlChannelA req{};
                    req.opcode = transfer_mask == natural_mask ? TlOpcodeA::PutFullData
                                                               : TlOpcodeA::PutPartialData;
                    req.address = beat_addr;
                    req.size = aw.size;
                    req.source = 0;
                    req.data = beat.data;
                    req.mask = static_cast<TlMask>(transfer_mask);
                    if (machine_->memory().system_bus().send_request(req)) {
                        TlChannelD d_resp{};
                        if (!machine_->memory().system_bus().get_response(req.source, d_resp) ||
                            d_resp.failed()) {
                            resp = AxiResp::Slverr;
                        }
                    } else {
                        resp = AxiResp::Decerr;
                    }
                }
            }
        }
        pending_w_.clear();
        pending_b_.push_back(Axi4B{.id = aw.id, .resp = resp});
    }
    return true;
}

auto Axi4Bridge::recv_b(Axi4B& b) -> bool {
    if (pending_b_.empty()) return false;
    b = pending_b_.front();
    pending_b_.erase(pending_b_.begin());

    const uint64_t current_cycle =
        machine_ != nullptr ? machine_->memory().system_bus().cycle() : 0;
    tracer_.trace_b(current_cycle, b);
    return true;
}

auto Axi4Bridge::axi_read(simrv::xlen::Address addr, uint8_t size_bytes, uint8_t len)
    -> std::pair<AxiResp, std::vector<simrv::xlen::Word>> {
    const auto size_enc = encode_size(size_bytes, config_.data_width_bytes);
    if (!size_enc) return {AxiResp::Slverr, {}};

    Axi4Ar ar{
        .id = 0,
        .addr = addr,
        .len = len,
        .size = *size_enc,
        .burst = AxiBurst::Incr,
    };

    if (!send_ar(ar)) {
        return {AxiResp::Slverr, {}};
    }

    std::vector<simrv::xlen::Word> words;
    AxiResp overall_resp = AxiResp::Okay;
    Axi4R r{};
    while (recv_r(r)) {
        words.push_back(r.data);
        if (r.resp != AxiResp::Okay) overall_resp = r.resp;
        if (r.last) break;
    }

    return {overall_resp, words};
}

auto Axi4Bridge::axi_write(simrv::xlen::Address addr, std::span<const simrv::xlen::Word> data,
                           uint8_t size_bytes, uint32_t strb) -> AxiResp {
    if (data.empty()) return AxiResp::Okay;

    const auto size_enc = encode_size(size_bytes, config_.data_width_bytes);
    if (!size_enc || data.size() > config_.burst_length_max || data.size() > 256) {
        return AxiResp::Slverr;
    }

    const auto len = static_cast<uint8_t>(data.size() - 1);
    Axi4Aw aw{
        .id = 0,
        .addr = addr,
        .len = len,
        .size = *size_enc,
        .burst = AxiBurst::Incr,
    };

    if (!send_aw(aw)) {
        return AxiResp::Slverr;
    }

    for (std::size_t i = 0; i < data.size(); ++i) {
        Axi4W w{
            .data = data[i],
            .strb = strb,
            .last = (i == data.size() - 1),
        };
        send_w(w);
    }

    Axi4B b{};
    if (recv_b(b)) {
        return b.resp;
    }
    return AxiResp::Slverr;
}

}  // namespace simrv::memory
