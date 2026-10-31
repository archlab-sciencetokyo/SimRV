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

## Remaining fixed behavior

| Priority | Finding | Classification | Evidence and rationale |
| --- | --- | --- | --- |
| Resolved | Generated FDT and UART runtime use the normalized UART input clock; reset divisor and 8N1 line format produce the reported current speed. | Descriptor default and 16550 reset contract | `SoCDeviceConfig::clock_hz` is the guest-visible source. The simulator schedules whole serial frames at CPU-cycle boundaries; it does not model individual wire bits. |
| Low | Generated FDT reports implemented ISA extensions and cache-block sizes from fixed capability lists. | Simulator capability contract | `FdtGenerator.cpp` and ISA/cache implementations. These lists should remain centralized with implemented capabilities; they are not user-selectable hardware claims today. |
| Low | UART reset baud and line format, PLIC priority behavior, and timer tick semantics are fixed. | Device-protocol or implementation contract | UART resets to 115200 8N1 using a divisor derived from its configured input clock. PLIC priority and timer tick rules remain implementation contracts. |

## Generated-DTB and runtime scope

For modeled descriptors, generated address, range, interrupt, PCIe BDF, INTx, and auxiliary-region
data follows the same normalized SoC configuration used by machine composition. Custom compatible
strings are emitted when provided. Devices absent from an explicit device policy are omitted. The built-in
`virt-pcie` and `virt-mmio` presets retain their Linux discovery maps, with descriptor defaults now
serving as the shared source for runtime and generated metadata.

Some metadata fields have no corresponding runtime configurability yet, as listed above. In
particular, user-supplied DTBs remain independently authored and use the existing patching workflow.
