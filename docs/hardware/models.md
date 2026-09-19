# CPU Model Configuration Guide

SimRV provides a microarchitectural CPU modeling framework that enables cycle-accurate (CA) simulation calibrated to real hardware, FPGA soft cores, and SystemVerilog RTL designs. Microarchitectural definitions are decoupled from the simulator binary and managed via human-readable, sectioned `.cfg` files.

---

## 1. Quick Start

### Using a Built-In Preset or Model File

SimRV automatically resolves CPU models by profile name or direct file path:

```bash
# Run with canonical pre-installed model profile
SimRV --ca --cpu-profile rvcomp -m program.elf

# Run with an explicit custom configuration file
SimRV --ca --cpu-profile configs/models/my_core.cfg -m program.elf
# or equivalently:
SimRV --ca --cpu-model-file configs/models/my_core.cfg -m program.elf
```

### Generating a New Model Configuration

#### Option A: Interactive CLI Wizard

SimRV includes an interactive questionnaire tool that guides you through tailoring latencies, predictors, and caches:

```bash
# Interactive wizard
python3 scripts/cpu_model_wizard.py --name custom_riscv

# Scaffold non-interactively from an existing template
python3 scripts/cpu_model_wizard.py --template rvcomp --name rvcomp_tuned --output configs/models/rvcomp_tuned.cfg --non-interactive
```

#### Option B: Simulator Scaffolding Flags

Generate an annotated `.cfg` configuration directly from the `SimRV` executable:

```bash
# Dump an existing profile to stdout or a file
SimRV --dump-cpu-model rvcomp configs/models/rvcomp_copy.cfg

# Scaffold a default balanced template
SimRV --scaffold-cpu-model configs/models/new_core.cfg
```

#### Option C: In the Interactive TUI

1. Launch SimRV in cycle-accurate mode (`SimRV --ca -m program.elf`).
2. Press `F12` or open Settings (`[3]` Microarchitecture tab).
3. Adjust pipeline latencies, predictor capacities, or forwarding rules with arrows or number keys.
4. Press `[S]` or select **Save configuration** to open the **Save CPU Model Configuration** modal.
5. Enter your preferred filename/path (e.g. `configs/models/my_experiment.cfg`) and press `Enter`.

---

## 2. Configuration Folder Convention & Lookup Hierarchy

SimRV looks for CPU model configuration files in the canonical `configs/models/` directory. When `--cpu-profile <TARGET>` is supplied:

1. **Direct Path**: If `<TARGET>` is a valid file path, it is loaded immediately.
2. **Path with Extension**: If `<TARGET>.cfg` exists, it is loaded.
3. **Canonical Project Folder**: SimRV checks `./configs/models/<TARGET>.cfg`.
4. **Environment Variable**: SimRV checks `$SIMRV_CONFIG_DIR/models/<TARGET>.cfg` or `$SIMRV_CONFIG_DIR/<TARGET>.cfg`.
5. **Built-In Fallback**: If no file is found, SimRV falls back to minimal compiled generic defaults (`tiny`, `balanced`, `performance`).

---

## 3. Configuration Format Reference (`.cfg`)

SimRV CPU model configurations follow a clean INI/TOML sectioned format with support for comments (`#` and `;`), unquoted numbers/booleans, and quoted strings.

### Sample Configuration (`configs/models/rvcomp.cfg`)

```ini
# SimRV CPU Model Configuration: Archlab RVComp
[cpu]
name = "rvcomp"
description = "Archlab RVComp 5-stage educational RISC-V processor"
xlen = 32
misa = "ima"

[pipeline]
type = "five-stage"
enable_forwarding = true
mul_latency = 2
div_latency = 35
fp_alu_latency = 4
fp_div_latency = 16
branch_mispredict_penalty = 4
cycle_counter_start_delay = 0
csr_flush_penalty = 3
fence_flush_penalty = 4

[branch_predictor]
type = "bimodal"
btb_entries = 512
bht_entries = 8192
ras_entries = 16
ghr_bits = 10
pc_shift = 2
enable_btb = true
enable_ras = false
untagged_btb = true
predict_non_control = true
registered_btb_read = false
bht_initial_state = 1

[instruction_cache]
capacity_bytes = 16384
associativity = 1
line_bytes = 32
hit_latency = 1
miss_latency = 1

[data_cache]
capacity_bytes = 16384
associativity = 1
line_bytes = 32
hit_latency = 4
miss_latency = 1

[interconnect]
request_latency = 1
response_latency = 1
```

---

## 4. Parameter Dictionary

### `[cpu]` Section

| Parameter | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `name` | string | `"custom"` | Identifier for the CPU model profile. |
| `description` | string | `""` | Human-readable documentation of the core architecture. |
| `xlen` | integer | `0` | Supported machine XLEN: `32` (RV32-only), `64` (RV64-only), or `0` (supports both RV32 and RV64 targets). Verified on startup against simulator build. |
| `misa` | string | `"gcbv"` | Target ISA extension profile: `gcbv`, `imac`, `ima`, `gc`, `im`, `i`. Setting extension names without `rv32`/`rv64` prefix allows the model to support both 32-bit and 64-bit simulator targets. Explicit `rv32*` and `rv64*` targets are also accepted. |

