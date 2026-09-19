# Reviewer & Artifact Evaluation Guide

This guide is intended for artifact evaluation reviewers, conference referees, and research evaluators seeking to independently inspect, benchmark, and reproduce the claims and results of **SimRV 3.0**.

---

## 1. Executive Summary of Research Claims

SimRV provides:

1. **High-Throughput Simulation Speed**:
   - **Functional Fast Mode**: 100 – 125 MIPS with full VirtIO device and SMP Linux support.
   - **Cycle-Accurate Mode**: 3.0 – 6.0 MIPS modeling 5-stage pipelines, scoreboards, and cache hierarchies—**5× to 15× faster than gem5 Minor/In-Order**.
2. **Hardware RTL Parity**:
   - Validated cycle-by-cycle retirement trace parity against physical Verilog hardware (CFU-Proving-Ground and Archlab RVComp) via Verilator at **20× to 50× faster execution speeds**.
3. **Full-System SMP Linux Emulation**:
   - Boots un-modified OpenSBI and Linux kernels dynamically across **2 to 16 cores** (`--smp <N>`), generating the Flattened Device Tree topology, CLINT timers, and PLIC interrupt controllers dynamically.
4. **Dual-Architecture Conformance**:
   - Strict compile-time fixed dual-target qualification for both **RV32GCBV** and **RV64GCBV** with Physical Memory Protection (PMP) and Sv32/Sv39 virtual memory translation.

---

## 2. Reviewer Reproduction Checklist

### Step 1: Clean Build Verification

Validate that both 64-bit and 32-bit simulator targets configure and compile cleanly with modern C++23:

```bash
# RV64 Release Build
cmake --preset rv64-release
cmake --build --preset rv64-release -j$(nproc)

# RV32 Release Build
cmake --preset rv32-release
cmake --build --preset rv32-release -j$(nproc)
```

### Step 2: Automated Release Qualification Gates

SimRV enforces a 40-test qualification gate across both architectures. Run the dual gates:

```bash
# Run 40/40 RV64 gate tests
ctest --test-dir build/rv64-release --output-on-failure -L gate

# Run 40/40 RV32 gate tests
ctest --test-dir build/rv32-release --output-on-failure -L gate

# Run release metadata conformance check
python3 scripts/release_check.py --binary build/rv64-release/SimRV
```

### Step 3: Full Clean-Checkout Reproducibility Suite

To execute the automated end-to-end reproducibility workflow that verifies clean checkouts, license audits, architectural boundaries, and package consumers:

```bash
python3 scripts/reproduce.py --mode full --output repro/results
```

### Step 4: Hardware RTL Parity Verification

SimRV includes automated evaluation adapters comparing physical RTL designs against SimRV cycle-accurate models:

```bash
# List available RTL parity targets
python3 scripts/evaluate_rtl_parity.py --list-targets
```

To run parity verification against a local checkout of CFU-Proving-Ground or RVComp:

```bash
python3 scripts/evaluate_rtl_parity.py cfu-pg \
  --cfu-dir ../CFU-Proving-Ground \
  --simrv-bin build/rv32-release/SimRV \
  --trace-dir build/cfu_pg_parity_traces
```

### Step 5: Multi-Hart SMP Linux Boot Verification

Verify that dynamic multi-core Linux boots cleanly and concurrently executes across multiple harts:

```bash
# Boot 2-hart SMP Linux for 20 million instructions
./build/rv64-release/SimRV --cli --smp 2 \
  -m linux-images/rv64/fw_payload.bin \
  --dtb dynamic \
  -D linux-images/rv64/root.img \
  -s 20000000

# Boot 4-hart SMP Linux for 20 million instructions
./build/rv64-release/SimRV --cli --smp 4 \
  -m linux-images/rv64/fw_payload.bin \
  --dtb dynamic \
  -D linux-images/rv64/root.img \
  -s 20000000
```

Notice that instructions retire evenly across harts (e.g. 5.00M per core for 4 harts) and simulation speed exceeds **40 MIPS**.

---

## 3. Simulator Performance Comparison

The following table summarizes empirical simulation throughput measured on an x86_64 host (Intel Core i9 / AMD Zen 4):

| Simulator | Simulation Fidelity | Host Throughput (MIPS) | Pipeline & Hazards | Cache / Memory Hierarchy | SMP Linux Support |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Verilator (RTL)** | Cycle-exact Verilog netlist | **0.05 – 0.2 MIPS** | Exact wires | Exact BRAM / bus | Very slow |
| **gem5 (Detailed/O3)** | Out-of-order cycle-approximate | **0.1 – 0.3 MIPS** | Full ROB / Rename | Ruby / Classic | Yes |
| **gem5 (Minor/In-Order)** | In-order cycle-approximate | **0.3 – 0.8 MIPS** | Staged pipeline | Cache models | Yes |
| **SimRV (CA Mode)** | In-order cycle-accurate | **3.0 – 6.0 MIPS** | 3/5-stage, Scoreboard | MESI directory hub | **Yes (Fast SMP)** |
| **Spike** | Architectural reference | **15 – 25 MIPS** | ❌ None | ❌ None | Basic HTIF |
| **SimRV (`--mode fast`)** | Architectural functional | **100 – 125 MIPS** | ❌ None | ❌ None | Full VirtIO + SBI |

### Why is SimRV CA 5× to 15× Faster than gem5?

1. **Inlined Transition Kernels vs. Dynamic Event Queues**:
   - `gem5` dispatches every cycle, stage tick, and packet through dynamic priority queues with heavy virtual method calls and memory allocations.
   - `SimRV` advances pipeline stages via a flat, monolithic cycle transition kernel with zero-copy pointer swaps between stages.
2. **Bitmask Register Scoreboard**:
   - Integer, floating-point, and vector RAW/WAW hazard checking is evaluated using compact bitwise masks rather than dynamic token graphs.
3. **Compile-Time Word Specialization**:
   - `SIMRV_XLEN` is fixed at compile time (32 or 64), eliminating runtime word-width branching and enabling full LTO and auto-vectorization.
4. **Branchless Host Execution**:
   - 2D lookup tables for branch predictor saturating counters, instruction traits, and opcode tables eliminate host CPU branch mispredictions.

---

## 4. Citation & BibTeX

If you reference SimRV in peer-reviewed publications, please cite the software release:

```bibtex
@software{simrv2026,
  author = {Trunk, Lennart and Kise, Kenji},
  title = {{SimRV: A Dual-Width Explainable RISC-V System Simulator}},
  url = {https://github.com/archlab-sciencetokyo/SimRV},
  version = {3.0.0-alpha.4},
  year = {2026}
}
```

For software metadata, see [CITATION.cff](https://github.com/archlab-sciencetokyo/SimRV/blob/main/CITATION.cff).
