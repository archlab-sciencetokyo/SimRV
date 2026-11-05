/**
 * @file ExternalDevicePlugin.cpp
 * @brief Dynamic loader and adapter for external MMIO device plugins.
 */
#include "simrv/device/ExternalDevicePlugin.hpp"

#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::device {

ExternalMmioDevice::ExternalMmioDevice(const SimRvExternalDevice* desc, void* ctx, std::string name,
                                       simrv::xlen::Address base, simrv::xlen::Address size)
    : desc_(desc), ctx_(ctx), name_(std::move(name)), base_(base), size_(size) {
    if (desc_ != nullptr && desc_->reset != nullptr) {
        desc_->reset(ctx_);
    }
}

ExternalMmioDevice::~ExternalMmioDevice() {
    if (desc_ != nullptr && desc_->destroy != nullptr) {
        desc_->destroy(ctx_);
        ctx_ = nullptr;
    }
}

auto ExternalMmioDevice::read8(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData8 {
    if (desc_ == nullptr || desc_->read == nullptr) return 0;
    uint64_t val = 0;
    desc_->read(ctx_, offset, 1, &val);
    return static_cast<simrv::memory::MmioData8>(val & 0xffU);
}

auto ExternalMmioDevice::read16(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData16 {
    if (desc_ == nullptr || desc_->read == nullptr) return 0;
    uint64_t val = 0;
    desc_->read(ctx_, offset, 2, &val);
    return static_cast<simrv::memory::MmioData16>(val & 0xffffU);
}

auto ExternalMmioDevice::read32(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData32 {
    if (desc_ == nullptr || desc_->read == nullptr) return 0;
    uint64_t val = 0;
    desc_->read(ctx_, offset, 4, &val);
    return static_cast<simrv::memory::MmioData32>(val & 0xffffffffU);
}

auto ExternalMmioDevice::read64(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData64 {
    if (desc_ == nullptr || desc_->read == nullptr) return 0;
    uint64_t val = 0;
    desc_->read(ctx_, offset, 8, &val);
    return val;
}

void ExternalMmioDevice::write8(simrv::memory::MmioOffset offset, simrv::memory::MmioData8 val) {
    if (desc_ != nullptr && desc_->write != nullptr) {
        desc_->write(ctx_, offset, 1, val);
    }
}

void ExternalMmioDevice::write16(simrv::memory::MmioOffset offset, simrv::memory::MmioData16 val) {
    if (desc_ != nullptr && desc_->write != nullptr) {
        desc_->write(ctx_, offset, 2, val);
    }
}

void ExternalMmioDevice::write32(simrv::memory::MmioOffset offset, simrv::memory::MmioData32 val) {
    if (desc_ != nullptr && desc_->write != nullptr) {
        desc_->write(ctx_, offset, 4, val);
    }
}

void ExternalMmioDevice::write64(simrv::memory::MmioOffset offset, simrv::memory::MmioData64 val) {
    if (desc_ != nullptr && desc_->write != nullptr) {
        desc_->write(ctx_, offset, 8, val);
    }
}

auto resolve_device_plugin_path(std::string_view name_or_path) -> std::optional<std::string> {
    if (name_or_path.empty()) return std::nullopt;

    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::exists(name_or_path, ec) && !fs::is_directory(name_or_path, ec)) {
        return fs::canonical(name_or_path, ec).string();
    }

    std::vector<std::string> candidates;
    const std::string base(name_or_path);
    candidates.push_back(base);
    const std::vector<std::string> exts = {".so", ".dylib"};
    for (const auto& ext : exts) {
        if (!base.ends_with(ext)) {
            candidates.push_back(base + ext);
            candidates.push_back("lib" + base + ext);
            candidates.push_back("libsimrv-" + base + ext);
            candidates.push_back("librvcomp-" + base + ext);
            if (base.starts_with("rvcomp-")) {
                const std::string stripped = base.substr(7);
                candidates.push_back("libsimrv-" + stripped + ext);
                candidates.push_back("lib" + stripped + ext);
            } else if (base.starts_with("cfu-")) {
                const std::string stripped = base.substr(4);
                candidates.push_back("libsimrv-cfu-" + stripped + ext);
                candidates.push_back("libcfu-" + stripped + ext);
                candidates.push_back("libsimrv-" + base + ext);
            } else {
                candidates.push_back("libsimrv-rvcomp-" + base + ext);
                candidates.push_back("librvcomp-" + base + ext);
            }
        }
    }
    if (base.find("cfu-provingground") != std::string::npos ||
        base.find("cfu-pg") != std::string::npos) {
        candidates.push_back("libsimrv-cfu-provingground.so");
        candidates.push_back("libcfu-provingground.so");
        candidates.push_back("libcfu-pg.so");
    }
    if (base.find("mmc") != std::string::npos || base.find("test") != std::string::npos) {
        candidates.push_back("libtest-device-plugin.so");
        candidates.push_back("libtest-device-plugin.dylib");
    }

    std::vector<fs::path> dirs;
    const fs::path cwd = fs::current_path(ec);
    dirs.push_back(cwd / "plugins");
    dirs.push_back(cwd);

    if (fs::exists("/proc/self/exe", ec)) {
        const auto exe_path = fs::read_symlink("/proc/self/exe", ec);
        const auto exe_dir = exe_path.parent_path();
        dirs.push_back(exe_dir / "plugins");
        dirs.push_back(exe_dir);
        dirs.push_back(exe_dir.parent_path() / "plugins");
        dirs.push_back(exe_dir.parent_path() / "lib" / "simrv" / "plugins");
        dirs.push_back(exe_dir.parent_path() / "lib64" / "simrv" / "plugins");
    }

    dirs.push_back(cwd / "build/rv64-release/plugins");
    dirs.push_back(cwd / "build/rv64-release");
    dirs.push_back(cwd / "build/rv32-release/plugins");
    dirs.push_back(cwd / "build/rv32-release");

    if (const char* env_path = std::getenv("SIMRV_PLUGIN_PATH");
        env_path != nullptr && *env_path != '\0') {
        std::string_view sv(env_path);
        size_t start = 0;
        while (start < sv.size()) {
            size_t end = sv.find(':', start);
            if (end == std::string_view::npos) end = sv.size();
            dirs.emplace_back(std::string(sv.substr(start, end - start)));
            start = end + 1;
        }
    }

    dirs.emplace_back("/usr/local/lib/simrv/plugins");
    dirs.emplace_back("/usr/lib/simrv/plugins");

    for (const auto& dir : dirs) {
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) continue;
        for (const auto& candidate : candidates) {
            const auto target = dir / candidate;
            if (fs::exists(target, ec) && !fs::is_directory(target, ec)) {
                return fs::canonical(target, ec).string();
            }
        }
    }

    return std::nullopt;
}

auto load_external_device_plugin(std::string_view path_and_args, PlatformDeviceRegistry& registry)
    -> bool {
    std::string raw_target;
    std::string args;
    if (const auto eq = path_and_args.find('='); eq != std::string_view::npos) {
        raw_target = std::string(path_and_args.substr(0, eq));
        args = std::string(path_and_args.substr(eq + 1));
    } else {
        raw_target = std::string(path_and_args);
    }

    const auto resolved = resolve_device_plugin_path(raw_target);
    const std::string path = resolved ? *resolved : raw_target;

    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        simrv::log::error("Failed to load external device plugin '{}': {}", raw_target, dlerror());
        return false;
    }

    auto init_fn =
        reinterpret_cast<SimRvDevicePluginInitFn>(dlsym(handle, "simrv_device_plugin_init"));
    if (init_fn == nullptr) {
        simrv::log::error("Plugin '{}' missing entry point 'simrv_device_plugin_init'", path);
        dlclose(handle);
        return false;
    }

    const SimRvExternalDevice* dev = init_fn();
    if (dev == nullptr || dev->read == nullptr || dev->write == nullptr) {
        simrv::log::error("Plugin '{}' returned invalid device descriptor", path);
        dlclose(handle);
        return false;
    }

    if (dev->api_version != SIMRV_EXTERNAL_DEVICE_API_VERSION) {
        simrv::log::error("Plugin '{}' has incompatible API version {} (expected {})", path,
                          dev->api_version, SIMRV_EXTERNAL_DEVICE_API_VERSION);
        dlclose(handle);
        return false;
    }

    const std::string compatible = dev->compatible != nullptr ? dev->compatible : "";
    const std::string name = dev->name != nullptr ? dev->name : "external-device";
    simrv::log::info("Loaded external device plugin '{}' ({}, compatible: '{}')", path, name,
                     compatible);

    auto factory = [dev, args = std::move(args), name](simrv::core::Machine& machine,
                                                       const simrv::core::SoCDeviceConfig& config)
        -> std::unique_ptr<simrv::memory::MmioDevice> {
        void* ctx = nullptr;
        if (dev->init != nullptr) {
            if (dev->init(&ctx, args.c_str()) != 0) {
                return nullptr;
            }
        }
        if (dev->set_host_services != nullptr) {
            static const SimRvHostServices services = {
                .host_ctx = &machine,
                .set_irq =
                    [](void* m, uint32_t irq, int level) {
                        if (m != nullptr) {
                            static_cast<simrv::core::Machine*>(m)->set_platform_irq(irq,
                                                                                    level != 0);
                        }
                    },
                .request_reboot =
                    [](void* m) {
                        if (m != nullptr) {
                            static_cast<simrv::core::Machine*>(m)->request_reboot();
                        }
                    },
                .request_poweroff =
                    [](void* m) {
                        if (m != nullptr) {
                            static_cast<simrv::core::Machine*>(m)->request_exit(0);
                        }
                    },
            };
            dev->set_host_services(ctx, &services);
        }
        const auto base = config.base != 0 ? config.base : dev->default_base;
        const auto size = config.size != 0 ? config.size : dev->default_size;
        const auto dev_name = !config.name.empty() ? config.name : name;
        return std::make_unique<ExternalMmioDevice>(dev, ctx, dev_name, base, size);
    };

    if (!compatible.empty()) {
        registry.register_mmio_compatible(compatible, factory);
    }
    registry.register_mmio_device(simrv::core::SoCDeviceKind::CustomMmio, factory);
    return true;
}

auto autodiscover_external_device_plugin(std::string_view compatible, std::string_view default_args,
                                         PlatformDeviceRegistry& registry) -> bool {
    if (compatible.empty()) return false;

    std::vector<std::string_view> probes;
    probes.push_back(compatible);
    if (const auto comma = compatible.find(','); comma != std::string_view::npos) {
        const auto suffix = compatible.substr(comma + 1);
        probes.push_back(suffix);
        if (suffix.starts_with("rvcomp-")) {
            probes.push_back(suffix.substr(7));
        }
    }

    for (const auto& probe : probes) {
        if (const auto resolved = resolve_device_plugin_path(probe)) {
            std::string spec = *resolved;
            if (!default_args.empty()) {
                spec += "=";
                spec += default_args;
            }
            if (load_external_device_plugin(spec, registry)) {
                simrv::log::info("Auto-discovered external device plugin for compatible '{}'",
                                 compatible);
                return true;
            }
        }
    }
    return false;
}

}  // namespace simrv::device
