# Reviewer & Artifact Evaluation Guide

This guide is intended for artifact evaluation reviewers, conference referees, and research evaluators seeking to independently inspect, benchmark, and reproduce the claims and results of **SimRV 2.0**.

---

## 1. Executive Summary of Research Claims

SimRV 2.0 provides:

1. **Simulation Execution Modes**:
   - **Functional Fast Mode (`--ia`)**: Fast instruction-accurate emulation with full VirtIO device and Linux OS support.
   - **Cycle-Accurate Mode (`--ca`)**: Detailed in-order pipeline simulation modeling 5-stage hazard stalls, forwarding, and cache hierarchies.
2. **Dual-Architecture Conformance**:
   - Strict compile-time fixed dual-target qualification for both **RV32GCBV** and **RV64GCBV** with Physical Memory Protection (PMP) and Sv32/Sv39 virtual memory translation.
3. **Full-System Linux Emulation**:
   - Boots un-modified OpenSBI/BBL and Linux kernels with VirtIO block storage, 16550A UART, CLINT timer, and PLIC interrupt controller.
4. **Co-Simulation Verification**:
   - Built-in lockstep verification against the official Spike reference simulator (`--lockstep`) and GDB Remote Serial Protocol support (`--gdb`).

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

SimRV enforces an automated qualification gate across both architectures. Run the dual gates:

```bash
# Run RV64 gate tests
ctest --test-dir build/rv64-release --output-on-failure -L gate

# Run RV32 gate tests
ctest --test-dir build/rv32-release --output-on-failure -L gate

# Run release metadata conformance check
python3 scripts/release_check.py --binary build/rv64-release/SimRV
```

### Step 3: Full Clean-Checkout Reproducibility Suite

To execute the automated end-to-end reproducibility workflow that verifies clean checkouts, license audits, architectural boundaries, and package consumers:

```bash
python3 scripts/reproduce.py --mode quick
```

For full multi-compiler qualification across GCC and Clang:

```bash
python3 scripts/reproduce.py --mode full --output repro/results
```

### Step 4: Full-System Linux Boot Verification

Verify that Linux boots cleanly to the user login shell:

```bash
# Boot RV64 Linux in headless CLI mode
./build/rv64-release/SimRV --os --cli \
  -m linux-images/rv64/fw_payload.bin \
  -D linux-images/rv64/root.bin \
  -f linux-images/rv64/devicetree.dtb \
  -s 20000000
```

---

## 3. Architectural Feature Matrix & Design Trade-offs

The following table summarizes architectural capabilities and simulation trade-offs across common RISC-V research environments:

| Simulator | Execution Paradigm | Pipeline & Hazards | Cache / Memory Hierarchy | Full Linux Support | Live TUI Inspection |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **gem5 (Detailed/O3)** | Out-of-order cycle-approximate | Full ROB / Rename | Ruby / Classic | Yes | ❌ Post-mortem trace |
| **gem5 (Minor/In-Order)** | In-order cycle-approximate | Staged pipeline | Cache models | Yes | ❌ Post-mortem trace |
| **SimRV (`--ca`)** | In-order cycle-accurate | 5-stage, Hazards, Forwarding | L1 I/D Cache | Yes | Live split-screen TUI |
| **SimRV (`--ia`)** | Architectural functional | ❌ None | ❌ None | Yes | Live split-screen TUI |
| **Spike** | Architectural reference | ❌ None | ❌ None | Basic HTIF | ❌ Command-line only |

### Architectural Design Principles

1. **Inlined Transition Kernels vs. Dynamic Event Queues**:
   - Simulation frameworks like `gem5` dispatch every cycle, stage tick, and packet through dynamic priority event queues.
   - `SimRV` advances pipeline stages via a flat, monolithic cycle transition kernel with zero-copy pointer swaps between stages.
2. **Compile-Time Word Specialization**:
   - `SIMRV_XLEN` is fixed at compile time (32 or 64), eliminating runtime word-width branching and enabling full link-time optimization (LTO) and compiler auto-vectorization.
3. **Dedicated Forwarding & Hazard Evaluation**:
   - Staging buffers and hazard resolution are tightly coupled within contiguous CPU state without indirect pointer indirection.
4. **Empirical Benchmarking**:
   - Absolute wall-clock execution time and throughput depend heavily on host CPU microarchitecture, compiler optimization levels (such as LTO), host memory bandwidth, and guest workload characteristics. Users and reviewers are encouraged to record local baseline measurements on their own hardware using the bundled benchmark tooling:
     ```bash
     python3 scripts/benchmark.py --binary build/rv64-release/SimRV
     ```

---

## 4. Citation & BibTeX

If you reference SimRV in peer-reviewed publications, please cite the software release:

```bibtex
@software{simrv2026,
  author = {Trunk, Lennart and Kise, Kenji},
  title = {{SimRV: A Dual-Width Explainable RISC-V System Simulator}},
  url = {https://github.com/archlab-sciencetokyo/SimRV},
  version = {2.0.2},
  year = {2026}
}
```

For software metadata, see [CITATION.cff](https://github.com/archlab-sciencetokyo/SimRV/blob/main/CITATION.cff).
