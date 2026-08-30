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
    return true;
}

auto MmioRouter::unregister_device(TileLinkNode* node) -> bool {
    if (node == nullptr) {
        return false;
    }
    const auto it = std::ranges::find(nodes_, node);
    if (it != nodes_.end()) {
        nodes_.erase(it);
        return true;
    }
    return false;
}

void MmioRouter::clear() { nodes_.clear(); }

auto MmioRouter::resolve_device(Address addr) const -> TileLinkNode* {
    if (nodes_.empty()) {
        return nullptr;
    }

    // Binary search over sorted base addresses
    auto it = std::ranges::upper_bound(nodes_, addr, {},
                                       [](const auto* node) { return node->base_address(); });

    if (it != nodes_.begin()) {
        --it;
        if ((*it)->contains(addr)) {
            return *it;
        }
    }

    // Fallback scan for sparse devices with holes or wider spans
    for (auto* node : nodes_) {
        if (node->contains(addr)) {
            return node;
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
    TileLinkNode* device = resolve_device(req.address);
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
    if (request_bytes - 1 > std::numeric_limits<Address>::max() - req.address ||
        !device->contains(req.address + request_bytes - 1)) {
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
