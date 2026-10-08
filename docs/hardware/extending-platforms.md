# Extending CPU Presets and MMIO Platforms

This guide covers two different kinds of extension:

1. Create a CPU timing model with a configuration file. This does not require C++ changes.
2. Add a new emulated MMIO device to SimRV. This requires a device implementation and platform
   integration.

Start with a model file when your goal is to describe a CPU's timing or select devices SimRV
already supports. Implement a new device only when the hardware's register behavior is not already
modeled.

!!! info "A preset configures models SimRV already implements"
    CPU presets describe timing and platform configuration. They do not add new instruction
    semantics or MMIO register behavior; those require a simulator implementation.

## Create a custom CPU model

Copy a model close to the target, or scaffold a starter file:

```bash
simrv --dump-cpu-model balanced configs/models/my-core.cfg
# Or generate an annotated starter:
simrv --scaffold-cpu-model configs/models/my-core.cfg
```

Edit the `[cpu]`, `[pipeline]`, cache, branch predictor, interconnect, and optional accelerator
sections to describe the target. Use the parameter reference in [Microarchitectural Models &
Tuning](models.md) for supported keys and units. Latencies are additional stall cycles after issue
where documented; convert a vendor's total instruction latency to the model's convention before
using it.

Validate the file by loading it in cycle-accurate mode:

```bash
simrv --ca --cpu-preset configs/models/my-core.cfg -m program.elf
```

The path can be supplied through `--cpu-preset` or `--cpu-model-file`. A custom file is a
user-authored model; it does not add a named built-in preset. To add a built-in preset, update the
preset enum, name parsing, factory/default configuration, and preset-focused tests. Keep the
compiled fallback behavior intentional and document whether the preset supports RV32, RV64, or
both.

If the preset describes a complete platform, add `[soc]` and `[device.<kind>]` sections. For
example:

```ini
[soc]
device_policy = "explicit"

[device.uart]
name = "uart0"
base = 0x10000000
size = 0x10
irq = 1
```

