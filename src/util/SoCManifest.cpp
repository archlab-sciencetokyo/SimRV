/** @file SoCManifest.cpp */
#include "simrv/util/SoCManifest.hpp"

#include <format>
#include <fstream>

#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/SoCDeviceRegistry.hpp"

namespace simrv::util {

namespace {

auto json_escape(std::string_view value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

auto quoted(std::string_view value) -> std::string {
    return std::format("\"{}\"", json_escape(value));
}

auto platform_name(const simrv::core::SoCConfig& config) -> std::string_view {
    if (config.enable_mmio) return "mmio";
    if (config.enable_pcie) return "pcie";
    return "none";
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
    out << "  \"generator\": \"simrv "
        << json_escape(simrv::buildinfo::kVersion) << "\",\n";
    out << "  \"name\": " << quoted(config.name) << ",\n";
    out << "  \"cpu_model\": " << quoted(config.cpu_model) << ",\n";
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
        out << "      \"name\": " << quoted(device.name) << ",\n";
        out << "      \"transport\": " << quoted(transport_name(config, device.kind)) << ",\n";
        out << "      \"base\": " << device.base << ",\n";
        out << "      \"size\": " << device.size << ",\n";
        out << "      \"irq\": " << device.irq << ",\n";
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