### `[pipeline]` Section

| Parameter | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `type` | string | `"five-stage"` | Pipeline structure: `"five-stage"` or `"three-stage"`. |
| `enable_forwarding` | bool | `true` | Enables EX-to-EX and MEM-to-EX operand bypass forwarding. |
| `mul_latency` | uint | `3` | Execution latency of integer multiplication (`MUL`, `MULH`, etc.) in clock cycles. |
| `div_latency` | uint | `18` | Execution latency of integer division and remainder (`DIV`, `REM`) in clock cycles. |
| `fp_alu_latency` | uint | `4` | Latency of single/double-precision floating-point additions and multiplications. |
| `fp_div_latency` | uint | `16` | Latency of floating-point division and square root operations. |
| `branch_mispredict_penalty` | uint | `3` | Recovery flush penalty in cycles when a branch is mispredicted. |
| `cycle_counter_start_delay` | uint | `0` | Delay in clock cycles before the `mcycle` counter starts incrementing after reset deassertion. |
| `host_interface_latency` | uint | `0` | Completion latency for cycle-mode writes to the HTIF/tohost interface. Zero publishes the write immediately. |
| `host_interface_phase_period` | uint | `0` | Optional transport phase period. A write issued at phase `p` adds `p` cycles, modeling a phase-aligned host interface. |
| `csr_flush_penalty` | uint | `3` | Pipeline stall/drain penalty when executing serializing CSR instructions. |
| `fence_flush_penalty` | uint | `4` | Pipeline stall/drain penalty when executing `FENCE` or `FENCE.I`. |

### `[branch_predictor]` Section

| Parameter | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `type` | string | `"bimodal"` | Prediction algorithm: `"none"`, `"static"`, `"bimodal"`, `"gshare"`, `"tournament"`. |
| `btb_entries` | uint | `256` | Number of Branch Target Buffer (BTB) cache entries (power of 2). |
| `bht_entries` | uint | `1024` | Number of 2-bit saturating counters in the Branch History Table (PHT/BHT). |
| `ras_entries` | uint | `16` | Return Address Stack depth for function call/return acceleration. |
| `ghr_bits` | uint | `10` | Global History Register bitwidth used in `gshare` hashing. |
| `pc_shift` | uint | `1` | Shift applied to branch PC before indexing tables (1 for RVC 16-bit instructions, 2 for 32-bit aligned instructions). |
| `enable_btb` | bool | `true` | Enables branch target caching. |
| `enable_ras` | bool | `true` | Enables Return Address Stack. |
| `untagged_btb` | bool | `false` | When true, BTB entries omit tag checks, indexing solely via lower PC bits. |
| `predict_non_control` | bool | `false` | When true, an untagged BTB may redirect a non-control instruction on an aliased taken entry, matching predictors that access every fetch PC. |
| `jump_uses_direction_counter` | bool | `false` | Require the BTB direction counter to predict a direct jump taken. Use this for unified BTB/PHT designs that do not special-case `JAL`. |
| `jump_uses_current_btb` | bool | `false` | Use the current-PC BTB entry for direct jumps even when conditional predictions consume a registered BTB output. |
| `registered_btb_read` | bool | `false` | Set to true if BTB output takes 1 cycle to latch, inserting a 1-cycle bubble on predicted taken branches. |
| `bht_initial_state` | uint | `0` | Initial 2-bit counter value: `0` (Strongly Not Taken), `1` (Weakly Not Taken), `2` (Weakly Taken), `3` (Strongly Taken). |

### `[instruction_cache]` and `[data_cache]` Sections

| Parameter | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `capacity_bytes` | uint | `4096` | Total cache capacity in bytes (e.g. `16384` for 16 KiB). |
| `associativity` | uint | `1` | Cache set associativity (`1` for direct-mapped, `2`, `4`, etc.). |
| `line_bytes` | uint | `32` | Cache line width in bytes (must match bus width, standard 32 bytes). |
| `hit_latency` | uint | `1` | Cache access latency on hit in clock cycles. |
| `miss_latency` | uint | `10` | Penalty added on cache miss during line fill request. |

An optional `[instruction_front_cache]` section adds a tag-only timing level in
front of the coherent instruction cache. It accepts `capacity_bytes`,
`associativity`, `line_bytes`, `hit_latency`, `refill_latency`, and
`backing_refill_latency`. The underlying timing engine supports ordered inclusive
levels with independent geometry and latency, while architectural data remains
owned by the coherent cache. `startup_refill_latencies` may contain a quoted,
comma-separated latency sequence for simulations that begin with partially warm
hardware state. `freeze_pipeline_on_refill` models blocking in-order frontends
whose outstanding fetch prevents every pipeline stage from advancing. This
permits small L0 lines and frontend-owned request timing without changing the
coherent fabric's transfer size.

