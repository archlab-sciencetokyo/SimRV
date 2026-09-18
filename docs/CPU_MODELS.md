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
5. **Built-In Fallback**: If no file is found, SimRV falls back to minimal compiled defaults (`tiny`, `balanced`, `performance`, `cfu-provingground`, `rvcomp`).

---

## 3. Configuration Format Reference (`.cfg`)

SimRV CPU model configurations follow a clean INI/TOML sectioned format with support for comments (`#` and `;`), unquoted numbers/booleans, and quoted strings.

### Sample Configuration (`configs/models/rvcomp.cfg`)

```ini
# SimRV CPU Model Configuration: Archlab RVComp
[cpu]
name = "rvcomp"
description = "Archlab RVComp 5-stage educational RISC-V processor"
misa = "rv32ima"

[pipeline]
type = "five-stage"
enable_forwarding = true
mul_latency = 2
div_latency = 34
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
| `misa` | string | `"rv32gcbv"` | Target ISA profile: `rv32i`, `rv32im`, `rv32ima`, `rv32imac`, `rv32gc`, `rv32gcbv` (or 64-bit variants). |

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

### `[interconnect]` Section

| Parameter | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `request_latency` | uint | `1` | Latency from CPU master issue to crossbar/device request arrival. |
| `response_latency` | uint | `1` | Latency for slave response/acknowledgment transfer back to CPU. |

---

## 5. Canonical Included Presets

SimRV ships with five authoritative presets located in `configs/models/`:

1. **`rvcomp.cfg`**: Calibrated to the Archlab RVComp 5-stage SystemVerilog processor. Features 34-cycle non-restoring divider, 2-cycle multiplier, untagged 512-entry BTB, 8192-entry BHT with weak-not-taken reset state (`2'b01`), 4-cycle branch mispredict penalty, and 4-cycle L1 D-Cache hit latency.
2. **`cfu-provingground.cfg`**: Calibrated to Tokyo Tech Archlab's CFU-ProvingGround FPGA core (RVProc). Features registered BTB reads (1-cycle branch prediction bubble), reset counter delay of 2 cycles, and custom function unit (CFU) hardware interface.
3. **`tiny.cfg`**: Compact 3-stage in-order embedded microcontroller profile without operand forwarding and with static branch prediction.
4. **`balanced.cfg`**: Standard 5-stage core with bimodal branch prediction, integer forwarding, and balanced 4 KiB 2-way L1 caches.
5. **`performance.cfg`**: Aggressive 5-stage core with 4096-entry GShare predictor, low execution penalties, and 16 KiB 4-way L1 caches.

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
   python3 scripts/evaluate_rvcomp.py
   ```

   Compare instruction counts, core cycle estimates, and cache stalls against Verilator simulation logs.
