/**
 * @file MemoryTopologySpec.hpp
 * @brief Compile-time NTTP descriptor for memory fast-path specialization.
 */
#pragma once

namespace simrv::memory {

/**
 * @struct MemoryTopologySpec
 * @brief Non-type template parameter capturing active memory protection and
 *        translation features for compile-time fast-path specialization.
 */
struct MemoryTopologySpec {
    bool has_mmu = true;
    bool has_pmp = true;
    bool has_coherence = true;
    bool strict_alignment = false;
};

inline constexpr MemoryTopologySpec kCoherentMmuMemory{true, true, true, false};
inline constexpr MemoryTopologySpec kSingleHartMmuMemory{true, true, false, false};
inline constexpr MemoryTopologySpec kDirectPmpMemory{false, true, false, false};
inline constexpr MemoryTopologySpec kDirectMemory{false, false, false, false};

template <typename Fn>
[[nodiscard]] inline decltype(auto) dispatch_memory_topology(bool has_mmu, bool has_pmp,
                                                             bool has_coherence, Fn&& fn) {
    if (!has_mmu) {
        if (!has_pmp) {
            return fn.template operator()<kDirectMemory>();
        }
        return fn.template operator()<kDirectPmpMemory>();
    }
    if (!has_coherence) {
        return fn.template operator()<kSingleHartMmuMemory>();
    }
    return fn.template operator()<kCoherentMmuMemory>();
}

}  // namespace simrv::memory
