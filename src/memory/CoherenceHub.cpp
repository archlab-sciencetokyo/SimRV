/**
 * @file CoherenceHub.cpp
 * @brief TileLink-C Directory-Based Coherence Hub implementation with MESI semantics.
 */
#include "simrv/memory/CoherenceHub.hpp"

#include <bit>
#include <cstring>
#include <format>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/TileLinkBus.hpp"

namespace simrv::memory {

CoherenceHub::CoherenceHub(simrv::core::Machine& machine) : machine_(machine) {
    for (auto& entry : fast_cache_) {
        entry.line_base = ~LineAddress{0};
    }
}

auto CoherenceHub::configure_topology(const simrv::core::SmpTopologyConfig& topology)
    -> std::expected<void, std::string> {
    if (auto result = topology.validate(); !result) return std::unexpected(result.error());
    topology_ = topology;
    l2_caches_.clear();
    l3_enabled_ = topology.l3.enabled;
    l3_cache_.flush(true);
    if (l3_enabled_ &&
        !l3_cache_.configure(topology.l3.capacity_bytes, topology.l3.associativity)) {
        return std::unexpected("shared L3 geometry exceeds simulator cache backing");
    }
    for (const auto& cluster : topology.clusters) {
        if (!cluster.l2.enabled) continue;
        auto cache = std::make_unique<ClusterCache>();
        cache->id = cluster.id;
        cache->harts = cluster.harts;
        cache->config = cluster.l2;
        if (!cache->cache.configure(cluster.l2.capacity_bytes, cluster.l2.associativity)) {
            return std::unexpected(
                std::format("cluster {} L2 geometry exceeds simulator backing", cluster.id));
        }
        l2_caches_.push_back(std::move(cache));
    }
    clear();
    return {};
}

auto CoherenceHub::l2_cache_for_hart(uint32_t hart) -> ClusterCache* {
    const auto cluster_id = topology_.cluster_for_hart(hart);
    const auto it = std::ranges::find_if(
        l2_caches_, [cluster_id](const auto& cache) { return cache->id == cluster_id; });
    return it == l2_caches_.end() ? nullptr : it->get();
}

auto CoherenceHub::shared_l2_for_hart(uint32_t hart) noexcept -> simrv::cache::L2Cache* {
    auto* cluster = l2_cache_for_hart(hart);
    return cluster == nullptr ? nullptr : &cluster->cache;
}

void CoherenceHub::invalidate_shared_caches(LineAddress line_base) {
    const auto aligned = line_base & ~(static_cast<LineAddress>(kLineBytes - 1u));
    if (l3_enabled_) l3_cache_.invalidate_line(aligned);
    for (auto& cache : l2_caches_) cache->cache.invalidate_line(aligned);
}

void CoherenceHub::update_dir_entry(LineAddress line_base, const DirectoryEntry& entry) {
    const LineAddress aligned_addr = line_base & ~(static_cast<LineAddress>(kLineBytes - 1u));
    directory_[aligned_addr] = entry;
    const size_t idx = (aligned_addr >> 6) & kFastCacheMask;
    fast_cache_[idx] = FastCacheEntry{.line_base = aligned_addr, .entry = entry};
}

void CoherenceHub::erase_dir_entry(LineAddress line_base) {
    const LineAddress aligned_addr = line_base & ~(static_cast<LineAddress>(kLineBytes - 1u));
    directory_.erase(aligned_addr);
    const size_t idx = (aligned_addr >> 6) & kFastCacheMask;
    if (fast_cache_[idx].line_base == aligned_addr) {
        fast_cache_[idx].line_base = ~LineAddress{0};
    }
}

auto CoherenceHub::lookup_dir_entry(LineAddress line_base, DirectoryEntry& out_entry) -> bool {
    const LineAddress aligned_addr = line_base & ~(static_cast<LineAddress>(kLineBytes - 1u));
    const size_t idx = (aligned_addr >> 6) & kFastCacheMask;
    if (fast_cache_[idx].line_base == aligned_addr) {
        out_entry = fast_cache_[idx].entry;
        return true;
    }
    auto it = directory_.find(aligned_addr);
    if (it != directory_.end()) {
        out_entry = it->second;
        fast_cache_[idx] = FastCacheEntry{.line_base = aligned_addr, .entry = it->second};
        return true;
    }
    return false;
}

auto CoherenceHub::get_directory_state(LineAddress line_base) const -> DirectoryEntry {
    const LineAddress aligned_addr = line_base & ~(static_cast<LineAddress>(kLineBytes - 1u));
    const size_t idx = (aligned_addr >> 6) & kFastCacheMask;
    if (fast_cache_[idx].line_base == aligned_addr) {
        return fast_cache_[idx].entry;
    }
    auto it = directory_.find(aligned_addr);
    if (it != directory_.end()) {
        return it->second;
    }
    return DirectoryEntry{};
}

auto CoherenceHub::validate_directory() const -> std::expected<void, std::string> {
    for (const auto& [address, entry] : directory_) {
        const bool has_owner = entry.owner_hart.has_value();
        const unsigned sharers = std::popcount(entry.sharers_mask);
        if (entry.state == MesiState::Invalid || entry.sharers_mask == 0) {
            return std::unexpected(std::format("invalid live directory entry at {:#x}", address));
        }
        if (entry.state == MesiState::Shared && has_owner) {
            return std::unexpected(std::format("shared line has an owner at {:#x}", address));
        }
        if ((entry.state == MesiState::Exclusive || entry.state == MesiState::Modified) &&
            (!has_owner || sharers != 1 ||
             (entry.sharers_mask & (uint64_t{1} << *entry.owner_hart)) == 0)) {
            return std::unexpected(
                std::format("Trunk line lacks its unique owner at {:#x}", address));
        }
    }
    return {};
}

void CoherenceHub::probe_hart_dcache(simrv::xlen::HartId hart_id, const TlChannelB& probe_req,
                                     TlChannelC& probe_resp,
                                     std::array<simrv::xlen::Byte, kLineBytes>& dirty_data) {
    if (hart_id >= machine_.num_harts()) {
        return;
    }
    machine_.memory().system_bus().record_transaction(
        TileLinkChannel::B, to_string(probe_req.opcode), probe_req.source, 0,
        probe_req.address.raw(), to_string(probe_req.cap));
    auto& target_cpu = machine_.hart(hart_id);
    if (target_cpu.dcache.handle_probe(probe_req, probe_resp, dirty_data)) {
        stats_.probe_count++;
        if (probe_resp.opcode == TlOpcodeC::ProbeAckData) {
            stats_.writeback_count++;
            target_cpu.record_hpm_event(core::HpmEvent::DCacheWriteback);
        }
    }
    machine_.memory().system_bus().record_transaction(
        TileLinkChannel::C, to_string(probe_resp.opcode), probe_resp.source, 0,
        probe_resp.address.raw(), to_string(probe_resp.report));
}

void CoherenceHub::probe_hart_icache(simrv::xlen::HartId hart_id, const TlChannelB& probe_req) {
    if (hart_id >= machine_.num_harts()) {
        return;
    }
    auto& target_cpu = machine_.hart(hart_id);
    TlChannelC ic_resp{};
    if (target_cpu.icache.handle_probe(probe_req, ic_resp)) {
        stats_.probe_count++;
        machine_.memory().system_bus().record_transaction(
            TileLinkChannel::C, to_string(ic_resp.opcode), ic_resp.source, 0, ic_resp.address.raw(),
            to_string(ic_resp.report));
    }
}

void CoherenceHub::invalidate_line_broadcast(LineAddress line_base,
                                             simrv::xlen::HartId initiator_hart) {
    const LineAddress aligned_addr = line_base & ~(static_cast<LineAddress>(kLineBytes - 1u));
    TlChannelB probe_req{};
    probe_req.opcode = TlOpcodeB::ProbeBlock;
    probe_req.cap = TlCap::ToN;
    probe_req.address = aligned_addr;

    for (uint32_t h = 0; h < machine_.num_harts(); ++h) {
        if (h != initiator_hart.value()) {
            TlChannelC resp{};
            std::array<simrv::xlen::Byte, kLineBytes> dirty{};
            probe_hart_dcache(h, probe_req, resp, dirty);
            probe_hart_icache(h, probe_req);
            stats_.invalidation_count++;
        }
    }

    machine_.memory_.reservation_table().invalidate_matching(aligned_addr, initiator_hart);
    invalidate_shared_caches(aligned_addr);
    erase_dir_entry(aligned_addr);
}

void CoherenceHub::invalidate_line_external(LineAddress line_base) {
    const LineAddress aligned_addr = line_base & ~(static_cast<LineAddress>(kLineBytes - 1u));
    DirectoryEntry dir_entry{};
    const bool is_tracked = lookup_dir_entry(aligned_addr, dir_entry);

    if (is_tracked && dir_entry.state != MesiState::Invalid) {
        TlChannelB probe{};
        probe.opcode = TlOpcodeB::ProbeBlock;
        probe.cap = TlCap::ToN;
        probe.address = aligned_addr;
        for (uint32_t hart = 0; hart < machine_.num_harts(); ++hart) {
            if ((dir_entry.sharers_mask & (1ULL << hart)) == 0 &&
                (!dir_entry.owner_hart.has_value() || *dir_entry.owner_hart != hart)) {
                continue;
            }
            TlChannelC response{};
            std::array<simrv::xlen::Byte, kLineBytes> dirty{};
            probe_hart_dcache(hart, probe, response, dirty);
            if (response.opcode == TlOpcodeC::ProbeAckData) {
                auto* dram = machine_.ram_data();
                const auto geometry = machine_.memory_geometry();
                if (dram != nullptr && geometry.contains(aligned_addr, kLineBytes)) {
                    std::memcpy(dram + (aligned_addr - geometry.dram_base), dirty.data(),
                                kLineBytes);
                }
            }
            probe_hart_icache(hart, probe);
            ++stats_.invalidation_count;
        }
    }
    machine_.memory_.reservation_table().invalidate_matching(aligned_addr);
    invalidate_shared_caches(aligned_addr);
    erase_dir_entry(aligned_addr);
}

void CoherenceHub::register_preloaded_line(LineAddress line_base, simrv::xlen::HartId hart) {
    const LineAddress aligned_addr = line_base & ~(static_cast<LineAddress>(kLineBytes - 1u));
    if (hart >= machine_.num_harts()) return;
    DirectoryEntry entry{};
    if (lookup_dir_entry(aligned_addr, entry) && entry.state != MesiState::Shared) return;
    entry.state = MesiState::Shared;
    entry.owner_hart.reset();
    entry.sharers_mask |= (uint64_t{1} << hart.val);
    update_dir_entry(aligned_addr, entry);
}

auto CoherenceHub::handle_acquire(const TlChannelA& req, TlChannelD& resp,
                                  std::array<simrv::xlen::Byte, kLineBytes>& line_buffer,
                                  uint32_t& service_latency) -> bool {
    service_latency = 1;
    const simrv::xlen::Address line_base =
        (req.address & ~(static_cast<simrv::xlen::Address>(kLineBytes - 1u))).raw();
    const auto requested_permission =
        req.grow == TlGrow::NtoB ? TlPermission::Branch : TlPermission::Trunk;

    stats_.acquire_count++;

    auto* dram_ptr = machine_.ram_data();

    DirectoryEntry dir_entry{};
    const bool dir_hit = lookup_dir_entry(line_base, dir_entry);

    if (req.opcode == TlOpcodeA::AcquirePerm) {
        // Upgrade from Branch to Trunk
        if (dir_hit && dir_entry.sharers_mask != 0) {
            const uint64_t mask = dir_entry.sharers_mask;
            for (uint32_t h = 0; h < machine_.num_harts(); ++h) {
                if (h != req.hart && (mask & (uint64_t{1} << h)) != 0u) {
                    TlChannelB probe_req{};
                    probe_req.opcode = TlOpcodeB::ProbeBlock;
                    probe_req.cap = TlCap::ToN;
                    probe_req.address = line_base;
                    TlChannelC probe_resp{};
                    std::array<simrv::xlen::Byte, kLineBytes> dirty{};
                    probe_hart_dcache(h, probe_req, probe_resp, dirty);
                    probe_hart_icache(h, probe_req);
                    stats_.invalidation_count++;
                }
            }
        }

        machine_.memory_.reservation_table().invalidate_matching(line_base, req.hart);
        invalidate_shared_caches(line_base);

        update_dir_entry(line_base, DirectoryEntry{
                                        .state = MesiState::Exclusive,
                                        .sharers_mask = (uint64_t{1} << req.hart),
                                        .owner_hart = req.hart,
                                    });

        resp.opcode = TlOpcodeD::Grant;
        resp.cap = TlCap::ToT;
        resp.size = req.size;
        resp.source = req.source;
        resp.sink = allocate_sink();
        resp.denied = false;
        resp.corrupt = false;
        stats_.grant_count++;
        return true;
    }

    // AcquireBlock (fetch line into cache)
    bool data_fetched_from_owner = false;
    TlCap granted_cap = requested_permission == TlPermission::Branch ? TlCap::ToB : TlCap::ToT;

    if (dir_hit && dir_entry.sharers_mask != 0) {
        const bool trunk_owned = permission_for(dir_entry.state) == TlPermission::Trunk &&
                                 dir_entry.owner_hart.has_value();
        if (trunk_owned &&
            (*dir_entry.owner_hart != req.hart || requested_permission == TlPermission::Branch)) {
            // Owner has Exclusive/Modified line.
            // If requesting Branch (read), probe owner to downgrade Trunk -> Branch (Shared Clean).
            // If requesting Trunk (write), probe owner to invalidate Trunk -> None.
            const auto target_cap =
                requested_permission == TlPermission::Branch ? TlCap::ToB : TlCap::ToN;
            TlChannelB probe_req{};
            probe_req.opcode = TlOpcodeB::ProbeBlock;
            probe_req.cap = target_cap;
            probe_req.address = line_base;
            TlChannelC probe_resp{};
            std::array<simrv::xlen::Byte, kLineBytes> dirty{};
            probe_hart_dcache(*dir_entry.owner_hart, probe_req, probe_resp, dirty);
            probe_hart_icache(*dir_entry.owner_hart, probe_req);

            if (probe_resp.opcode == TlOpcodeC::ProbeAckData) {
                std::memcpy(line_buffer.data(), dirty.data(), kLineBytes);
                data_fetched_from_owner = true;
                if (l3_enabled_) l3_cache_.write_line(line_base, line_buffer, MesiState::Exclusive);
                if (auto* l2 = l2_cache_for_hart(req.hart.val)) {
                    l2->cache.write_line(line_base, line_buffer, MesiState::Exclusive);
                    service_latency = l2->config.miss_latency;
                }
                const auto geometry = machine_.memory_geometry();
                if (dram_ptr != nullptr && geometry.contains(line_base, kLineBytes)) {
                    const simrv::xlen::Address offset = line_base - geometry.dram_base;
                    std::memcpy(dram_ptr + offset, dirty.data(), kLineBytes);
                }
            }

            if (requested_permission == TlPermission::Branch) {
                dir_entry.state = MesiState::Shared;
                dir_entry.sharers_mask |= (uint64_t{1} << req.hart);
                dir_entry.owner_hart.reset();
                granted_cap = TlCap::ToB;
            } else {
                dir_entry.state = MesiState::Exclusive;
                dir_entry.sharers_mask = (uint64_t{1} << req.hart);
                dir_entry.owner_hart = req.hart;
                granted_cap = TlCap::ToT;
            }
        } else if (dir_entry.state == MesiState::Shared &&
                   requested_permission == TlPermission::Trunk) {
            // Invalidate all existing sharers
            const uint64_t mask = dir_entry.sharers_mask;
            for (uint32_t h = 0; h < machine_.num_harts(); ++h) {
                if (h != req.hart && (mask & (uint64_t{1} << h)) != 0u) {
                    TlChannelB probe_req{};
                    probe_req.opcode = TlOpcodeB::ProbeBlock;
                    probe_req.cap = TlCap::ToN;
                    probe_req.address = line_base;
                    TlChannelC probe_resp{};
                    std::array<simrv::xlen::Byte, kLineBytes> dirty{};
                    probe_hart_dcache(h, probe_req, probe_resp, dirty);
                    probe_hart_icache(h, probe_req);
                    stats_.invalidation_count++;
                }
            }
            dir_entry.state = MesiState::Exclusive;
            dir_entry.sharers_mask = (uint64_t{1} << req.hart);
            dir_entry.owner_hart = req.hart;
            granted_cap = TlCap::ToT;
        } else if (dir_entry.state == MesiState::Shared &&
                   requested_permission == TlPermission::Branch) {
            dir_entry.sharers_mask |= (uint64_t{1} << req.hart);
            granted_cap = TlCap::ToB;
        }
    } else {
        // MESI Optimization: Sole-sharer read miss receives Exclusive Clean (Trunk)
        // Subsequent stores will hit immediately without emitting AcquirePerm bus requests.
        granted_cap = TlCap::ToT;
        dir_entry.state = MesiState::Exclusive;
        dir_entry.sharers_mask = (uint64_t{1} << req.hart);
        dir_entry.owner_hart = req.hart;
    }

    if (!data_fetched_from_owner) {
        auto* l2 = l2_cache_for_hart(req.hart.val);
        if (l2 != nullptr && l2->cache.read_line(line_base, line_buffer)) {
            service_latency = l2->config.hit_latency;
        } else {
            uint32_t lower_latency = 1;
            MesiState l3_state = MesiState::Invalid;
            if (l3_enabled_ && l3_cache_.lookup_line(line_base, line_buffer, l3_state)) {
                lower_latency = topology_.l3.hit_latency;
            } else {
                const auto geometry = machine_.memory_geometry();
                if (dram_ptr != nullptr && geometry.contains(line_base, kLineBytes)) {
                    const simrv::xlen::Address offset = line_base - geometry.dram_base;
                    std::memcpy(line_buffer.data(), dram_ptr + offset, kLineBytes);
                } else {
                    std::ranges::fill(line_buffer, static_cast<simrv::xlen::Byte>(0));
                }
                lower_latency = topology_.l3.enabled ? topology_.l3.miss_latency : 1;
                if (l3_enabled_) l3_cache_.write_line(line_base, line_buffer, MesiState::Exclusive);
            }
            if (l2 != nullptr) {
                l2->cache.write_line(line_base, line_buffer, MesiState::Exclusive);
                service_latency = l2->config.miss_latency + lower_latency - 1;
            } else {
                service_latency = lower_latency;
            }
        }
    }

    if (granted_cap == TlCap::ToT) {
        machine_.memory_.reservation_table().invalidate_matching(line_base, req.hart);
    }

    update_dir_entry(line_base, dir_entry);

    resp.opcode = TlOpcodeD::GrantData;
    resp.cap = granted_cap;
    resp.size = req.size;
    resp.source = req.source;
    resp.sink = allocate_sink();
    resp.denied = false;
    resp.corrupt = false;
    stats_.grant_count++;
    return true;
}

auto CoherenceHub::handle_intent(const TlChannelA& req, TlChannelD& resp) -> bool {
    const simrv::xlen::Address line_base =
        (req.address & ~(static_cast<simrv::xlen::Address>(kLineBytes - 1u))).raw();
    stats_.prefetch_count++;

    // Warm L3 cache if not present for valid memory addresses
    std::array<simrv::xlen::Byte, kLineBytes> line_buf{};
    MesiState l3_state = MesiState::Invalid;
    if (!l3_enabled_ || !l3_cache_.lookup_line(line_base, line_buf, l3_state)) {
        auto* dram_ptr = machine_.ram_data();
        const auto geometry = machine_.memory_geometry();
        if (dram_ptr != nullptr && geometry.contains(line_base, kLineBytes)) {
            const simrv::xlen::Address offset = line_base - geometry.dram_base;
            std::memcpy(line_buf.data(), dram_ptr + offset, kLineBytes);
            if (l3_enabled_) l3_cache_.write_line(line_base, line_buf, MesiState::Exclusive);
        }
    }
    if (auto* l2 = l2_cache_for_hart(req.hart.val))
        l2->cache.write_line(line_base, line_buf, MesiState::Exclusive);

    resp.opcode = TlOpcodeD::HintAck;
    resp.size = req.size;
    resp.source = req.source;
    resp.sink = 0;
    resp.denied = false;
    resp.corrupt = false;
    return true;
}

auto CoherenceHub::handle_release(const TlChannelC& req, TlChannelD& resp,
                                  const std::array<simrv::xlen::Byte, kLineBytes>* release_data)
    -> bool {
    const simrv::xlen::Address line_base =
        (req.address & ~(static_cast<simrv::xlen::Address>(kLineBytes - 1u))).raw();
    stats_.release_count++;

    if (req.opcode == TlOpcodeC::ReleaseData && release_data != nullptr) {
        if (l3_enabled_) l3_cache_.write_line(line_base, *release_data, MesiState::Exclusive);
        if (auto* l2 = l2_cache_for_hart(req.hart.val))
            l2->cache.write_line(line_base, *release_data, MesiState::Exclusive);
        auto* dram_ptr = machine_.ram_data();
        const auto geometry = machine_.memory_geometry();
        if (dram_ptr != nullptr && geometry.contains(line_base, kLineBytes)) {
            const simrv::xlen::Address offset = line_base - geometry.dram_base;
            std::memcpy(dram_ptr + offset, release_data->data(), kLineBytes);
            stats_.writeback_count++;
        }
    }

    DirectoryEntry dir_entry{};
    if (lookup_dir_entry(line_base, dir_entry)) {
        dir_entry.sharers_mask &= ~(uint64_t{1} << req.hart);
        if (req.hart < machine_.num_harts()) {
            const auto& hart = machine_.hart(req.hart);
            const bool other_copy = hart.icache.line_state(line_base) != MesiState::Invalid ||
                                    hart.dcache.line_state(line_base) != MesiState::Invalid;
            if (other_copy) dir_entry.sharers_mask |= (uint64_t{1} << req.hart);
        }
        if (dir_entry.sharers_mask == 0) {
            erase_dir_entry(line_base);
        } else {
            dir_entry.state = MesiState::Shared;
            dir_entry.owner_hart.reset();
            update_dir_entry(line_base, dir_entry);
        }
    }

    resp.opcode = TlOpcodeD::ReleaseAck;
    resp.size = req.size;
    resp.source = req.source;
    resp.sink = 0;
    resp.denied = false;
    resp.corrupt = false;
    return true;
}

auto CoherenceHub::allocate_sink() -> TlSinkId {
    while (next_sink_ == 0 || pending_grants_.contains(next_sink_)) ++next_sink_;
    const TlSinkId sink = next_sink_++;
    pending_grants_.insert(sink);
    return sink;
}

void CoherenceHub::process_grant_ack(const TlChannelE& ack) { pending_grants_.erase(ack.sink); }

void CoherenceHub::mark_modified(LineAddress line_base, simrv::xlen::HartId hart) {
    line_base &= ~(static_cast<LineAddress>(kLineBytes - 1u));
    DirectoryEntry entry{};
    if (!lookup_dir_entry(line_base, entry) || !entry.owner_hart.has_value() ||
        *entry.owner_hart != hart || permission_for(entry.state) != TlPermission::Trunk) {
        return;
    }
    if (entry.state == MesiState::Modified) {
        return;
    }
    // DRAM stores are write-through, while the shared backing cache may still contain the line
    // fetched before the store. Once an L1 becomes authoritative, invalidate that clean copy so
    // a later refill cannot resurrect stale data after the modified L1 line is evicted.
    invalidate_shared_caches(line_base);
    entry.state = MesiState::Modified;
    update_dir_entry(line_base, entry);
}

void CoherenceHub::clear() {
    directory_.clear();
    for (auto& entry : fast_cache_) entry = {};
    pending_grants_.clear();
    l3_cache_.flush(true);
    for (auto& cache : l2_caches_) cache->cache.flush(true);
    stats_ = {};
}

}  // namespace simrv::memory
