/**
 * @file ExternalDevicePlugin.cpp
 * @brief Dynamic loader and adapter for external MMIO device plugins.
 */
#include "simrv/device/ExternalDevicePlugin.hpp"

#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>
#include <utility>
#include <vector>

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/ExternalFramebufferDevice.h"
#include "simrv/device/ExternalInputDevice.h"
#include "simrv/device/ExternalSerialDevice.h"
#include "simrv/device/FramebufferDevice.hpp"
#include "simrv/device/InputDevice.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {

namespace {
class ExternalSerialAdapter final : public Uart {
   public:
    ExternalSerialAdapter(simrv::core::Machine& machine, const simrv::core::SoCDeviceConfig& config,
                          const SimRvExternalDevice* dev, const SimRvExternalSerialDevice* serial,
                          void* ctx)
        : Uart(machine, simrv::xlen::PhysAddr{config.base.raw()}, config.size, config.irq,
               config.clock_hz, machine.execution_config().cpu_clock_hz),
          serial_(serial),
          ctx_(ctx),
          services_{.host_ctx = this,
                    .irq_number = config.irq,
                    .write_byte =
                        [](void* host, uint8_t byte) {
                            static_cast<ExternalSerialAdapter*>(host)->write_host_byte(byte);
                        }},
          mmio_(dev, ctx, config.name, simrv::xlen::PhysAddr{config.base}, config.size, machine,
                true) {
        if (serial_->set_serial_host_services) serial_->set_serial_host_services(ctx_, &services_);
        mmio_.reset();
    }
    ~ExternalSerialAdapter() override {
        stop_input_thread();
        stop_pty();
    }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override {
        return mmio_.handle_request(req, resp);
    }
    void reset() override { mmio_.reset(); }
    void push_rx_byte(uint8_t byte) override { serial_->receive_byte(ctx_, byte); }
    void advance_cycles(uint64_t cycles) override {
        if (cycles == 0) return;
        mmio_.advance_cycles(cycles);
        if (serial_->advance_cycles) serial_->advance_cycles(ctx_, cycles);
        flush_tx_buffer();
        service_interrupts();
    }
    [[nodiscard]] auto requires_instruction_ticks() const noexcept -> bool override {
        return mmio_.has_cycle_ticking();
    }
    void service_interrupts() override {
        if (serial_->service_interrupts) serial_->service_interrupts(ctx_);
    }
    [[nodiscard]] auto has_active_transfer() const noexcept -> bool override {
        return serial_->has_active_transfer && serial_->has_active_transfer(ctx_) != 0;
    }
    [[nodiscard]] auto is_interrupt_pending() const -> bool override {
        return serial_->is_interrupt_pending && serial_->is_interrupt_pending(ctx_) != 0;
    }

   private:
    const SimRvExternalSerialDevice* serial_;
    void* ctx_;
    SimRvSerialHostServices services_;
    ExternalMmioDevice mmio_;
};

class ExternalFramebufferAdapter : public ExternalMmioDevice, public FramebufferDevice {
   public:
    ExternalFramebufferAdapter(const SimRvExternalDevice* dev,
                               const SimRvExternalFramebufferDevice* fb, void* ctx,
                               std::string name, simrv::xlen::PhysAddr base,
                               simrv::xlen::Address size, simrv::core::Machine& machine)
        : ExternalMmioDevice(dev, ctx, std::move(name), base, size, machine), fb_(fb), ctx_(ctx) {}

    [[nodiscard]] auto framebuffer_geometry() const noexcept
        -> simrv::core::FramebufferConfig override {
        simrv::core::FramebufferConfig cfg{};
        if (!fb_ || !fb_->get_info) return cfg;
        SimRvFramebufferInfo info{};
        if (fb_->get_info(ctx_, &info) != 0) return cfg;
        cfg.width = info.width;
        cfg.height = info.height;
        cfg.stride = info.stride;
        cfg.bpp = info.bpp;
        cfg.format = (info.format == SIMRV_FB_FORMAT_R5G6B5)
                         ? simrv::core::FramebufferFormat::R5G6B5
                         : simrv::core::FramebufferFormat::A8R8G8B8;
        return cfg;
    }

    [[nodiscard]] auto framebuffer_data() const noexcept -> std::span<const uint8_t> override {
        if (!fb_ || !fb_->get_buffer) return {};
        size_t size = 0;
        const void* ptr = fb_->get_buffer(ctx_, &size);
        if (!ptr || size == 0) return {};
        return {static_cast<const uint8_t*>(ptr), size};
    }

    [[nodiscard]] auto frame_id() const noexcept -> uint64_t override {
        return (fb_ && fb_->get_frame_id) ? fb_->get_frame_id(ctx_) : 0;
    }

