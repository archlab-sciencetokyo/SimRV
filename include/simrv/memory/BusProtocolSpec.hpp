/**
 * @file BusProtocolSpec.hpp
 * @brief Structural NTTP bus protocol specification and compile-time catalog.
 */
#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string_view>

namespace simrv::memory {

enum class BusProtocol : uint8_t {
    TileLinkUL,
    TileLinkUH,
    TileLinkC,
    Axi4,
    Axi4Lite,
};

struct BusProtocolSpec {
    BusProtocol protocol{BusProtocol::TileLinkC};
    uint32_t beat_bytes{8};
    uint32_t max_outstanding{16};
    uint8_t id_width_bits{4};
    uint16_t max_burst_beats{256};
    bool supports_burst{true};
    bool supports_atomics{true};
    bool supports_coherence{true};
    bool separate_write_response{false};

    [[nodiscard]] constexpr auto is_axi() const noexcept -> bool {
        return protocol == BusProtocol::Axi4 || protocol == BusProtocol::Axi4Lite;
    }

    [[nodiscard]] constexpr auto is_tilelink() const noexcept -> bool { return !is_axi(); }

    friend constexpr auto operator==(const BusProtocolSpec&, const BusProtocolSpec&)
        -> bool = default;
};

struct BusCatalogEntry {
    BusProtocol protocol;
    BusProtocolSpec spec;
    std::string_view cli_name;
    std::string_view display_name;
};

inline constexpr BusProtocolSpec kDefaultTileLinkCSpec{
    .protocol = BusProtocol::TileLinkC,
    .beat_bytes = 8,
    .max_outstanding = 16,
    .id_width_bits = 4,
    .max_burst_beats = 256,
    .supports_burst = true,
    .supports_atomics = true,
    .supports_coherence = true,
    .separate_write_response = false,
};

inline constexpr BusProtocolSpec kDefaultTileLinkUHSpec{
    .protocol = BusProtocol::TileLinkUH,
    .beat_bytes = 8,
    .max_outstanding = 16,
    .id_width_bits = 4,
    .max_burst_beats = 256,
    .supports_burst = true,
    .supports_atomics = true,
    .supports_coherence = false,
    .separate_write_response = false,
};

inline constexpr BusProtocolSpec kDefaultTileLinkULSpec{
    .protocol = BusProtocol::TileLinkUL,
    .beat_bytes = 4,
    .max_outstanding = 8,
    .id_width_bits = 4,
    .max_burst_beats = 1,
    .supports_burst = false,
    .supports_atomics = false,
    .supports_coherence = false,
    .separate_write_response = false,
};

inline constexpr BusProtocolSpec kDefaultAxi4Spec{
    .protocol = BusProtocol::Axi4,
    .beat_bytes = 8,
    .max_outstanding = 16,
    .id_width_bits = 4,
    .max_burst_beats = 256,
    .supports_burst = true,
    .supports_atomics = false,
    .supports_coherence = false,
    .separate_write_response = true,
};

inline constexpr BusProtocolSpec kDefaultAxi4LiteSpec{
    .protocol = BusProtocol::Axi4Lite,
    .beat_bytes = 4,
    .max_outstanding = 4,
    .id_width_bits = 0,
    .max_burst_beats = 1,
    .supports_burst = false,
    .supports_atomics = false,
    .supports_coherence = false,
    .separate_write_response = true,
};

inline constexpr std::array<BusCatalogEntry, 5> kBusCatalog = {{
    {BusProtocol::TileLinkC, kDefaultTileLinkCSpec, "tilelink-c", "TileLink-C (Coherent SMP)"},
    {BusProtocol::TileLinkUH, kDefaultTileLinkUHSpec, "tilelink-uh",
     "TileLink-UH (Uncached Heavy)"},
    {BusProtocol::TileLinkUL, kDefaultTileLinkULSpec, "tilelink-ul",
     "TileLink-UL (Uncached Lightweight)"},
    {BusProtocol::Axi4, kDefaultAxi4Spec, "axi4", "AMBA AXI4"},
    {BusProtocol::Axi4Lite, kDefaultAxi4LiteSpec, "axi4-lite", "AMBA AXI4-Lite"},
}};

[[nodiscard]] constexpr auto find_bus_catalog_entry(BusProtocol protocol) noexcept
    -> const BusCatalogEntry* {
    for (const auto& entry : kBusCatalog) {
        if (entry.protocol == protocol) return &entry;
    }
    return nullptr;
}

[[nodiscard]] inline auto find_bus_catalog_entry(std::string_view name) noexcept
    -> const BusCatalogEntry* {
    auto ieq = [](std::string_view a, std::string_view b) -> bool {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    };
    if (ieq(name, "tilelink") || ieq(name, "tl-c") || ieq(name, "tlc")) {
        return find_bus_catalog_entry(BusProtocol::TileLinkC);
    }
    if (ieq(name, "tl-uh") || ieq(name, "tluh")) {
        return find_bus_catalog_entry(BusProtocol::TileLinkUH);
    }
    if (ieq(name, "tl-ul") || ieq(name, "tlul")) {
        return find_bus_catalog_entry(BusProtocol::TileLinkUL);
    }
    if (ieq(name, "axi") || ieq(name, "axi4-full")) {
        return find_bus_catalog_entry(BusProtocol::Axi4);
    }
    if (ieq(name, "axi4lite") || ieq(name, "axi-lite")) {
        return find_bus_catalog_entry(BusProtocol::Axi4Lite);
    }
    for (const auto& entry : kBusCatalog) {
        if (ieq(entry.cli_name, name)) return &entry;
    }
    return nullptr;
}

[[nodiscard]] constexpr auto bus_protocol_name(BusProtocol protocol) noexcept -> std::string_view {
    if (const auto* entry = find_bus_catalog_entry(protocol)) {
        return entry->cli_name;
    }
    return "tilelink-c";
}

[[nodiscard]] constexpr auto bus_protocol_display_name(BusProtocol protocol) noexcept
    -> std::string_view {
    if (const auto* entry = find_bus_catalog_entry(protocol)) {
        return entry->display_name;
    }
    return "TileLink-C (Coherent SMP)";
}

}  // namespace simrv::memory