`device_policy = "explicit"` means only the configured device set is registered. Otherwise the
platform's implicit defaults may remain enabled. Device kinds and configurable descriptor fields
are defined by the current registry; a config entry cannot create a new device implementation or
change its register semantics. The [ArchLab RVComp example platform](https://github.com/archlab-sciencetokyo/RVComp)
(described in its [paper](https://www.ieice.org/publications/ken/summary.php?contribution_id=138850&expandable=0&ken_id=R&lang=en&presen_date=2025-09-19&schedule_id=8813&society_cd=ESSNLS&year=2025)) includes the opt-in `reset-control` and
`ring-buffer-ethernet` extensions. The Ethernet descriptor's `base` selects the CSR window; RX and
TX ring windows are configurable with `region.rxbuf.base` / `region.rxbuf.size` and
`region.txbuf.base` / `region.txbuf.size`. The `mac_address` field accepts a 48-bit hexadecimal
value. These values feed the normalized SoC map used
for runtime registration, FDT generation, and the SoC manifest. Check the resolved result with:

```bash
simrv --dump-soc-manifest rvcomp /tmp/my-core-soc.json
```

Replace `rvcomp` with the applicable SoC preset. For custom CPU/SoC files, use the CPU model
configuration workflow and inspect the platform in a guest boot or generated FDT.

### Reserve a device window with a dummy model

When a custom SoC has an address range with no matching SimRV model, configure a dummy device. It
returns a constant value on reads and ignores writes. The MMIO router records accesses in the
normal device trace, so guest writes can be inspected without producing an unbounded log:

```ini
[device.dummy-mmio.watchdog]
base = 0x10010000
size = 0x1000
compatible = "acme,watchdog"
read_value = 0

[device.dummy-mmio.fpga-control]
base = 0x10011000
size = 0x1000
compatible = "acme,fpga-control"
read_value = 0xffffffff
```

Select an instance with `--trace-device watchdog` or `--trace-device fpga-control`, for example
`simrv --cli -m program.elf --arch-trace trace.jsonl --trace-level 1 --trace-device watchdog`.
Select the `mmio_write` event when using trace event filters to retain writes to that window. Dummy
windows appear in generated FDT and SoC manifests, but do not implement the hardware named by their
compatible string. The section suffix supplies the trace name; set `name = "..."` to choose a
different name.

!!! warning "Dummy windows are not hardware models"
    Dummy devices reserve an address range, return a configured constant on reads, and ignore
    writes. They are useful for integration and logging, but guest software that expects register
    side effects will need a real device implementation.

### Attach a custom C++ MMIO model

An embedding application can register a C++ device factory for a custom compatible string. Add a
custom descriptor to the SoC file:

```ini
[device.custom-mmio.timer]
name = "timer"
base = 0x10012000
size = 0x1000
irq = 0
compatible = "acme,timer"
```

Register the factory on the machine before calling `initialize()`:

```cpp
simrv::core::MachineConfig config;
simrv::core::Machine machine(config);
machine.device_registry().register_mmio_compatible(
    "acme,timer", [](simrv::core::Machine& machine,
                     const simrv::core::SoCDeviceConfig& config) {
        return std::make_unique<MyTimer>(machine, config.name, config.base, config.size);
    });
machine.initialize();
```

`MyTimer` derives from `simrv::memory::MmioDevice`; its name, base address, and size must match
the descriptor. Factories are scoped to one `Machine`, so embedding applications can run different
custom SoCs side by side without sharing a process-global registration table. A registered device
is owned by the machine runtime and installed on the system bus. If no factory is registered, the
range remains present as an inert constant-read, ignored-write
device, so a custom SoC config can still be inspected and traced without the model. IRQ wiring is
not part of this factory API yet; use `irq = 0`. The compatible, range, and device name are emitted
to the generated FDT and SoC manifest in either case. `Machine::device_registry()` is scoped to one
machine and also constructs SimRV's built-in reset-control device through the same factory path.

!!! warning "Custom C++ device factories do not wire interrupts yet"
    Keep `irq = 0` for a registered custom factory. The descriptor field is present for platform
    metadata, but this factory API does not connect a custom device to a hart interrupt line.

Factories create the node during initialization; the runtime owns it and attaches it to the system
bus. Embedders can subscribe with `Machine::add_event_observer()` to observe interrupt line edges and
DMA start, completion, and cancellation events independently of architectural trace-file settings.
For example, an embedding application can attach a small observer before machine initialization:

```cpp
const auto observer_id = machine.add_event_observer([](const simrv::core::MachineEvent& event) {
    if (event.kind == simrv::core::MachineEventKind::InterruptAsserted ||
        event.kind == simrv::core::MachineEventKind::InterruptDeasserted) {
        std::println("cycle {}: {} interrupt from {}", event.cycle,
                     event.kind == simrv::core::MachineEventKind::InterruptAsserted ? "asserted"
                                                                                     : "cleared",
                     event.component);
    }
});
machine.initialize();
machine.run();
// Remove the observer before its captured state or output sink is destroyed.
machine.remove_event_observer(observer_id);
```

Observers run synchronously on the simulation thread that produces the event (which can be a hart
worker in multithreaded SMP mode), so keep callbacks short and synchronize shared output sinks.
Use `MachineEventKind::DmaStarted`, `DmaCompleted`, and `DmaCancelled` with the transfer ID,
component, byte count, and request/start/completion cycle fields for DMA tracking. These hooks report
machine activity; they do not provide a device IRQ-routing API or change a device's modeled timing.

## Add an emulated MMIO device

First check `SoCDeviceKind` and `SoCDeviceRegistry` for an existing device kind. If SimRV already
implements the hardware, configure its descriptor or add it to a platform preset rather than
creating a second device class.

For a new device, a typical implementation derives from `simrv::memory::MmioDevice`. Implement the
register reads and writes, and provide stable `name()`, `base_address()`, and `size()` values from
the `TileLinkNode` interface. `MmioDevice` adapts typed register accesses to the simulator's
TileLink-style request interface and offers DMA helpers when the device needs guest-memory access.
Use a direct `TileLinkNode` implementation only when the device needs custom request/response
handling. Keep device state owned by the machine runtime so registered bus-node pointers remain
valid for the full machine lifetime.

Integrate a preset-configurable device through the shared SoC description rather than registering
it in only one code path:

1. Add the kind and descriptor defaults to `SoCConfig` and include it in `SoCDeviceRegistry`.
2. Parse and validate its configuration, including nonzero, nonwrapping address range and
   interrupt rules. Preserve overlap rejection.
3. Construct and own the device during machine initialization, then register it on the system bus
   according to the device policy.
4. Connect interrupts, DMA, reset, and any transport or backing storage it requires.
5. Add generated FDT and `--dump-soc-manifest` representation when the guest or platform tooling
   needs to discover the device. Keep the runtime map, FDT, and manifest consistent.
6. Add device-level register tests, address-map/config tests, and guest-facing integration
   coverage. Validate both RV32 and RV64 builds when the device is available in both.

Some devices are composed by `PlatformBuilder` or exposed through PCIe/VirtIO transports rather
than constructed as simple base nodes. Follow the nearest existing device implementation and
verify transport discovery as well as MMIO access. Do not assign a standard VirtIO register layout
to custom RTL hardware just to make it discoverable.

## Validation checklist

- Confirm the CPU model parses and passes its supported XLEN/ISA checks.
- Check timing against the target's documented cycle convention and, for RTL-backed models, run
  the applicable cycle-accuracy/parity gates.
- Confirm the complete address map has no overlaps and matches the intended reset/boot setup.
- Check that the generated FDT and SoC manifest agree with registered runtime devices.
- Exercise register side effects, invalid accesses, interrupts, reset, and DMA boundaries as
  applicable.
- Run the relevant unit and integration tests plus RV32/RV64 release gates. See the
  [Developer Guide](../development/contributing.md#5-adding-regression-tests) for gate commands.

For the existing platform syntax and RVComp map, see [Microarchitectural Models & Tuning](models.md).