   private:
    const SimRvExternalFramebufferDevice* fb_;
    void* ctx_;
};

class ExternalFramebufferInputAdapter final : public ExternalFramebufferAdapter,
                                              public InputDevice {
   public:
    ExternalFramebufferInputAdapter(const SimRvExternalDevice* dev,
                                    const SimRvExternalFramebufferDevice* framebuffer,
                                    const SimRvExternalInputDevice* input, void* ctx,
                                    std::string name, simrv::xlen::PhysAddr base,
                                    simrv::xlen::Address size, simrv::core::Machine& machine)
        : ExternalFramebufferAdapter(dev, framebuffer, ctx, std::move(name), base, size, machine),
          input_(input),
          ctx_(ctx) {}

    void send_key(uint16_t code, bool pressed) override {
        input_->send_key(ctx_, code, pressed ? 1 : 0);
    }

   private:
    const SimRvExternalInputDevice* input_;
    void* ctx_;
};

class ExternalInputAdapter final : public ExternalMmioDevice, public InputDevice {
   public:
    ExternalInputAdapter(const SimRvExternalDevice* dev, const SimRvExternalInputDevice* input,
                         void* ctx, std::string name, simrv::xlen::PhysAddr base,
                         simrv::xlen::Address size, simrv::core::Machine& machine)
        : ExternalMmioDevice(dev, ctx, std::move(name), base, size, machine),
          input_(input),
          ctx_(ctx) {}

    void send_key(uint16_t code, bool pressed) override {
        if (input_ && input_->send_key) {
            input_->send_key(ctx_, code, pressed ? 1 : 0);
        }
    }

   private:
    const SimRvExternalInputDevice* input_;
    void* ctx_;
};
}  // namespace

ExternalMmioDevice::ExternalMmioDevice(const SimRvExternalDevice* desc, void* ctx, std::string name,
                                       simrv::xlen::PhysAddr base, simrv::xlen::Address size,
                                       simrv::core::Machine& machine, bool defer_reset)
    : memory::MmioDevice(&machine),
      desc_(desc),
      ctx_(ctx),
      name_(std::move(name)),
      base_(base),
      size_(size) {
    host_services_ = {
        .host_ctx = &machine,
        .set_irq =
            [](void* m, uint32_t irq, int level) {
                static_cast<simrv::core::Machine*>(m)->set_platform_irq(irq, level != 0);
            },
        .request_reboot = [](void* m) { static_cast<simrv::core::Machine*>(m)->request_reboot(); },
        .request_poweroff = [](void* m) { static_cast<simrv::core::Machine*>(m)->request_exit(0); },
        .dma_read = [](void* m, uint64_t paddr, void* dst, size_t len) -> int {
            auto* mach = static_cast<simrv::core::Machine*>(m);
            if (!mach || !dst || len == 0 || !std::in_range<simrv::xlen::Address>(paddr)) return -1;
            const auto phys = simrv::xlen::PhysAddr{static_cast<simrv::xlen::Address>(paddr)};
            return mach->memory().dma_read(phys, dst, len) ? 0 : -1;
        },
        .dma_write = [](void* m, uint64_t paddr, const void* src, size_t len) -> int {
            auto* mach = static_cast<simrv::core::Machine*>(m);
            if (!mach || !src || len == 0 || !std::in_range<simrv::xlen::Address>(paddr)) return -1;
            const auto phys = simrv::xlen::PhysAddr{static_cast<simrv::xlen::Address>(paddr)};
            return mach->memory().dma_write(phys, src, len) ? 0 : -1;
        },
        .get_cycle_count = [](void* m) -> uint64_t {
            auto* mach = static_cast<simrv::core::Machine*>(m);
            return mach ? mach->memory().system_bus().cycle() : 0ULL;
        },
        .log_msg =
            [](void* /*m*/, int level, const char* msg) {
                if (!msg) return;
                switch (static_cast<SimRvLogLevel>(level)) {
                    case SIMRV_LOG_TRACE:
                    case SIMRV_LOG_DEBUG:
                        simrv::log::debug("{}", msg);
                        break;
                    case SIMRV_LOG_INFO:
                        simrv::log::info("{}", msg);
                        break;
                    case SIMRV_LOG_WARN:
                        simrv::log::warn("{}", msg);
                        break;
                    case SIMRV_LOG_ERROR:
                    default:
                        simrv::log::error("{}", msg);
                        break;
                }
            },
    };
    if (desc_ != nullptr && desc_->set_host_services != nullptr) {
        desc_->set_host_services(ctx_, &host_services_);
    }
    if (!defer_reset) reset();
}

void ExternalMmioDevice::reset() {
    if (desc_ != nullptr && desc_->reset != nullptr) desc_->reset(ctx_);
}

void ExternalMmioDevice::advance_cycles(uint64_t cycles) {
    if (desc_ != nullptr && desc_->api_version >= SIMRV_EXTERNAL_DEVICE_API_VERSION &&
        desc_->advance_cycles != nullptr) {
        desc_->advance_cycles(ctx_, cycles);
    }
}

