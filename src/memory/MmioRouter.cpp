/**
 * @file MmioRouter.cpp
 * @brief Memory-mapped I/O address router implementation.
 */
#include "simrv/memory/MmioRouter.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

#include "simrv/core/Logger.hpp"

namespace simrv::memory {

auto MmioRouter::register_device(TileLinkNode* node) -> bool {
    if (node == nullptr) {
        return false;
    }

    const Address base = node->base_address();
    if (node->size() == 0 || node->size() > std::numeric_limits<Address>::max() - base) {
        simrv::log::warn("Rejecting invalid MMIO range for '{}'", node->name());
        return false;
    }
    const Address end = base + node->size();

    // Check for range overlaps with existing registered devices
    for (const auto* existing : nodes_) {
        if (existing == node) {
            return true;  // Already registered
        }
        const Address ex_base = existing->base_address();
        if (existing->size() > std::numeric_limits<Address>::max() - ex_base) return false;
        const Address ex_end = ex_base + existing->size();
        const Address overlap_begin = std::max(base, ex_base);
        const Address overlap_end = std::min(end, ex_end);
        if (overlap_begin < overlap_end) {
            bool collision = false;
            for (Address address = overlap_begin; address < overlap_end; ++address) {
                if (node->contains(address) && existing->contains(address)) {
                    collision = true;
                    break;
                }
            }
            if (collision) {
                simrv::log::warn(
                    "MMIO address range collision detected: '{}' [{:#x}, {:#x}) conflicts with "
                    "'{}' "
                    "[{:#x}, {:#x})",
                    node->name(), static_cast<unsigned long long>(base),
                    static_cast<unsigned long long>(end), existing->name(),
                    static_cast<unsigned long long>(ex_base),
                    static_cast<unsigned long long>(ex_end));
                return false;
            }
        }
    }

    nodes_.push_back(node);
    std::ranges::sort(
        nodes_, [](const auto* a, const auto* b) { return a->base_address() < b->base_address(); });
    rebuild_entries();
    return true;
}

auto MmioRouter::unregister_device(TileLinkNode* node) -> bool {
    if (node == nullptr) {
        return false;
    }
    const auto it = std::ranges::find(nodes_, node);
    if (it != nodes_.end()) {
        nodes_.erase(it);
        rebuild_entries();
        return true;
    }
    return false;
}

void MmioRouter::clear() {
    nodes_.clear();
    rebuild_entries();
}

void MmioRouter::rebuild_entries() {
    entries_.clear();
    entries_.reserve(nodes_.size());
    min_base_ = std::numeric_limits<Address>::max();
    max_end_ = 0;
    mru_count_ = 0;
    for (auto* node : nodes_) {
        if (node == nullptr) continue;
        const Address base = node->base_address();
        const Address end = base + node->size();
        entries_.push_back(DeviceEntry{.base = base, .end = end, .node = node});
        min_base_ = std::min(min_base_, base);
        max_end_ = std::max(max_end_, end);
    }
    std::ranges::sort(entries_, [](const auto& a, const auto& b) { return a.base < b.base; });
}

auto MmioRouter::resolve_device(Address addr) const -> TileLinkNode* {
    if (entries_.empty() || addr < min_base_ || addr >= max_end_) {
        return nullptr;
    }

    // Check 4-entry MRU cache (covers high-frequency alternating UART, CLINT, PLIC, VirtIO)
    for (size_t i = 0; i < mru_count_; ++i) {
        const auto& entry = mru_entries_[i];
        if (addr >= entry.base && addr < entry.end && entry.node->contains(addr)) {
            if (i > 0) {
                const auto hit = entry;
                for (size_t j = i; j > 0; --j) {
                    mru_entries_[j] = mru_entries_[j - 1];
                }
                mru_entries_[0] = hit;
            }
            return mru_entries_[0].node;
        }
    }

    // Binary search over sorted base addresses (scalar comparisons, zero virtual calls)
    auto it = std::ranges::upper_bound(entries_, addr, {}, &DeviceEntry::base);

    while (it != entries_.begin()) {
        --it;
        if (addr < it->end && it->node->contains(addr)) {
            const size_t shift_limit = std::min(mru_count_, kMruCapacity - 1);
            for (size_t j = shift_limit; j > 0; --j) {
                mru_entries_[j] = mru_entries_[j - 1];
            }
            mru_entries_[0] = *it;
            if (mru_count_ < kMruCapacity) {
                ++mru_count_;
            }
            return it->node;
        }
        if (addr >= it->end) {
            // For nested devices with subrange holes, continue checking enclosing entries
            continue;
        }
    }

    return nullptr;
}

auto MmioRouter::find_by_name(std::string_view name) const -> TileLinkNode* {
    for (auto* node : nodes_) {
        if (node->name() != nullptr && std::string_view(node->name()) == name) {
            return node;
        }
    }
    return nullptr;
}

auto MmioRouter::route_request(const TlChannelA& req, TlChannelD& resp) -> bool {
    TileLinkNode* device = resolve_device(req.address.raw());
    if (device == nullptr) {
        resp.denied = true;
        ++bus_error_count_;
        return false;
    }

    const bool is_read = (req.opcode == TlOpcodeA::Get);
    const bool is_write =
        (req.opcode == TlOpcodeA::PutFullData || req.opcode == TlOpcodeA::PutPartialData);

    if (!is_read && !is_write) {
        resp.denied = true;
        ++bus_error_count_;
        return true;
    }

    const Address request_bytes = static_cast<Address>(1u << (req.size & 0x3u));
    if (request_bytes - 1 > std::numeric_limits<Address>::max() - req.address.raw() ||
        !device->contains((req.address + request_bytes - 1).raw())) {
        resp.denied = true;
        ++bus_error_count_;
        return true;
    }

    if (is_write && device->is_read_only()) {
        resp.denied = true;
        ++bus_error_count_;
        return true;
    }

    // Alignment validation against device requirements
    const Address align = device->alignment();
    if (align > 1 && (req.address % align) != 0) {
        resp.denied = true;
        ++bus_error_count_;
        return true;
    }

    const bool handled = device->handle_request(req, resp);
    if (handled) {
        if (is_read) {
            ++mmio_read_count_;
        } else if (is_write) {
            ++mmio_write_count_;
        }
        if (resp.failed()) {
            ++bus_error_count_;
        }
    } else {
        resp.denied = true;
        ++bus_error_count_;
    }

    return true;
}

}  // namespace simrv::memory
