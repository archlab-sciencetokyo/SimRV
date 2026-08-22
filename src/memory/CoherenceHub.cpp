/**
 * @file CoherenceHub.cpp
 * @brief TileLink-C Directory-Based Coherence Hub implementation with MESI semantics.
 */
#include "simrv/memory/CoherenceHub.hpp"

#include <cstring>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemoryUtil.hpp"

namespace simrv::memory {

CoherenceHub::CoherenceHub(simrv::core::Machine& machine) : machine_(machine) {
    for (auto& entry : fast_cache_) {
        entry.line_base = ~Address{0};
    }
}

void CoherenceHub::update_dir_entry(Address line_base, const DirectoryEntry& entry) {
    const Address aligned_addr = line_base & ~(static_cast<Address>(kLineBytes - 1u));
    directory_[aligned_addr] = entry;
    const size_t idx = (aligned_addr >> 5) & kFastCacheMask;
    fast_cache_[idx] = FastCacheEntry{.line_base = aligned_addr, .entry = entry};
}

void CoherenceHub::erase_dir_entry(Address line_base) {
    const Address aligned_addr = line_base & ~(static_cast<Address>(kLineBytes - 1u));
    directory_.erase(aligned_addr);
    const size_t idx = (aligned_addr >> 5) & kFastCacheMask;
    if (fast_cache_[idx].line_base == aligned_addr) {
        fast_cache_[idx].line_base = ~Address{0};
    }
}

auto CoherenceHub::lookup_dir_entry(Address line_base, DirectoryEntry& out_entry) -> bool {
    const Address aligned_addr = line_base & ~(static_cast<Address>(kLineBytes - 1u));
    const size_t idx = (aligned_addr >> 5) & kFastCacheMask;
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

auto CoherenceHub::get_directory_state(Address line_base) const -> DirectoryEntry {
    const Address aligned_addr = line_base & ~(static_cast<Address>(kLineBytes - 1u));
    const size_t idx = (aligned_addr >> 5) & kFastCacheMask;
    if (fast_cache_[idx].line_base == aligned_addr) {
        return fast_cache_[idx].entry;
    }
    auto it = directory_.find(aligned_addr);
    if (it != directory_.end()) {
        return it->second;
    }
    return DirectoryEntry{};
}

void CoherenceHub::probe_hart_dcache(uint32_t hart_id, const TlChannelB& probe_req,
                                     TlChannelC& probe_resp,
                                     std::array<Byte, kLineBytes>& dirty_data) {
    if (hart_id >= machine_.num_harts()) {
        return;
    }
    auto& target_cpu = machine_.hart(hart_id);
    if (target_cpu.dcache.handle_probe(probe_req, probe_resp, dirty_data)) {
        stats_.probe_count++;
        if (probe_resp.opcode == TlOpcodeC::ProbeAckData) {
            stats_.writeback_count++;
        }
    }
}

void CoherenceHub::probe_hart_icache(uint32_t hart_id, const TlChannelB& probe_req) {
    if (hart_id >= machine_.num_harts()) {
        return;
    }
    auto& target_cpu = machine_.hart(hart_id);
    TlChannelC ic_resp{};
    if (target_cpu.icache.handle_probe(probe_req, ic_resp)) {
        stats_.probe_count++;
    }
}

void CoherenceHub::invalidate_line_broadcast(Address line_base, uint32_t initiator_hart) {
    const Address aligned_addr = line_base & ~(static_cast<Address>(kLineBytes - 1u));
    TlChannelB probe_req{};
    probe_req.opcode = TlOpcodeB::ProbeBlock;
    probe_req.param = static_cast<uint8_t>(CoherenceState::None);
    probe_req.address = aligned_addr;

    for (uint32_t h = 0; h < machine_.num_harts(); ++h) {
        if (h != initiator_hart) {
            TlChannelC resp{};
            std::array<Byte, kLineBytes> dirty{};
            probe_hart_dcache(h, probe_req, resp, dirty);
            probe_hart_icache(h, probe_req);
            stats_.invalidation_count++;
        }
    }

    machine_.memory_.reservation_table().invalidate_matching(aligned_addr,
                                                             static_cast<HartId>(initiator_hart));
    erase_dir_entry(aligned_addr);
}

void CoherenceHub::invalidate_line_external(Address line_base) {
    const Address aligned_addr = line_base & ~(static_cast<Address>(kLineBytes - 1u));
    TlChannelB probe{};
    probe.opcode = TlOpcodeB::ProbeBlock;
    probe.param = static_cast<uint8_t>(CoherenceState::None);
    probe.address = aligned_addr;
    for (uint32_t hart = 0; hart < machine_.num_harts(); ++hart) {
        TlChannelC response{};
        std::array<Byte, kLineBytes> dirty{};
        probe_hart_dcache(hart, probe, response, dirty);
        probe_hart_icache(hart, probe);
        ++stats_.invalidation_count;
    }
    machine_.memory_.reservation_table().invalidate_matching(aligned_addr);
    erase_dir_entry(aligned_addr);
}

auto CoherenceHub::handle_acquire(const TlChannelA& req, TlChannelD& resp,
                                  std::array<Byte, kLineBytes>& line_buffer) -> bool {
    const Address line_base = req.address & ~(static_cast<Address>(kLineBytes - 1u));
    const auto req_state =
        (req.opcode == TlOpcodeA::AcquirePerm)
            ? CoherenceState::Trunk
            : ((req.param != 0) ? static_cast<CoherenceState>(req.param) : CoherenceState::Trunk);

    stats_.acquire_count++;

    auto dram_ptr = machine_.memory_.mmu() ? machine_.memory_.mmu()->mmem() : nullptr;

    DirectoryEntry dir_entry{};
    const bool dir_hit = lookup_dir_entry(line_base, dir_entry);

    if (req.opcode == TlOpcodeA::AcquirePerm) {
        // Upgrade from Branch to Trunk
        if (dir_hit && dir_entry.sharers_mask != 0) {
            uint32_t mask = dir_entry.sharers_mask;
            for (uint32_t h = 0; h < machine_.num_harts(); ++h) {
                if (h != req.hart && ((mask & (1u << h)) != 0u)) {
                    TlChannelB probe_req{};
                    probe_req.opcode = TlOpcodeB::ProbeBlock;
                    probe_req.param = static_cast<uint8_t>(CoherenceState::None);
                    probe_req.address = line_base;
                    TlChannelC probe_resp{};
                    std::array<Byte, kLineBytes> dirty{};
                    probe_hart_dcache(h, probe_req, probe_resp, dirty);
                    probe_hart_icache(h, probe_req);
                    stats_.invalidation_count++;
                }
            }
        }

        machine_.memory_.reservation_table().invalidate_matching(line_base, req.hart);

        update_dir_entry(line_base, DirectoryEntry{
                                        .state = CoherenceState::Trunk,
                                        .sharers_mask = (1u << req.hart),
                                        .owner_hart = static_cast<uint32_t>(req.hart),
                                    });

        resp.opcode = TlOpcodeD::Grant;
        resp.param = static_cast<uint8_t>(CoherenceState::Trunk);
        resp.source = req.source;
        resp.sink = 0;
        resp.error = false;
        stats_.grant_count++;
        return true;
    }

    // AcquireBlock (fetch line into cache)
    bool data_fetched_from_owner = false;
    CoherenceState granted_state = req_state;

    if (dir_hit && dir_entry.sharers_mask != 0) {
        if (dir_entry.state == CoherenceState::Trunk && dir_entry.owner_hart != req.hart) {
            // Owner has Exclusive/Modified line.
            // If requesting Branch (read), probe owner to downgrade Trunk -> Branch (Shared Clean).
            // If requesting Trunk (write), probe owner to invalidate Trunk -> None.
            const auto target_probe_state = (req_state == CoherenceState::Branch)
                                                ? CoherenceState::Branch
                                                : CoherenceState::None;
            TlChannelB probe_req{};
            probe_req.opcode = TlOpcodeB::ProbeBlock;
            probe_req.param = static_cast<uint8_t>(target_probe_state);
            probe_req.address = line_base;
            TlChannelC probe_resp{};
            std::array<Byte, kLineBytes> dirty{};
            probe_hart_dcache(dir_entry.owner_hart, probe_req, probe_resp, dirty);

            if (probe_resp.opcode == TlOpcodeC::ProbeAckData) {
                std::memcpy(line_buffer.data(), dirty.data(), kLineBytes);
                data_fetched_from_owner = true;
                if (dram_ptr != nullptr && is_dram_access(line_base, kLineBytes)) {
                    const Address offset = line_base - simrv::memory::g_dram_base;
                    std::memcpy(dram_ptr + offset, dirty.data(), kLineBytes);
                }
            }

            if (req_state == CoherenceState::Branch) {
                dir_entry.state = CoherenceState::Branch;
                dir_entry.sharers_mask |= (1u << req.hart);
                granted_state = CoherenceState::Branch;
            } else {
                dir_entry.state = CoherenceState::Trunk;
                dir_entry.sharers_mask = (1u << req.hart);
                dir_entry.owner_hart = static_cast<uint32_t>(req.hart);
                granted_state = CoherenceState::Trunk;
            }
        } else if (dir_entry.state == CoherenceState::Branch &&
                   req_state == CoherenceState::Trunk) {
            // Invalidate all existing sharers
            uint32_t mask = dir_entry.sharers_mask;
            for (uint32_t h = 0; h < machine_.num_harts(); ++h) {
                if (h != req.hart && ((mask & (1u << h)) != 0u)) {
                    TlChannelB probe_req{};
                    probe_req.opcode = TlOpcodeB::ProbeBlock;
                    probe_req.param = static_cast<uint8_t>(CoherenceState::None);
                    probe_req.address = line_base;
                    TlChannelC probe_resp{};
                    std::array<Byte, kLineBytes> dirty{};
                    probe_hart_dcache(h, probe_req, probe_resp, dirty);
                    probe_hart_icache(h, probe_req);
                    stats_.invalidation_count++;
                }
            }
            dir_entry.state = CoherenceState::Trunk;
            dir_entry.sharers_mask = (1u << req.hart);
            dir_entry.owner_hart = static_cast<uint32_t>(req.hart);
            granted_state = CoherenceState::Trunk;
        } else if (dir_entry.state == CoherenceState::Branch &&
                   req_state == CoherenceState::Branch) {
            dir_entry.sharers_mask |= (1u << req.hart);
            granted_state = CoherenceState::Branch;
        }
    } else {
        // MESI Optimization: Sole-sharer read miss receives Exclusive Clean (Trunk)
        // Subsequent stores will hit immediately without emitting AcquirePerm bus requests.
        granted_state = CoherenceState::Trunk;
        dir_entry.state = CoherenceState::Trunk;
        dir_entry.sharers_mask = (1u << req.hart);
        dir_entry.owner_hart = static_cast<uint32_t>(req.hart);
    }

    if (!data_fetched_from_owner) {
        if (dram_ptr != nullptr && is_dram_access(line_base, kLineBytes)) {
            const Address offset = line_base - simrv::memory::g_dram_base;
            std::memcpy(line_buffer.data(), dram_ptr + offset, kLineBytes);
        } else {
            std::ranges::fill(line_buffer, static_cast<Byte>(0));
        }
    }

    if (granted_state == CoherenceState::Trunk) {
        machine_.memory_.reservation_table().invalidate_matching(line_base, req.hart);
    }

    update_dir_entry(line_base, dir_entry);

    resp.opcode = TlOpcodeD::GrantData;
    resp.param = static_cast<uint8_t>(granted_state);
    resp.source = req.source;
    resp.sink = 0;
    resp.error = false;
    stats_.grant_count++;
    return true;
}

auto CoherenceHub::handle_release(const TlChannelC& req, TlChannelD& resp,
                                  const std::array<Byte, kLineBytes>* release_data) -> bool {
    const Address line_base = req.address & ~(static_cast<Address>(kLineBytes - 1u));
    stats_.release_count++;

    DirectoryEntry dir_entry{};
    if (lookup_dir_entry(line_base, dir_entry)) {
        dir_entry.sharers_mask &= ~(1u << req.hart);
        if (req.opcode == TlOpcodeC::ReleaseData && release_data != nullptr) {
            auto dram_ptr = machine_.memory_.mmu() ? machine_.memory_.mmu()->mmem() : nullptr;
            if (dram_ptr != nullptr && is_dram_access(line_base, kLineBytes)) {
                const Address offset = line_base - simrv::memory::g_dram_base;
                std::memcpy(dram_ptr + offset, release_data->data(), kLineBytes);
                stats_.writeback_count++;
            }
        }
        if (dir_entry.sharers_mask == 0) {
            erase_dir_entry(line_base);
        } else {
            dir_entry.state = CoherenceState::Branch;
            update_dir_entry(line_base, dir_entry);
        }
    }

    resp.opcode = TlOpcodeD::ReleaseAck;
    resp.source = req.source;
    resp.sink = 0;
    resp.error = false;
    return true;
}

void CoherenceHub::process_grant_ack([[maybe_unused]] const TlChannelE& ack) {
    // TL-C 3-way handshake completion
}

}  // namespace simrv::memory
