# SimRV 3.0 User Guide

SimRV is an explainable, dual-width (RV32 / RV64) RISC-V architectural simulator with an interactive terminal workbench (TUI), cycle-accurate microarchitectural modeling, reference parity checks, and full-system SMP Linux emulation.

---

## Table of Contents

1. [Architecture & Features Overview](#1-architecture-features-overview)
2. [Installation & Quickstart](#2-installation-quickstart)
3. [Command-Line Interface (CLI)](#3-command-line-interface-cli)
4. [Interactive TUI Workbench](#4-interactive-tui-workbench)
5. [Bare-Metal & Embedded Simulation](#5-bare-metal-embedded-simulation)
6. [Full-System Linux Emulation](#6-full-system-linux-emulation)
7. [CPU Model Presets & Microarchitecture Tuning](#7-cpu-model-presets-microarchitecture-tuning)
8. [Reference Parity Verification](#8-reference-parity-verification)
9. [Benchmarking Suite](#9-benchmarking-suite)
10. [Troubleshooting & Reference](#10-troubleshooting-reference)

---

## 1. Architecture & Features Overview

SimRV simulates standard 32-bit and 64-bit RISC-V systems:

- **ISA Support**: RV32GCBV / RV64GCBV (Base Integer `I`/`E`, Standard Multiply/Divide `M`, Atomic `A`, Single/Double Floating Point `F`/`D`, Compressed `C`, Bit Manipulation `B`, and Vector 1.0 `V`).
- **Privilege & System Architecture**: Machine (M), Supervisor (S), and User (U) privilege modes with PMP (Physical Memory Protection), Sv32 / Sv39 MMU page translation, CLINT / ACLINT timer and software interrupts, and PLIC / AIA (APLIC/IMSIC) interrupt routing.
- **Microarchitectural Modeling**: Per-cycle transition kernels for three-stage, five-stage, and dual-issue pipelines with configurable branch prediction (static, bimodal, 2-level adaptive, RAS, BTB) and multi-level L1/L2/L3 cache hierarchies.
- **Multi-Hart Coherence**: TileLink-C directory-based coherence hubs modeling MESI protocols for SMP configurations (2 to 16 harts).
- **Reference Parity**: Compare architectural retirement and cycle-level behavior with external implementations and measured traces.
- **Buildroot guests**: Load a Buildroot output directory or published bundle, with matching firmware, device tree, and root filesystem resolved together.
- **Custom SoCs**: Configure CPU models and device maps, register reusable MMIO extensions, or describe unmapped ranges with traceable dummy devices.
- **Interactive TUI**: Educational split-screen monitor displaying register banks, pipeline slots, cache lines, hazard graphs, disassembly explainers, and an interactive Linux PTY terminal.

---

## 2. Installation & Quickstart

For the portable archive and native RPM or DEB packages, start with the dedicated
[installation guide](install.md). The instructions below cover source builds.

### Prerequisites

- Modern C++23 compiler: **Clang 22+** or **GCC 16+**
- Build tools: **CMake 3.31+**, **Ninja**, **mold** (recommended for fast linking)
- Python: **Python 3.10+** (for utility tools and test suites)

### Building from Source

SimRV requires using standard CMake presets:

```bash
# Clone repository
git clone https://github.com/archlab-sciencetokyo/SimRV.git
cd SimRV

# Build the normal RV64-capable release target
cmake --preset rv64-release
cmake --build --preset rv64-release -j$(nproc)

# Optional: build the strict-width RV32 verification target
cmake --preset rv32-release
cmake --build --preset rv32-release -j$(nproc)
```

The normal executable is `build/rv64-release/simrv`. It runs RV64 guests and RV32 guests; the
separate `build/rv32-release/simrv` target is primarily retained for strict-width validation.

### Choose a workflow

| Goal | Guide |
| --- | --- |
| Explore Linux with the Alpine/JWM desktop | [Full-System Linux](linux.md) |
| Build or boot a serial Buildroot guest | [Buildroot image workflow](linux.md#fpga-baseline-profile) |
| Build and validate SimRV from source | [Install and build](install.md#build-from-source) and [release validation workflow](ga-workflow.md) |
| Create a CPU preset or custom SoC device | [Extending CPU presets and MMIO platforms](../hardware/extending-platforms.md) |
| Compare architectural or cycle-level behavior | [Reference parity](../hardware/rtl_parity.md) |

### Installing System-Wide

```bash
# Install SimRV binary, tools, models, schemas, and documentation
sudo cmake --install build/rv64-release --prefix /usr/local
```

Installed files include:

- Executables: `/usr/local/bin/simrv`, `/usr/local/bin/simrv-cpu-wizard`, `/usr/local/bin/simrv-tune`, `/usr/local/bin/simrv-parity`, `/usr/local/bin/simrv-benchmark`
- CPU Model Templates: `/usr/local/share/SimRV/models/`
- JSON Schemas: `/usr/local/share/SimRV/schemas/`
- Documentation & Man Page: `/usr/local/share/doc/SimRV/USER_GUIDE.md`, `/usr/local/share/man/man1/simrv.1`

---

## 3. Command-Line Interface (CLI)

By default, launching `simrv` without arguments launches the interactive TUI workbench. For non-interactive batch scripts or automated testing, provide `--cli`.

### Common Invocations

```bash
# Run a bare-metal ELF binary in fast headless CLI mode
simrv --cli -m program.elf

# Run in cycle-accurate (CA) mode with step limit
simrv --cli --ca -m program.elf -s 1000000

# Load a custom CPU model preset
simrv --cli --ca --cpu-preset configs/models/rvcomp.cfg -m program.elf

# Launch TUI workbench with loaded binary
simrv -m program.elf
```

### Key CLI Options

| Option | Description |
| :--- | :--- |
| `-c, --cli` | Force headless command-line execution (disables TUI). |
| `--tui` | Force interactive TUI workbench (default). |
| `-m, --memory <file.elf>` | Load ELF executable into guest memory. |
| `--ca` | Select cycle-accurate pipeline simulation kernel. |
| `--ia` | Select fast instruction-accurate simulation mode. |
| `-s, -e, --steps <N>` | Evaluate machine-wide instruction limit across all harts before stopping. |
| `--cpu-preset <path.cfg>` | Load human-editable CPU microarchitecture preset. |
| `--isa <string>` | Select a supported base ISA preset; recognized ratified Z extensions may be listed with optional supported versions. |
| `-p, --pipeline <type>` | Pipeline microarchitecture target (`three-stage`, `five-stage`, `dual-issue`). |
| `-H, --tohost <addr>` | Specify physical address of `tohost` communication symbol for tests. |
| `--summary <file>` | Write a machine-readable JSON execution summary when the run ends. |
| `--events <file>` | Write newline-delimited lifecycle, interrupt, and DMA events for automation. |
| `--buildroot-output <dir>` | Load firmware, DTB, and root filesystem from a Buildroot output directory or bundle. |
| `--uart-transcript <file>` | Save guest UART output to a file while keeping the normal CLI, TUI, or server destination. |
| `--arch-trace <file>` | Write a versioned JSONL retirement trace (defaults to detailed mode unless an execution mode is explicitly selected). |
| `--trace-level <0-4>` | Limit architectural event detail: devices, calls, retirement, or detailed state. |
| `--trace-function <name>` | Filter attributed events to an exact ELF function symbol. |
| `--trace-device <name>` | Filter component-attributed events to an exact component name. |
| `--log-format <text|json|json-pretty>` | Select diagnostic log rendering; default is `text`. |
| `--log-file <file>` | Mirror diagnostic messages to a file in the selected log format. |
| `--trace` | Write an aligned, labeled architectural instruction trace to `<trace-dir>/trace.txt`. |
| `--tracepc` | Write PC stream trace to `<trace-dir>/tracepc.txt`. |
| `--gdb` | Start GDB Remote Serial Protocol (RSP) server. |
| `--gdb-port <port>` | Set GDB RSP TCP listener port (default: 1234). |
| `--isa-info` | Show the qualified ISA, vector, privilege, and debugger capability contract. |
| `--doctor` | Diagnose terminal presentation and portable-runtime conditions. |
| `-v, --version` | Display version and build information. |
| `-h, --help` | Show full command-line help message. |

`--log-format` controls simulator diagnostic messages only. JSON diagnostic output uses one record
per line; `json-pretty` is intended for interactive inspection. It does not change the independent
schemas or JSONL encoding of architectural trace artifacts.

CLI color is automatic for a capable terminal and can be disabled with `NO_COLOR` or
`TERM=dumb`; a nonzero `FORCE_COLOR` explicitly enables it. CPU-model validation paths are OSC 8
clickable links in terminals (and plain paths when output is redirected). TUI mode is selected
automatically only when SimRV owns an interactive foreground terminal; it has no separate startup
or shutdown banner. Warnings and errors remain visible in its message panel.

Runtime installs also provide Bash, Zsh, and Fish completions for common options, enum values, and
file/directory arguments. They are installed with the simulator, including in the portable runtime
archive.

### Automation summary

Use `--summary` when a script needs architectural results without parsing human-readable logs:

```bash
simrv --cli --baremetal -m program.elf --summary results/run.json
python3 -m json.tool results/run.json
```

The JSON document has schema version `1` and includes the simulator version, XLEN, hart count,
execution engine, stop reason, exit status, final PC, retired instructions, cycles, CPI, IPC,
per-hart retirement counts, cache hit/miss counts, branch prediction outcomes, and bus traffic.
The summary is written after execution;
a failure to write it
causes a nonzero simulator exit status. `--summary -` is rejected so guest UART output remains
unambiguous on stdout.

For streaming automation, `--events` writes one JSON object per machine lifecycle transition,
interrupt line edge, and DMA start, completion, or cancellation. Lifecycle records include status,
stop reason, retired instructions, and cycles; device events include their source or component and
event-specific fields. Like summaries, `--events -` is rejected because stdout belongs to guest UART
output.

For instruction-by-instruction architectural comparison, use the opt-in JSONL trace:

```bash
simrv --cli -m program.elf --arch-trace results/retire.jsonl --steps 10000
```

The first record identifies the trace schema, XLEN, VLEN, and hart count. Each subsequent
`retire` record contains the hart, cycle, cumulative retired count, instruction PC and encoding,
decoded operation, next PC, and privilege mode. The option selects detailed execution so every
committed instruction is represented; it is intended for reference-model parity work and is not a
throughput mode.

!!! warning "Tracing changes what a run measures"
    Architectural traces add serialization and file I/O, and full retirement tracing selects
    detailed execution. Use an untraced run for throughput measurements; use `--summary` when you
    only need aggregate counters.

Architectural tracing keeps `retire.jsonl` compact and compatibility-oriented. It does not repeat
the full CSR/vector snapshot on every line. Additional streams are written beside it:
`calls.jsonl`, `devices.jsonl`, `interrupts.jsonl`, `metadata.json`, and `events.jsonl` when
`--events` is not supplied.
An explicit `--events` path takes precedence. Schema-2 event streams use a common envelope with
`schema_version`, `event`, `cycle`, and `payload`; timestamps and attribution fields are present
where applicable. The optional `function` field contains the ELF symbol covering an attributed PC;
it is omitted when SimRV has no symbol for that PC. The optional `mode` field uses the architectural
privilege names `U`, `S`, and `M`. PC and address values are lowercase hexadecimal strings with a
`0x` prefix. The schema is
published at `schemas/trace-event.schema.json`. Schema 2 is SimRV's tooling envelope: it does not
claim conformance to RISC-V [E-Trace](https://docs.riscv.org/reference/e-trace/index.html),
[N-Trace](https://docs.riscv.org/reference/debug-trace-ras/nexus-trace/index.html), or the ratified
[Trace Control Interface](https://docs.riscv.org/reference/trace-control-interface/index.html),
which specify hardware trace encoding, transport, and component control rather than SimRV's JSONL
event protocol. Architectural fields and instruction conventions follow the RISC-V ISA and calling
convention. Lifecycle records include
`initialized`, `started`, `running`, `stopped`, `completed`, and `failed` where those transitions
occur; reboot and guest-exit requests have their own lifecycle events. `cycle` is the ordering key,
while ISO-8601 UTC timestamps correlate with host logs.

The default trace level is 3. Level 1 enables trap, exception, and device events; level 2 adds calls
and returns; level 3 adds instruction retirement; level 4 adds supported bus transactions and
committed scalar memory events. New streams use the schema-2 envelope with RISC-V privilege names
(`U`, `S`, and `M`).

Use filters to keep selected architectural streams bounded:

```sh
simrv --cli -m program.elf --arch-trace results/retire.jsonl --trace-level 4 \
  --trace-events call,return,mmio_write --trace-hart 0 --trace-function main \
  --trace-device uart0 --trace-pc-range 0x80010000-0x80020000 \
  --trace-after-cycle 10000 --trace-before-cycle 20000
```

`--trace-events` takes comma-separated event names. Hart and cycle bounds are inclusive; filters
are conjunctive. `--trace-function` uses the loaded ELF symbol table and matches the exact symbol
containing the event PC; events without a resolvable symbol are excluded. `--trace-device` matches
the event envelope's `component` exactly and excludes events without a component attribution. The
PC range is inclusive and accepts hexadecimal or decimal addresses. `--trace-pc START-END` is an
alias for `--trace-pc-range`. Use `--trace-pc-period N` to sample the PC trace; numeric values for
`--trace-pc` are rejected with a migration hint.
Events without an architectural PC (for example bus transactions and interrupt assertion edges) are
excluded while a PC range is active. Filtering occurs before event serialization. Lifecycle records
in `events.jsonl` are always retained, so filters cannot hide run completion or failure. Selected
filter values are copied into `metadata.json`. A hart filter excludes bus events without hart
attribution.

FP/vector destination changes can be recorded separately with `--trace-register-writes` (requires
`--arch-trace`). The opt-in `registers.jsonl` stream emits schema-version-1 `register_write` events;
FPR records preserve exact 64-bit register contents plus `fcsr`, while vector records preserve the
full destination register bits and include `VLEN`, `vl`, and `vtype`. Event filters also apply to
these records. Example:

```sh
simrv --cli -m vector-program.elf --arch-trace trace/retire.jsonl \\
  --trace-register-writes --trace-events register_write
```

Architectural names (`f0`–`f31`, `v0`–`v31`) are the canonical register identifiers. For example,
an FPR write may contain `before_bits: "0x0000000000000000"` and
`after_bits: "0x3ff0000000000000"`; vector bit strings cover the full VLEN-sized register.

Example device event:

```json
{"schema_version":2,"event":"mmio_write","timestamp":"2026-10-05T12:34:56.123Z",
 "cycle":18420,"hart":0,"pc":"0x800125bc","mode":"M","component":"uart0",
 "payload":{"address":"0x40001000","value":"0x1","width":1,"access":"mmio",
 "faulted":false,"latency_cycles":null}}
```

Calls are `jal`/`jalr` instructions that write architectural link register `x1` (`ra`) or the
alternate link register `x5`; returns are `jalr x0, 0(x1)` or `jalr x0, 0(x5)`, including
compressed aliases. SimRV records PCs and dynamic depth; downstream ELF-aware tools resolve symbols
and source locations:

```json
{"schema_version":2,"event":"call","timestamp":"2026-10-05T12:34:56.123Z",
 "cycle":42,"hart":0,"mode":"M","payload":{"source_pc":"0x80000000",
 "target_pc":"0x80000120","return_pc":"0x80000004","call_depth":1}}
{"schema_version":2,"event":"return","timestamp":"2026-10-05T12:34:56.124Z",
 "cycle":57,"hart":0,"mode":"M","payload":{"source_pc":"0x80000124",
 "target_pc":"0x80000004","call_depth":0}}
```

`metadata.json` records the simulator version and commit, the lowercase single-letter ISA string
derived from runtime `misa` for standard single-letter extensions, separately lists
`Xsimrvtrace` as a non-standard extension, XLEN, VLEN, hart count, trace level, registered runtime
MMIO devices and address ranges, the VirtIO RNG's fixed seed, guest ELF path and SHA-256, a SHA-256
configuration fingerprint, exact argument vector, host OS/release/machine, and start/completion timestamps. The
ISA string reflects `misa` and does not claim multi-letter `Z*` extension coverage. For `--isa`,
SimRV accepts `rv32g`/`rv64g` as the standard G shorthand for IMAFD plus `Zicsr` and `Zifencei`;
`rv32gc`/`rv64gc` add the separate C extension. SimRV also accepts `Zicsr`, `Zifencei`, and
`Zicntr` with supported ratified 2.0 version spellings; `Zicntr` must be listed separately. The
canonical `rv64g_zicntr2p0` spelling is accepted, as are case-insensitive extension names and
supported explicit `2.0` suffix variants. The schema-1
retirement header and records remain unchanged for existing trace consumers. ISA strings may attach
the first multi-letter extension directly to the base and separate subsequent extensions with
underscores, as specified by RISC-V naming conventions. Call events include
`source_pc`, `target_pc`, `return_pc`, and
post-transition `call_depth`;
return events include `source_pc`, the architectural destination as
`target_pc`, and post-transition `call_depth`.

At trace level 1, `interrupts.jsonl` records pending-state assertion/deassertion edges, architectural
interrupt delivery after trap state has been updated, and return when a matching `mret`, `sret`, or
`uret` retires. `cause` contains
the interrupt code; `cause_value` contains the XLEN-encoded cause value. Register-name fields
identify the applicable `mcause`/`scause`, `mepc`/`sepc`, and `mtval`/`stval` bank for delegated
traps. `pc` is the handler PC on entry, and `target_pc` is the resumed PC on return.

Schema-2 event catalog:

| Event | Stream | Event-specific fields |
|---|---|---|
| Lifecycle transitions | `events.jsonl` | `payload.status`, stop reason, retired instructions, cycles; failures may include an error and exits may include exit status |
| Interrupt assertion/deassertion and DMA start/completion/cancellation | `events.jsonl` | interrupt source/cause or DMA component, transfer ID, byte count, and modeled cycle fields |
| `call`, `return` | `calls.jsonl` | source/target PCs, call `return_pc`, dynamic call depth |
| MMIO and unmapped reads/writes | `devices.jsonl` | component; address, value, width, access, fault status, latency cycles (`null` when not modeled) |
| `dma_start`, `dma_complete`, `dma_cancelled` | `devices.jsonl` | scheduler transfer ID, source component, byte count, request/start/completion cycles, modeled latency |
| `trap`, `sbi` | `devices.jsonl` | cause and cause name, fault PC/tval/privilege or SBI extension/function/arguments |
| Interrupt assertion/deassertion | `interrupts.jsonl` | component, cause, cause name, asserted state |
| Interrupt entry/return | `interrupts.jsonl` | handler/resumed PC, cause, EPC, CSR bank/value, source mode, nesting depth |
| `bus_transaction` | `bus.jsonl` | bus, TileLink channel/opcode, protocol source/sink IDs, sequence, hart/master, target, address, transfer width, byte enables, data, request/completion cycles, modeled latency, response/error flags |
| `pipeline_stall` | `pipeline.jsonl` | cycle-accurate stages stalled that cycle, each stage's PC, remaining modeled latency, and stall reason |
| `memory_read`, `memory_write`, `memory_atomic`, `memory_fault` | `memory.jsonl` | effective/optional physical address, region/component, operation, width, values, fault/cause/tval, latency when modeled |
| `marker_begin`, `marker_end`, `marker` | `markers.jsonl` | PC, mode, label, marker depth |
| `header` | event streams | stream name |

Mapped MMIO failures and accesses to unmapped addresses are retained as device-stream records with
`faulted: true`; unmapped events use `access: "unmapped"` and `component: "unmapped"`. DMA scheduler
events use the component names `dma-controller`, `virtio-mmio-*`, or `virtio-pci-*`; each transfer's
start and completion share a run-local `transfer_id`. The request cycle is when the scheduler was
called, `start_cycle` accounts for queued transfers, and the completion event's envelope cycle is
the modeled completion cycle. These scheduler-level records intentionally do not invent source or
destination addresses, which are not supplied by all current DMA clients. The legacy
`dlog.txt` continues to include successful accesses only.

At level 4, `memory.jsonl` records successfully retired scalar loads, stores, and AMOs, plus precise
synchronous faults on scalar data accesses. Successful-event cycles are architectural retirement
cycles; `memory_fault` cycles are trap-detection cycles. `effective_address` is the guest effective address,
    and `physical_address` is included when the active translation is available in the hart's TLB.
`region` distinguishes `ram`, `mmio`, `unmapped`, and `unresolved`; MMIO records include the
registered component when identifiable and are also detailed in `devices.jsonl`. `faulted` is
false for retired accesses and true for faults; load-fault values are `null`, while a faulting store
includes its attempted value. Precise cause and `tval` are recorded on fault events, with the
corresponding trap also in `devices.jsonl`. `latency_cycles` is `null` until per-access architectural
latency is modeled.

`retire.jsonl` intentionally remains schema 1 for existing consumers. A companion
`<trace-stem>.index.jsonl` stores byte offsets every 256 emitted retirement records, keyed by
record ordinal, cycle, hart, PC, and event type. Consumers can binary-search the sparse anchors
for an approximate cycle/PC/hart location, seek the uncompressed retirement file to that byte
offset, then scan forward. Compression is opt-in: give `--arch-trace` a path ending in `.gz`
(for example, `retire.jsonl.gz`) to gzip the unchanged JSONL stream using the host's shared zlib
library. The stream is flushed at the same trace flush points; `.gz` traces do not get a byte-offset
index because compressed offsets are not seekable as JSONL byte positions. For example, use
`--arch-trace results/retire.jsonl.gz` to enable it. Without runtime zlib, compressed tracing is
unavailable and SimRV reports an error. Other event streams remain plain JSONL. Header and
index-entry records are defined by `schemas/trace-index.schema.json`. `bus.jsonl` and
`pipeline.jsonl` are created beside the other architectural streams at trace level 4. The bus stream
records TileLink-C channel observations. The pipeline stream is emitted only by the cycle-accurate
engine and contains a compact per-cycle list of stalled stages, independent of TUI execution-detail
capture. A representative event is:

```json
{"schema_version":2,"event":"pipeline_stall","cycle":18420,"hart":0,"mode":"M",
 "payload":{"stages":[{"stage":"fetch","pc":"0x800125bc","remaining_cycles":3,
 "reason":"instruction_fill"}]}}
```

Stall reasons distinguish stage latency, data hazards, instruction fills, page walks, data
transfers, and downstream backpressure. Configured filters are applied before writing the record.
`source_id` and `sink_id` are protocol identifiers, not hart IDs; response-channel records carry
their own cycle and are correlated with requests by TileLink source ID.
TileLink A-channel records include the request payload and `request_cycle`; corresponding D-channel
records use the modeled completion as the envelope `cycle`, repeat the request context, and include
`completion_cycle`, `latency_cycles`, `response_data`, and denied/corrupt flags. `master` names the
issuing hart and instruction/data port; `target` is the
registered device name or `ram`/`unmapped`. TileLink transfer `size_log2` is accompanied by the
derived `width_bytes`; `burst_length` is measured in 8-byte TileLink beats. Synchronous paths may
have zero modeled latency; cycle-accurate paths report the configured interconnect latency.

#### Guest software annotations

SimRV implements the write-command CSR `0x800` from the RISC-V custom U-level read/write range.
This address allocation follows the [RISC-V Privileged Architecture CSR address mapping
conventions](https://docs.riscv.org/reference/isa/priv/priv-csrs.html). It is accessed with
standard Zicsr instructions and does not occupy a standard CSR address. This is a SimRV-specific,
non-standard facility named `Xsimrvtrace`; it is not a ratified RISC-V extension.
The CSR is inert when architectural tracing is disabled. Guest labels are sent as UTF-8 bytes,
followed by one command write, so the simulator does not dereference guest pointers or translate
guest virtual addresses:

```c
#include <simrv/trace.h>

simrv_trace_begin("decode");
simrv_marker("entered decoder");
simrv_trace_end("decode");
```

The equivalent assembly macros are in `simrv/trace.S` and take a NUL-terminated string pointer in
an integer register, for example `SIMRV_TRACE_BEGIN a0`. Labels are capped at 1024 bytes per event.
Events include hart, cycle, source PC, privilege mode, label, and post-transition marker nesting
depth. `marker_end` decrements depth only when nonzero.

### Checkpoint and resume

Save a versioned architectural snapshot when a run ends and resume it with the same machine
configuration:

```bash
simrv --cli -m program.elf --save-checkpoint run.ckpt --steps 1000000
simrv --cli -m program.elf --load-checkpoint run.ckpt --steps 1000000
```

Snapshots contain guest DRAM, hart registers and CSRs, retirement counters, and `mcycle`. The
XLEN, VLEN, hart count, and DRAM geometry must match. Device queues, host sockets, and external
time are intentionally not serialized; use this for deterministic bare-metal and architectural
experiments, not transparent VM migration.

Periodic snapshots can be requested with `--checkpoint-every <cycles> --checkpoint-dir <dir>`.
SimRV writes `checkpoint-<cycle>.ckpt` and a JSON sidecar containing the cycle, per-hart PC,
privilege and pending-interrupt bitmap, RAM/checkpoint SHA-256 hashes, and explicit completeness flags. If a generated name
already exists, a numeric suffix is added rather than overwriting it. Snapshot points occur at
the first safe simulator boundary at or after each requested interval. Device state is not
included, and `trace_sequence` is currently `null`; these files are architectural snapshots, not
replay-complete checkpoints.

GDB debugger sessions are not checkpointed: network connections, breakpoints, RSP negotiation state,
and debugger-side register caches are not serialized. To debug a resumed snapshot, launch SimRV with
`--load-checkpoint run.ckpt --gdb --cli`, reconnect GDB, and reapply breakpoints. The restored guest
PC and architectural register state are visible on connection; debugger protocol state starts fresh.

---

## 4. Interactive TUI Workbench

The SimRV TUI provides an educational visual inspection environment for architecture students and hardware engineers.

```
┌─ SimRV 3.0 ─────────────────────────────────────────── [Hart 0: RUNNING] ─┐
│ [Regs] [Cache] [TLB] [Pipeline] [Hazards] [Explainer] │ Guest Terminal PTY │
│                                                      │                    │
│ PC: 0x80000000   ra: 0x00000000   sp: 0x80010000     │ Linux version 7.2  │
│ IF: [0x80000020] addi a0, a0, 1                      │ buildroot login:   │
│ ID: [0x8000001c] lw   a1, 0(sp)                      │                    │
│ EX: [0x80000018] mul  a2, a1, a0                     │                    │
│ MEM:[0x80000014] sw   s0, 4(sp)                      │                    │
│ WB: [0x80000010] addi sp, sp, -16                    │                    │
├──────────────────────────────────────────────────────┴────────────────────┤
│ [F1:Help] [F2:Load] [F5:Run] [F6:Step] [F8:Break] [Alt-M:MISA] [F4:Presets]
└───────────────────────────────────────────────────────────────────────────┘
```

### Focus & Input Navigation

- **Input Focus**: Keyboard input is automatically linked to simulation state: when running, input routes directly to the guest terminal (PTY / UART); when paused, keystrokes control TUI navigation and inspector panels.
- **Paste to UART**: Terminal bracketed paste is delivered to the running guest as UART input.
- **Search UART scrollback**: Pause and focus the console pane, press `Ctrl-F` (or `/`), enter a query, then use `n` / `N` to move between matches. Press `Esc` to cancel search input.
- **Load files**: In binary, disk, and CPU model path dialogs, use `Tab` to complete the shared path prefix, or `Ctrl-O` to browse and filter files. `Shift-Tab` in the binary dialog cycles bare-metal, OS, and Buildroot-folder modes.
- **`[Tab]` / `[Shift-Tab]`**: Switch active sub-views within the left inspector pane.
- **`[F5]` / `[c]` / `[Ctrl-P]`**: Run / Pause simulation execution.
- **`[F6]` / `[s]`**: Step one cycle machine-wide across all harts.
- **`[q]` / `[Ctrl-C]`**: Quit simulator.

!!! info "Pause to use TUI navigation"
    While the guest is running, regular keystrokes go to its UART. Pause with `Ctrl-P` before using
    navigation keys or inspecting state; resume with the same key when ready.

The TUI also saves the latest workbench layout and supported preferences across launches. The
complete key map, file-browser controls, and preference path are in the [TUI guide](tui.md).

For CPU preset authoring, custom MMIO devices, traceable dummy mappings, and the machine event
observer API, see [Extending CPU Presets and MMIO Platforms](../hardware/extending-platforms.md).
For building and loading Linux images from Buildroot, see [Linux and Buildroot](linux.md).

When the terminal advertises OSC 22 pointer-shape support, the TUI changes the pointer to a
clickable-hand shape over interactive controls and restores the prior pointer shape when it exits.
Terminals without OSC 22 support keep their normal pointer behavior; no notifications or terminal
theme colors are changed.

### Visualizer Panes

1. **Registers (`Regs`)**: Real-time display of integer registers (`x0`–`x31`), floating-point registers (`f0`–`f31`), and vector registers (`v0`–`v31`).
2. **Pipeline View**: In-flight stage slots (`IF`, `ID`, `EX`, `MEM`, `WB`) annotated with instruction mnemonic, data hazard stalls, and branch mispredict bubbles.
3. **Cache Inspector**: Multi-way cache visualization for ICache and DCache, showing set indices, tags, hit/miss markers, and MESI line state flags (`[M]`, `[E]`, `[S]`, `[I]`).
4. **Instruction Explainer**: Architectural breakdown of the current instruction, explaining bitfields, immediate decoding, register operands, IEEE-754 FP flags, or vector group configurations.
5. **Memory & Stack**: Guest memory hex viewer and call stack frame resolution.

---

## 5. Bare-Metal & Embedded Simulation

SimRV can execute freestanding bare-metal ELFs compiled with standard RISC-V GCC or Clang toolchains.

```bash
# Run baremetal ELF with standard tohost exit reporting
simrv --cli -m build/my_program.elf -H 0x80001000
```

### Hardware Peripherals Emulated

- **16550A UART**: Serial I/O mapped at base address `0x10000000`.
- **CLINT**: Core Local Interruptor at base address `0x02000000` providing `mtime` and
  `mtimecmp` registers (10 MHz timebase). In cycle-accurate mode each timer tick corresponds to 16
  cycles of the advertised 160 MHz CPU clock; instruction-accurate modes expose deterministic virtual
  time rather than predicted hardware time.
- **PLIC**: Platform-Level Interrupt Controller at base address `0x0c000000` supporting priority thresholds and interrupt claims.

---

## 6. Full-System Linux Emulation

SimRV boots full Linux distributions with OpenSBI and dynamic device-tree generation.

```bash
# Launch the bundled OpenSBI + Linux image with its root filesystem
simrv --os \
      -m linux-images/rv64/alpine-gui/opensbi-linux-payload.elf \
      -D linux-images/rv64/alpine-gui/rootfs.img \
      -f linux-images/rv64/alpine-gui/devicetree.dtb \
      --smp 2
```

### Full-System CLI Options

- `-m <opensbi-linux-payload.elf>`: OpenSBI firmware containing the Linux kernel.
- `-D <rootfs.img>`: Ext4 root filesystem image (attached as `/dev/vda` via VirtIO Block). Guest
  writes use a temporary overlay and are discarded when SimRV exits; the source image is unchanged.
- `--virtiofs <DIR>`: Expose a selected host directory to Linux guests through VirtIO-FS. Guest
  writes are immediately visible in that host directory; see
  [Linux and Buildroot](linux.md#share-a-host-directory-with-virtio-fs).
- `--smp <2..16>`: Number of active SMP harts (default is 1 for single-hart execution).
- `--dtb <virt.dtb>`: Optional custom Device Tree Blob (SimRV automatically synthesizes a device tree if omitted).
- `--dram-size <SIZE>`, `--ram-size <SIZE>`: Guest DRAM size with an optional
  scaled suffix such as `128M` or `2G` (official release default: 256 MiB).

---

## 7. CPU Model Presets & Microarchitecture Tuning

SimRV allows defining custom processor pipelines and timing parameters in human-editable `.cfg` files.

### Interactive Model Wizard (`simrv-cpu-wizard`)

Create and customize microarchitectural models interactively:

```bash
# Run interactive CLI wizard
simrv-cpu-wizard

# Generate from a template directly
simrv-cpu-wizard --template rvcomp --name my_core --output configs/models/my_core.cfg

# Validate configuration syntax against schema
simrv-cpu-wizard --validate configs/models/my_core.cfg
```

### Automatic Model Calibration (`simrv-tune`)

Automatically calibrate pipeline execution latencies, branch penalties, and cache configurations against RTL or hardware execution traces:

```bash
simrv-tune --base-config configs/models/rvcomp.cfg --apply
```

---

## 8. Reference Parity Verification

SimRV can compare architectural retirement against an ISA reference model and compare modeled
cycles against cycle-producing implementations or measured hardware traces. These are separate
checks: architectural references such as Spike establish instruction-level equivalence, while a
cycle-accurate target or captured trace can establish timing parity for a configured CPU model.

The `simrv-parity` runner currently supplies adapters for the RVComp and CFU Proving Ground RTL
targets. Its trace comparison and adapter contract can also be reused when adding other
cycle-producing references; architectural lockstep with Spike is available separately through
`--spike` and the release gates.

```bash
# List targets supported by the current cycle-parity runner
simrv-parity --list-targets

# Compare cycle traces against CFU Proving Ground RVProc
simrv-parity cfu-pg --benchmark all

# Compare cycle traces against RVComp
simrv-parity rvcomp --quick
```

For the adapter contract, parity levels, and guidance for adding another reference target, see
[Reference Parity Verification](../hardware/rtl_parity.md).

---

## 9. Benchmarking Suite

The `simrv-benchmark` suite measures simulation throughput (KIPS/MIPS), memory footprint (RSS), and compares results against Spike or previous SimRV versions.

```bash
# Run standard real-world benchmarks
simrv-benchmark --suite realworld

# Compare performance against Spike
simrv-benchmark --suite realworld --spike $(which spike) --output results.json

# Generate LaTeX performance comparison table
simrv-benchmark --suite realworld --latex-table
```

---

## 10. Troubleshooting & Reference

### Common Questions

- **Q: How does keyboard input routing work between Linux and the TUI?**
  *A:* Keyboard input routes automatically based on execution state: while running, keystrokes are delivered directly to the guest terminal; while paused (via `F5` or `Ctrl-P`), keystrokes control simulator inspection and navigation.

- **Q: Why does headless CLI mode finish without displaying the TUI?**
  *A:* The `--cli` flag enforces non-interactive headless operation. To see the graphical TUI workbench, run `simrv` without `--cli`.

- **Q: Where are instruction traces written when passing `--trace`?**
  *A:* Traces are saved in the `trace/` directory relative to your working directory by default.
  Use `--trace-dir DIR` to select another root. The `trace.txt` file presents labeled, aligned
  cycle, instruction, register, and CSR fields; periodic PC samples remain in `tracepc.txt`.

- **Q: How can I connect GDB to debug a running binary?**
  *A:* Launch SimRV with `--gdb --cli -m program.elf`, then in another terminal run:
  `riscv64-unknown-elf-gdb program.elf -ex "target remote :1234"`

### Detailed Architecture & Extension Guides

- [System Architecture](../architecture/overview.md)
- [Bare-Metal Guide](baremetal.md)
- [CPU Model Configuration Reference](../hardware/models.md)
- [RTL Parity Verification Guide](../hardware/rtl_parity.md)
- [TileLink-C Profile & Cache Coherence](../architecture/tilelink.md)
- [RISC-V Compliance Scope](../architecture/compliance.md)