auto ExternalMmioDevice::has_cycle_ticking() const noexcept -> bool {
    return desc_ != nullptr && desc_->api_version >= SIMRV_EXTERNAL_DEVICE_API_VERSION &&
           desc_->advance_cycles != nullptr;
}

ExternalMmioDevice::~ExternalMmioDevice() {
    if (desc_ != nullptr && desc_->destroy != nullptr) {
        desc_->destroy(ctx_);
        ctx_ = nullptr;
    }
}

auto ExternalMmioDevice::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp)
    -> bool {
    resp.source = req.source;
    resp.size = req.size;
    resp.denied = false;
    resp.corrupt = false;
    resp.data = 0;
    if (req.size > 3 || desc_ == nullptr) {
        resp.denied = true;
        return true;
    }
    const auto offset = req.address.raw() - base_.raw();
    const uint32_t bytes = uint32_t{1} << req.size;
    const uint64_t mask = bytes == 8 ? UINT64_MAX : (uint64_t{1} << (bytes * 8)) - 1;
    if (req.opcode == memory::TlOpcodeA::Get) {
        resp.opcode = memory::TlOpcodeD::AccessAckData;
        uint64_t value = 0;
        resp.denied = !desc_->read || desc_->read(ctx_, offset, bytes, &value) != 0;
        if (!resp.denied) resp.data = static_cast<simrv::xlen::Word>(value & mask);
    } else if (req.opcode == memory::TlOpcodeA::PutFullData ||
               req.opcode == memory::TlOpcodeA::PutPartialData) {
        resp.opcode = memory::TlOpcodeD::AccessAck;
        resp.denied = !desc_->write || desc_->write(ctx_, offset, bytes, req.data & mask) != 0;
    } else {
        resp.denied = true;
        return false;
    }
    return true;
}

