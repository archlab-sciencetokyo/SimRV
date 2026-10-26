/** @file SoCManifest.cpp */
#include "simrv/util/SoCManifest.hpp"

#include <format>
#include <fstream>
#include <sstream>

#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/SoCDeviceRegistry.hpp"

namespace simrv::util {

namespace {

void write_quoted(std::ostream& out, std::string_view value);

auto quoted(std::string_view value) -> std::string {
    std::ostringstream result;
    write_quoted(result, value);
    return result.str();
}

void write_quoted(std::ostream& out, std::string_view value) {
    constexpr std::string_view hex = "0123456789abcdef";
    out.put('"');
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (ch < 0x20) {
                    out << "\\u00" << hex.at(ch >> 4) << hex.at(ch & 0x0f);
                } else {
                    out.put(static_cast<char>(ch));
                }
                break;
        }
    }
    out.put('"');
}

auto platform_name(const simrv::core::SoCConfig& config) -> std::string_view {
    if (config.enable_mmio) return "mmio";
    if (config.enable_pcie) return "pcie";
    return "none";
}

auto mode_name(simrv::core::SmpMode mode) -> std::string_view {
    return mode == simrv::core::SmpMode::SingleThreaded ? "single-threaded" : "multi-threaded";
}

void write_cache(std::ostream& out, const simrv::core::SharedCacheConfig& cache) {
    out << "{\"enabled\": " << (cache.enabled ? "true" : "false")
        << ", \"capacity_bytes\": " << cache.capacity_bytes
        << ", \"associativity\": " << cache.associativity
        << ", \"line_bytes\": " << cache.line_bytes << ", \"hit_latency\": " << cache.hit_latency
        << ", \"miss_latency\": " << cache.miss_latency << "}";
}

auto transport_name(const simrv::core::SoCConfig& config, simrv::core::SoCDeviceKind kind)
    -> std::string_view {
    switch (kind) {
        case simrv::core::SoCDeviceKind::VirtioMmioBlock:
        case simrv::core::SoCDeviceKind::VirtioMmioConsole:
        case simrv::core::SoCDeviceKind::VirtioMmioRng:
        case simrv::core::SoCDeviceKind::VirtioMmioGpu:
        case simrv::core::SoCDeviceKind::VirtioMmioInput:
        case simrv::core::SoCDeviceKind::VirtioMmioSound:
        case simrv::core::SoCDeviceKind::VirtioMmioNet:
        case simrv::core::SoCDeviceKind::VirtioMmioFs:
            return config.enable_pcie ? "pci" : "mmio";
        default:
            return "mmio";
    }
}

}  // namespace

