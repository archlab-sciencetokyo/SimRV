// SPDX-License-Identifier: MIT
#include "simrv/v3/Simulator.hpp"

#include <atomic>
#include <memory>
#include <utility>

#include "simrv/core/BaremetalMachine.hpp"
#include "simrv/core/OSMachine.hpp"
#include "simrv/core/Telemetry.hpp"
#include "simrv/util/CliParser.hpp"
#include "simrv/v3/Plugin.hpp"

namespace simrv::v3 {
namespace {
auto event_kind(core::LifecycleEventKind kind) -> EventKind {
    switch (kind) {
        case core::LifecycleEventKind::Started: return EventKind::Started;
        case core::LifecycleEventKind::Stopped: return EventKind::Stopped;
        case core::LifecycleEventKind::RebootRequested: return EventKind::RebootRequested;
        case core::LifecycleEventKind::ExitRequested: return EventKind::ExitRequested;
    }
    return EventKind::Stopped;
}
auto runtime_options(const RunManifest& manifest) -> util::RuntimeOptions {
    util::RuntimeOptions options;
    options.fn_memimg = manifest.program.string();
    options.fn_dskimg = manifest.disk.string();
    options.fn_dvtree = manifest.device_tree.string();
    options.use_disk = !manifest.disk.empty();
    options.appmode = manifest.baremetal;
    options.tuimode = manifest.interaction == Interaction::Tui;
    options.explicit_tui_mode = options.tuimode;
    options.explicit_cli_mode = !options.tuimode;
    options.cycle_mode_requested = manifest.cycle_accurate;
    options.instruction_mode_requested = !manifest.cycle_accurate;
    options.num_harts = manifest.harts;
    options.vlen = manifest.vlen;
    return options;
}
}  // namespace

class Simulator::Impl {
   public:
    explicit Impl(RunManifest input) : manifest(std::move(input)) {}
    RunManifest manifest;
    std::unique_ptr<core::Machine> machine;
    bool initialized = false;
};

Simulator::Simulator() = default;
Simulator::Simulator(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Simulator::~Simulator() = default;
Simulator::Simulator(Simulator&&) noexcept = default;
auto Simulator::operator=(Simulator&&) noexcept -> Simulator& = default;

auto Simulator::create(const RunManifest& manifest) -> Result<Simulator> {
    if (auto valid = validate_manifest(manifest); !valid) return std::unexpected(valid.error());
    for (const auto& plugin : manifest.plugins) {
        if (auto valid = validate_plugin_declaration(plugin); !valid) return std::unexpected(valid.error());
    }
    return Simulator(std::make_unique<Impl>(manifest));
}

auto Simulator::initialize() -> Result<void> {
    if (!impl_) return std::unexpected(Error{"simulator is not configured"});
    if (impl_->initialized) return {};
    if (impl_->manifest.baremetal) {
        impl_->machine = std::make_unique<core::BaremetalMachine>();
    } else {
        impl_->machine = std::make_unique<core::OSMachine>();
    }
    const auto options = runtime_options(impl_->manifest);
    if (auto applied = util::apply_runtime_options(impl_->machine.get(), options); !applied) {
        return std::unexpected(Error{applied.error()});
    }
    if (const auto result = impl_->machine->initialize(); result != 0) {
        return std::unexpected(Error{"machine initialization failed with status " + std::to_string(result)});
    }
    impl_->initialized = true;
    return {};
}

auto Simulator::run() -> Result<int> {
    if (auto initialized = initialize(); !initialized) return std::unexpected(initialized.error());
    impl_->machine->run();
    return impl_->machine->exit_code.load(std::memory_order_relaxed);
}

void Simulator::step() { if (impl_ && impl_->machine) impl_->machine->step(); }
void Simulator::pause() { if (impl_ && impl_->machine) impl_->machine->pause(); }
void Simulator::resume() { if (impl_ && impl_->machine) impl_->machine->resume(); }
void Simulator::stop() { if (impl_ && impl_->machine) impl_->machine->stop(); }

auto Simulator::snapshot() const -> Snapshot {
    if (!impl_ || !impl_->machine) return {};
    return {.instruction_count = impl_->machine->primary_hart().e_icount,
            .exit_status = impl_->machine->exit_code.load(std::memory_order_relaxed),
            .running = impl_->machine->is_running(),
            .paused = impl_->machine->is_paused()};
}

auto Simulator::subscribe(EventObserver observer) -> ObserverId {
    if (!impl_ || !impl_->machine) return 0;
    return impl_->machine->add_lifecycle_observer([observer = std::move(observer)](const core::LifecycleEvent& source) {
        observer(Event{.kind = event_kind(source.kind), .instruction_count = source.instruction_count,
                       .exit_status = source.exit_status, .stop_reason = source.stop_reason});
    });
}
void Simulator::unsubscribe(ObserverId observer) { if (impl_ && impl_->machine && observer != 0) impl_->machine->remove_lifecycle_observer(observer); }
}  // namespace simrv::v3
