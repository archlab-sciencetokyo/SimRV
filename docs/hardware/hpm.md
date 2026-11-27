# HPM Counters and Telemetry Events

SimRV's programmable hardware performance counters are the source of truth for architectural
event counts. A guest selects an event by writing its SimRV event ID to `mhpmevent3` through
`mhpmevent31`; the matching count is then read from `mhpmcounter3` through `mhpmcounter31` (or the
visible `hpmcounter` alias). Unselected events do not accumulate a hidden second total.

The event IDs below are SimRV-defined and implementation-specific. Programs that use them should
identify SimRV as the target rather than assume the same IDs mean the same thing on other RISC-V
implementations. Multiple counters may select one event. Existing privilege filters and
`mcountinhibit` apply to each selected counter.

<!-- BEGIN GENERATED SIMRV HPM CATALOG -->
| ID | Event | Counting boundary |
|---:|---|---|
| 1 | cycles | One simulated CPU cycle. |
| 2 | instructions | One completed guest instruction. |
| 3 | icache-miss | One instruction-cache lookup miss. |
| 4 | dcache-miss | One data-cache miss or write requiring acquisition. |
| 5 | dcache-writeback | One modified data-cache line evicted. |
| 6 | itlb-miss | One instruction TLB miss requiring a page walk. |
| 7 | dtlb-miss | One data TLB miss requiring a page walk. |
| 8 | conditional-branches | One conditional branch completed. |
| 9 | branch-mispredict | One resolved prediction miss. |
| 10 | stall-cycles | One cycle with an execution pipeline stall. |
| 11 | data-hazard-stall | One cycle stalled by a data hazard. |
| 12 | structural-stall | One cycle stalled by a structural hazard. |
| 13 | interconnect-stall | One cycle waiting for an interconnect response. |
| 14 | load-instruction | One load instruction completed. |
| 15 | store-instruction | One store instruction completed. |
| 16 | integer-alu-instruction | One integer ALU instruction completed. |
| 17 | muldiv-instruction | One multiply/divide instruction completed. |
| 18 | float-instruction | One floating-point instruction completed. |
| 19 | vector-instruction | One vector instruction completed. |
| 20 | compressed-instruction | One 16-bit instruction completed. |
<!-- END GENERATED SIMRV HPM CATALOG -->

Instruction-class events may overlap: for example, a compressed load increments the compressed
and load events. Branch instruction counts include conditional branches; jumps are not included.
Cache-miss counts follow cache lookup attempts, while writebacks count only modified-line evictions.
These definitions distinguish event totals from derived rates such as IPC or cache hit rate.

HPM counters are programmable architectural state. SimRV's broader performance reports may also
show subsystem snapshots and derived ratios; those are not replacements for the guest-visible HPM
counters and do not change their selector or privilege-filter behavior.

## Linux `perf` access

With OpenSBI as the machine-mode firmware, the `riscv,pmu` device-tree node maps the standard SBI
branch-misses event to SimRV selector 9 and exposes raw selector values 1–20 to `perf`. Linux
configures those counters through SBI PMU; the underlying `mhpmcounter` CSRs remain the only
accumulated HPM totals. Cycle and retired-instruction counts use the architectural `cycle` and
`instret` counters. The generic branches event is intentionally not mapped because SimRV's
selector 8 counts conditional branches only.

On a Linux image with `CONFIG_RISCV_PMU_SBI` and `perf` installed, try:

```sh
perf stat -e cycles,instructions,branch-misses -- ./program
perf stat -e r3 -e r4 -e r8 -e r9 -- ./program
```

Raw `rN` events select SimRV selector N. Since these IDs are implementation-specific, raw event
names and meanings are portable only across compatible SimRV builds. Sampling through `perf record`
also depends on the guest kernel and Sscofpmf overflow support; event counting is the supported
initial use case.

This Linux integration relies on two standardized layers: the privileged architecture defines
the counter CSRs and access rules, while SBI PMU defines supervisor discovery/configuration and
Linux `perf` integration. The mapping from SimRV event IDs to `mhpmevent` selector values remains
implementation-specific and is advertised in the device tree; RISC-V does not assign universal
meanings to those selector numbers. See the [SBI PMU specification](https://github.com/riscv-non-isa/riscv-sbi-doc/blob/master/src/ext-pmu.adoc),
[OpenSBI PMU device-tree mapping](https://raw.githubusercontent.com/riscv-software-src/opensbi/master/docs/pmu_support.md),
and [Linux RISC-V PMU binding](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/perf/riscv,pmu.yaml).