### `[interconnect]` Section

| Parameter | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `request_latency` | uint | `1` | Latency from CPU master issue to crossbar/device request arrival. |
| `response_latency` | uint | `1` | Latency for slave response/acknowledgment transfer back to CPU. |
| `data_request_latency` | uint | `0` | Optional data-port request override; zero inherits `request_latency`. |
| `data_response_latency` | uint | `0` | Optional data-port response override; zero inherits `response_latency`. |
| `startup_data_response_latency` | uint | `0` | Optional response latency for the first data-port transaction after reset; later responses use `data_response_latency`. |

---

## 5. Built-In Generic Presets vs. Model Configs

SimRV distinguishes between **compiled-in generic presets** and **hardware-specific `.cfg` models**:

### Compiled-In Generic Presets

Generic presets are compiled directly into the simulator runtime so no external files are required, keeping the project and configuration directories clean:

1. **`balanced`** (default): Standard 5-stage core with bimodal branch prediction, integer forwarding, and balanced 4 KiB 2-way L1 caches. Works for both RV32 and RV64.
2. **`tiny`**: Compact 3-stage in-order embedded microcontroller profile without operand forwarding and with static branch prediction. Works for both RV32 and RV64.
3. **`performance`**: Aggressive 5-stage core with 4096-entry GShare predictor, low execution penalties, and 16 KiB 4-way L1 caches. Works for both RV32 and RV64.

To scaffold or inspect any generic preset as an editable `.cfg`, use:

```bash
SimRV --dump-cpu-model balanced my_balanced.cfg
```

### Hardware-Specific Models (`configs/models/`)

The `configs/models/` folder contains authoritative configurations calibrated to specific hardware/FPGA processor RTL:

1. **`rvcomp.cfg`**: Calibrated to the Archlab RVComp 5-stage SystemVerilog processor (`xlen = 32`, `misa = "ima"`). Features a 34-stall-cycle non-restoring divider (`div_latency = 35` because SimRV includes the issue cycle), 2-cycle multiplier, untagged 512-entry BTB, 8192-entry BHT with weak-not-taken reset state (`2'b01`), 4-cycle branch mispredict penalty, and 4-cycle L1 D-Cache hit latency.
2. **`cfu-provingground.cfg`**: Calibrated to Tokyo Tech Archlab's CFU-ProvingGround FPGA core (RVProc, `xlen = 32`, `misa = "im"`). Features registered BTB reads (1-cycle branch prediction bubble), reset counter delay of 2 cycles, and custom function unit (CFU) hardware interface.

---

## 6. Case Study: Calibrating SimRV to RTL (RVComp)

To calibrate SimRV to an external RTL core:

1. **Identify Hardware Latencies**:
   - Inspect RTL modules (`multiplier.v`, `divider.v`, `lsu.v`).
   - For example, RVComp's non-restoring divider takes 34 clock cycles, and its multiplier takes 2 stall cycles.
2. **Inspect Branch Predictor Microarchitecture**:
   - Check reset initialization in `bimodal.v`: RVComp initializes `pht[i] = 2'b01;` (`bht_initial_state = 1`).
   - Check BTB read timing: RVComp's `bpu_access_pc` is speculative lookahead (`r_pc + 4`), allowing 0-bubble predicted branch fetches (`registered_btb_read = false`).
3. **Verify via Evaluation Tooling**:

   ```bash
   python3 scripts/evaluate_rvcomp.py --rvcomp-dir ../RVComp \
     --trace-dir build/rvcomp_traces
   ```

   Compare instruction counts, measured RTL `mcycle`, derived component estimates, and cache stalls
   against Verilator simulation logs. The evaluator reports measured and estimated cycle deltas
   separately; only a zero measured-cycle delta is exact parity.

   The evaluator uses the RISC-V tools already on `PATH`; it does not assume a
   site-specific toolchain directory. `--rvcomp-bin` can select a prebuilt RTL
   simulator when the default `../RVComp/obj_dir/rvcom` is not appropriate.

4. **Automated Microarchitecture Calibration Optimizer**:

   SimRV provides `scripts/tune_cpu_model.py` to systematically calibrate any CPU `.cfg` against Verilator RTL logs across an evaluation benchmark suite:

   ```bash
   python3 scripts/tune_cpu_model.py --base-config configs/models/rvcomp.cfg --apply
   ```

   The tuner performs multi-parameter coordinate descent across interconnect latencies, cache
   capacities, and hit/miss timing, optimizing Mean Absolute Percentage Error (MAPE) against the
   measured RTL `mcycle`. The current hierarchy-aware configuration measures **0.96% MAPE** across the five
   evaluation kernels. The previously reported **2.39%** used the RTL testbench's derived
   `minstret + stall counters` estimate, which is consistently 33 cycles above measured `mcycle`
   for these kernels and must not be presented as exact cycle parity.