auto serialize_soc_manifest(const simrv::core::SoCConfig& config, std::ostream& out) -> bool {
    const auto devices = simrv::core::SoCDeviceRegistry::resolve(config);
    simrv::pipeline::CpuModelConfig cpu{};
    if (const auto cpu_path = simrv::core::resolve_cpu_model_path(config.cpu_model)) {
        (void)simrv::core::parse_cpu_config(*cpu_path, cpu);
    }
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"manifest_version\": \"3.0\",\n";
    out << "  \"generator\": " << quoted(std::format("simrv {}", simrv::buildinfo::kVersion))
        << ",\n";
    out << "  \"name\": " << quoted(config.name) << ",\n";
    out << "  \"cpu_model\": " << quoted(config.cpu_model) << ",\n";
    out << "  \"smp\": {\n";
    out << "    \"hart_count\": " << config.smp.hart_count << ",\n";
    out << "    \"mode\": " << quoted(mode_name(config.smp.mode)) << ",\n";
    out << "    \"synchronization_quantum\": " << config.smp.synchronization_quantum << ",\n";
    out << "    \"l3\": ";
    write_cache(out, config.smp.l3);
    out << ",\n    \"clusters\": [";
    for (size_t i = 0; i < config.smp.clusters.size(); ++i) {
        const auto& cluster = config.smp.clusters[i];
        if (i != 0) out << ", ";
        out << "{\"id\": " << cluster.id << ", \"harts\": [";
        for (size_t h = 0; h < cluster.harts.size(); ++h) {
            if (h != 0) out << ", ";
            out << cluster.harts[h];
        }
        out << "], \"l2\": ";
        write_cache(out, cluster.l2);
        out << "}";
    }
    out << "]\n  },\n";
    out << "  \"cpu\": {\n";
    out << "    \"name\": " << quoted(cpu.name) << ",\n";
    out << "    \"xlen\": " << static_cast<unsigned int>(cpu.supported_xlen) << ",\n";
    out << "    \"misa\": " << quoted(simrv::core::detail::isa_preset_name(cpu.isa_preset))
        << ",\n";
    out << "    \"extensions\": [";
    for (size_t i = 0; i < cpu.isa_extensions.size(); ++i) {
        if (i != 0) out << ", ";
        out << quoted(cpu.isa_extensions[i]);
    }
    out << "],\n";
    out << "    \"cfu\": {\n";
    out << "      \"enabled\": " << (cpu.cfu.enabled ? "true" : "false") << ",\n";
    out << "      \"opcode\": " << quoted(cpu.cfu.opcode) << ",\n";
    out << "      \"default_latency\": " << cpu.cfu.default_latency << ",\n";
    out << "      \"rtl_module\": " << quoted(cpu.cfu.rtl_module) << ",\n";
    out << "      \"interface\": " << quoted(cpu.cfu.interface) << "\n";
    out << "    }\n";
    out << "  },\n";
    out << "  \"platform\": " << quoted(platform_name(config)) << ",\n";
    out << "  \"device_policy\": "
        << quoted(config.disable_unlisted_devices ? "explicit" : "implicit") << ",\n";
    out << "  \"memory\": {\n";
    out << "    \"dram_base\": " << (config.dram_base.value_or(0)) << ",\n";
    out << "    \"dram_size\": " << (config.dram_size.value_or(0)) << "\n";
    out << "  },\n";
    out << "  \"boot\": {\n";
    out << "    \"reset_pc\": " << (config.reset_pc.value_or(0)) << ",\n";
    out << "    \"tohost\": " << (config.tohost.value_or(0)) << "\n";
    out << "  },\n";
    out << "  \"devices\": [\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        out << "    {\n";
        out << "      \"kind\": " << quoted(simrv::core::soc_device_kind_name(device.kind))
            << ",\n";
        out << "      \"name\": ";
        write_quoted(out, device.name);
        out << ",\n";
        out << "      \"transport\": " << quoted(transport_name(config, device.kind)) << ",\n";
        out << "      \"base\": " << device.base << ",\n";
        out << "      \"size\": " << device.size << ",\n";
        out << "      \"irq\": " << device.irq << ",\n";
        if (device.kind == simrv::core::SoCDeviceKind::DummyMmio ||
            device.kind == simrv::core::SoCDeviceKind::CustomMmio) {
            out << "      \"compatible\": " << quoted(device.compatible) << ",\n";
            out << "      \"read_value\": " << device.read_value << ",\n";
        }
        if (device.kind == simrv::core::SoCDeviceKind::RingBufferEthernet) {
            out << "      \"mac_address\": "
                << quoted(std::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                                      device.mac_address[0], device.mac_address[1],
                                      device.mac_address[2], device.mac_address[3],
                                      device.mac_address[4], device.mac_address[5]))
                << ",\n";
        }
        if (!device.regions.empty()) {
            out << "      \"regions\": [";
            for (size_t region_index = 0; region_index < device.regions.size(); ++region_index) {
                const auto& region = device.regions[region_index];
                if (region_index != 0) out << ", ";
                out << "{\"name\": " << quoted(region.name) << ", \"base\": " << region.base
                    << ", \"size\": " << region.size << "}";
            }
            out << "],\n";
        }
        out << "      \"enabled\": " << (device.enabled ? "true" : "false") << "\n";
        out << "    }" << (i + 1 == devices.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return static_cast<bool>(out);
}

auto save_soc_manifest(const std::string& path, const simrv::core::SoCConfig& config) -> bool {
    std::ofstream out(path);
    return out.is_open() && serialize_soc_manifest(config, out);
}

}  // namespace simrv::util