auto ExternalMmioDevice::read8(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData8 {
    if (desc_ == nullptr || desc_->read == nullptr) return 0;
    uint64_t val = 0;
    desc_->read(ctx_, offset.raw(), 1, &val);
    return static_cast<simrv::memory::MmioData8>(val & 0xffU);
}

auto ExternalMmioDevice::read16(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData16 {
    if (desc_ == nullptr || desc_->read == nullptr) return 0;
    uint64_t val = 0;
    desc_->read(ctx_, offset.raw(), 2, &val);
    return static_cast<simrv::memory::MmioData16>(val & 0xffffU);
}

auto ExternalMmioDevice::read32(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData32 {
    if (desc_ == nullptr || desc_->read == nullptr) return 0;
    uint64_t val = 0;
    desc_->read(ctx_, offset.raw(), 4, &val);
    return static_cast<simrv::memory::MmioData32>(val & 0xffffffffU);
}

auto ExternalMmioDevice::read64(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData64 {
    if (desc_ == nullptr || desc_->read == nullptr) return 0;
    uint64_t val = 0;
    desc_->read(ctx_, offset.raw(), 8, &val);
    return val;
}

void ExternalMmioDevice::write8(simrv::memory::MmioOffset offset, simrv::memory::MmioData8 val) {
    if (desc_ != nullptr && desc_->write != nullptr) {
        desc_->write(ctx_, offset.raw(), 1, val);
    }
}

void ExternalMmioDevice::write16(simrv::memory::MmioOffset offset, simrv::memory::MmioData16 val) {
    if (desc_ != nullptr && desc_->write != nullptr) {
        desc_->write(ctx_, offset.raw(), 2, val);
    }
}

void ExternalMmioDevice::write32(simrv::memory::MmioOffset offset, simrv::memory::MmioData32 val) {
    if (desc_ != nullptr && desc_->write != nullptr) {
        desc_->write(ctx_, offset.raw(), 4, val);
    }
}

void ExternalMmioDevice::write64(simrv::memory::MmioOffset offset, simrv::memory::MmioData64 val) {
    if (desc_ != nullptr && desc_->write != nullptr) {
        desc_->write(ctx_, offset.raw(), 8, val);
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
            if (const auto separator = base.find('-'); separator != std::string::npos) {
                const auto suffix = base.substr(separator + 1);
                candidates.push_back("libsimrv-" + suffix + ext);
                candidates.push_back("lib" + suffix + ext);
            }
        }
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

    if (dev->api_version != SIMRV_EXTERNAL_DEVICE_API_VERSION &&
        dev->api_version != SIMRV_EXTERNAL_DEVICE_API_VERSION_V1) {
        simrv::log::error("Plugin '{}' has incompatible API version {} (expected {} or {})", path,
                          dev->api_version, SIMRV_EXTERNAL_DEVICE_API_VERSION,
                          SIMRV_EXTERNAL_DEVICE_API_VERSION_V1);
        dlclose(handle);
        return false;
    }

    const std::string compatible = dev->compatible != nullptr ? dev->compatible : "";
    const std::string name = dev->name != nullptr ? dev->name : "external-device";
    auto serial_init =
        reinterpret_cast<SimRvSerialPluginInitFn>(dlsym(handle, "simrv_serial_plugin_init"));
    const SimRvExternalSerialDevice* serial = serial_init ? serial_init() : nullptr;
    if (serial_init && (!serial || serial->api_version != SIMRV_EXTERNAL_SERIAL_API_VERSION ||
                        !serial->receive_byte || compatible.empty())) {
        simrv::log::error("Plugin '{}' returned invalid serial capability", path);
        dlclose(handle);
        return false;
    }

    auto fb_init = reinterpret_cast<SimRvFramebufferPluginInitFn>(
        dlsym(handle, "simrv_framebuffer_plugin_init"));
    const SimRvExternalFramebufferDevice* fb = fb_init ? fb_init() : nullptr;
    if (fb_init && (!fb || fb->api_version != SIMRV_EXTERNAL_FRAMEBUFFER_API_VERSION ||
                    !fb->get_info || !fb->get_buffer || compatible.empty())) {
        simrv::log::error("Plugin '{}' returned invalid framebuffer capability", path);
        dlclose(handle);
        return false;
    }

    auto input_init =
        reinterpret_cast<SimRvInputPluginInitFn>(dlsym(handle, "simrv_input_plugin_init"));
    const SimRvExternalInputDevice* input = input_init ? input_init() : nullptr;
    if (input_init && (!input || input->api_version != SIMRV_EXTERNAL_INPUT_API_VERSION ||
                       !input->send_key || compatible.empty())) {
        simrv::log::error("Plugin '{}' returned invalid input capability", path);
        dlclose(handle);
        return false;
    }

    simrv::log::info("Loaded external device plugin '{}' ({}, compatible: '{}')", path, name,
                     compatible);

    SerialDeviceFactory serial_factory;
    if (serial) {
        serial_factory =
            [dev, serial, default_args = args](
                simrv::core::Machine& machine,
                const simrv::core::SoCDeviceConfig& config) -> std::unique_ptr<SerialDevice> {
            void* ctx = nullptr;
            if (dev->init && dev->init(&ctx, default_args.c_str()) != 0) return {};
            return std::make_unique<ExternalSerialAdapter>(machine, config, dev, serial, ctx);
        };
    }

    auto factory = [dev, fb, input, default_args = std::move(args), name](
                       simrv::core::Machine& machine, const simrv::core::SoCDeviceConfig& config)
        -> std::unique_ptr<simrv::memory::MmioDevice> {
        void* ctx = nullptr;
        std::string effective_args = default_args;
        if (effective_args.empty()) {
            if (!machine.files_config().disk_path.empty()) {
                effective_args = machine.files_config().disk_path;
            } else if (machine.files_config().binary_path.ends_with(".img") ||
                       machine.files_config().binary_path.ends_with(".raw") ||
                       machine.files_config().binary_path.ends_with(".ext4") ||
                       machine.files_config().binary_path.ends_with(".bin")) {
                effective_args = machine.files_config().binary_path;
            }
        }
        if (dev->init != nullptr) {
            if (dev->init(&ctx, effective_args.c_str()) != 0) {
                return nullptr;
            }
        }
        const auto base = config.base != 0 ? config.base.raw() : dev->default_base;
        const auto size = config.size != 0 ? config.size : dev->default_size;
        const auto dev_name = !config.name.empty() ? config.name : name;
        const auto physical_base = simrv::xlen::PhysAddr{static_cast<simrv::xlen::Address>(base)};
        if (fb && input) {
            return std::make_unique<ExternalFramebufferInputAdapter>(dev, fb, input, ctx, dev_name,
                                                                     physical_base, size, machine);
        }
        if (fb) {
            return std::make_unique<ExternalFramebufferAdapter>(dev, fb, ctx, dev_name,
                                                                physical_base, size, machine);
        }
        if (input) {
            return std::make_unique<ExternalInputAdapter>(dev, input, ctx, dev_name, physical_base,
                                                          size, machine);
        }
        return std::make_unique<ExternalMmioDevice>(dev, ctx, dev_name, physical_base, size,
                                                    machine);
    };

    const bool registered = !compatible.empty()
                                ? registry.register_external_compatible(
                                      compatible, std::move(factory), std::move(serial_factory))
                                : registry.register_mmio_device(
                                      simrv::core::SoCDeviceKind::CustomMmio, std::move(factory));
    if (!registered) {
        simrv::log::error("Duplicate or invalid device plugin compatible '{}'", compatible);
        dlclose(handle);
        return false;
    }
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
