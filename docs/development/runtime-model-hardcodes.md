# Runtime Model Formalization Audit

This audit records fixed runtime values that can affect agreement between the selected SoC,
guest-visible metadata, and simulator behavior. Architectural and device-protocol rules remain
fixed by design; preset defaults are centralized where practical; model behavior is configured from
the normalized SoC description when the model supports it.

## Addressed in this pass

| Finding | Change | Classification |
| --- | --- | --- |
| Generated DTB device map differed from the normalized SoC description. | Auto-generated DTBs now resolve enabled descriptors and emit their supported compatible, base, size, IRQ, and auxiliary regions. Explicitly excluded devices are omitted. Caller-provided DTBs retain their patching workflow. | Model behavior is descriptor-driven. |
| Built-in UART, RTC, power control, DMA, ACLINT, PLIC, CLINT, IMSIC/APLIC, and PFlash runtime maps ignored descriptor addresses, ranges, or IRQs. | Machine composition passes normalized descriptor values into those implementations. Controller windows are installed from configured descriptors. PFlash bank layout uses its descriptor regions. | Model behavior is descriptor-driven for these devices. |
| Cache prewarming preferred conventional address `0x10000000` when it happened to lie in RAM. | The fallback cache warmup address is now the configured DRAM base. | Model heuristic now follows the configured memory map. |
| PCIe host ranges, bus window, endpoint slots, and INTx source assignments were fixed independently in runtime and FDT code. | A typed PCIe host configuration and per-endpoint BDF/INTx fields now drive composition, BAR routing, interrupt delivery, manifests, and generated FDT. | Model topology is descriptor-driven with preset defaults. |
| PLIC runtime accepted only source IDs 1–31 while generated FDT and SoC validation carried independent limits. | One shared 31-source contract now controls PLIC delivery, PLIC-routed IRQ validation, PCIe INTx validation, and generated `riscv,ndev`. Source 0 remains reserved. | Fixed implemented controller capacity, shared across runtime and guest metadata. |
| Per-hart controller capacities and configured machine limits repeated the same 16-hart maximum; PLIC separately fixed 32 contexts. | One shared capacity now sizes and validates CLINT, ACLINT, AIA, and PLIC storage. PLIC retains two contexts per hart. | Fixed simulator implementation capacity, not a user-configurable SoC property. |
| CPU and timebase frequencies were cast to FDT `u32` properties without checking representability. | Machine configuration and DTB patching reject frequencies above `UINT32_MAX`; valid CLI clocks continue to flow to runtime and guest metadata. | FDT property width is a format constraint. |
| PLIC completion register hardcoded IRQ 3 for level-sensitive re-assertion. | PLIC dynamically tracks physical input wire levels (`input_levels_`) across all sources and checks device pending state (`is_interrupt_pending()`) upon claim-complete, eliminating device-specific IRQ assumptions. | Device-protocol contract compliance. |
| Fast OS kernel batch execution could delay UART cycle progression during tight polling. | `run_fast_os_kernel` flushes pending simulation cycles early whenever the UART device has active frame transmission pending, ensuring TEMT/THRE polling completes promptly. | Simulation cycle pacing heuristic. |
| Multi-hart fast execution allowed secondary harts to advance CLINT `mtime` independently. | Batch fast-execution paths now enforce a single platform timebase where secondary harts synchronize `mtime` from the primary hart. | Architectural multi-hart timebase specification. |
| Writes to read-only CSRs (`mvendorid`, `marchid`, `mimpid`, `mhartid`, `time`, `cycle`, `instret`) did not raise exceptions. | `CsrFile::write` enforces read-only address checks (`is_read_only()`) and raises `IllegalInstruction` traps per RISC-V Privileged Architecture. | Privileged architecture specification compliance. |
| Hypervisor G-stage two-stage page table walks fetched physical PTEs without PMP verification. | `PageTableWalker::walk_guest_physical` enforces `pmp::check_access` under supervisor privilege for all intermediate G-stage PTE accesses. | Privileged architecture PMP enforcement. |
| APLIC direct-mode interrupt delivery never de-asserted external interrupt lines when cleared. | `Aplic::update()` evaluates active sources per target hart and explicitly drives `set_hart_irq(..., false)` when interrupts are cleared or when delivery is disabled (`domaincfg`). | AIA direct-mode specification compliance. |
| PLIC writes to priority, enable, and threshold registers did not dynamically re-evaluate pending lines. | `Plic::mmio_write` invokes `update_mip()` upon priority, enable, and threshold updates so unmasking interrupts immediately triggers CPU interrupt pending state. | PLIC arbitration specification compliance. |
| ACLINT dropped 32-bit MMIO word writes to `mtime` (0x7ff8/0x7ffc) and omitted secondary harts. | `Clint::mmio_write` accepts 32-bit word writes to 0x7ff8/0x7ffc and re-evaluates timer interrupts across all configured harts in the machine. | ACLINT specification compliance. |
| Sv39 Page Directory Cache (PDC) did not isolate Hypervisor guest two-stage page walks. | `PageTableWalker` guards PDC lookups and insertions with `!state.guest_stage_enabled`, preventing guest-physical table addresses from bypassing G-stage translation or corrupting host PDC entries. | Hypervisor memory virtualization specification compliance. |

## Remaining fixed behavior

| Priority | Finding | Classification | Evidence and rationale |
| --- | --- | --- | --- |
| Resolved | Generated FDT and UART runtime use the normalized UART input clock; reset divisor and 8N1 line format produce the reported current speed. | Descriptor default and 16550 reset contract | `SoCDeviceConfig::clock_hz` is the guest-visible source. The simulator schedules whole serial frames at CPU-cycle boundaries; it does not model individual wire bits. |
| Resolved | Generated and patched FDTs report implemented ISA extensions and cache-block sizes from shared capability data. | Simulator capability contract | The guest capability catalog is shared by FDT generation, patching, and synchronized DTS templates. Cache-block size follows the implemented cache line size. |
| Low | UART reset baud and line format, PLIC priority behavior, and timer tick semantics are fixed. | Device-protocol or implementation contract | UART resets to 115200 8N1 using a divisor derived from its configured input clock. PLIC priority and timer tick rules remain implementation contracts. |

## Guest completion and UART scope

Serial transmission advances only with simulated CPU cycles. The example guests poll the UART TEMT
bit before signaling `tohost`, and THRE before each transmit write because the reset-disabled FIFO
holds one pending byte. Their advertised output has completed transmission when the simulation
stops. This is guest behavior; simulator termination does not invent additional cycles.

## Generated-DTB and runtime scope

For modeled descriptors, generated address, range, interrupt, PCIe BDF, INTx, and auxiliary-region
data follows the same normalized SoC configuration used by machine composition. Custom compatible
strings are emitted when provided. Devices absent from an explicit device policy are omitted. The built-in
`virt-pcie` and `virt-mmio` presets retain their Linux discovery maps, with descriptor defaults now
serving as the shared source for runtime and generated metadata.

Some metadata fields have no corresponding runtime configurability yet, as listed above. In
particular, user-supplied DTBs remain independently authored and use the existing patching workflow.
