# SimRV: Dual-Width Explainable RISC-V System Simulator

<p align="center">
  <img src="https://raw.githubusercontent.com/archlab-sciencetokyo/SimRV/main/docs/assets/logo.png" alt="SimRV Logo" width="180" onerror="this.style.display='none'"/>
</p>

<p align="center">
  <strong>An explainable, dual-width (RV32 / RV64) RISC-V architectural simulator with an interactive terminal workbench (TUI), cycle-accurate microarchitectural modeling, hardware RTL parity verification, and full-system SMP Linux emulation.</strong>
</p>

<p align="center">
  <a href="https://github.com/archlab-sciencetokyo/SimRV/releases"><img src="https://img.shields.io/badge/version-3.0.0--alpha.4-blue.svg" alt="SimRV Version"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License"/></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-purple.svg" alt="C++23"/>
  <img src="https://img.shields.io/badge/architecture-RV32GCBV%20%7C%20RV64GCBV-orange.svg" alt="Architecture"/>
  <img src="https://img.shields.io/badge/SMP-2%20to%2016%20cores-brightgreen.svg" alt="SMP"/>
</p>

---

## Key Highlights

=== "Full-System Linux SMP"
    SimRV boots un-modified RISC-V Linux kernels and OpenSBI across **2 to 16 SMP cores** using dynamic Flattened Device Tree (FDT) synthesis. It features VirtIO block storage, 16550A UART, CLINT/ACLINT timers, and PLIC/AIA interrupt controllers, achieving **40+ MIPS** simulation speed on multi-core Linux boots.

=== "Cycle-Accurate Modeling"
    Features inlined per-cycle transition kernels for **3-stage** and **5-stage** pipelines with an authoritative hardware register scoreboard (tracking INT, FP, and Vector dependencies), configurable branch predictors (Bimodal, GShare, Tournament), and multi-level L1/L2/L3 MESI directory cache coherence.

=== "Hardware RTL Parity"
    Validated bit-for-bit against physical Verilog implementations (such as CFU-Proving-Ground RVProc and Archlab RVComp) through Verilator. SimRV matches cycle-by-cycle retirement traces and hardware performance counters while running **20× to 50× faster than software RTL simulation**.

=== "Interactive TUI & Classroom"
    Interactive split-screen terminal monitor displaying live register files, pipeline stages, cache tags, hazard graphs, disassembly explainers, and an interactive Linux PTY console. Includes a structured classroom mission mode for computer architecture education.

---

## Quickstart

### 1. Build from Source

SimRV requires a modern C++23 compiler (**Clang 22+** or **GCC 16+**), **CMake 3.31+**, and **Ninja**.

=== "RV64 Target (Default)"
    ```bash
    git clone https://github.com/archlab-sciencetokyo/SimRV.git
    cd SimRV

    # Configure and build RV64 release target
    cmake --preset rv64-release
    cmake --build --preset rv64-release -j$(nproc)
    ```

=== "RV32 Target"
    ```bash
    git clone https://github.com/archlab-sciencetokyo/SimRV.git
    cd SimRV

    # Configure and build RV32 release target
    cmake --preset rv32-release
    cmake --build --preset rv32-release -j$(nproc)
    ```

### 2. Run Your First Workload

=== "Interactive TUI Mode (Default)"
    ```bash
    # Launch interactive terminal workbench
    ./build/rv64-release/SimRV -m examples/isa/bin/demo.elf
    ```

=== "Headless CLI Execution"
    ```bash
    # Fast instruction simulation
    ./build/rv64-release/SimRV --cli -m examples/isa/bin/demo.elf -b

    # Cycle-accurate simulation with 5-stage pipeline
    ./build/rv64-release/SimRV --cli --mode cycle-accurate --pipeline 5stage -m examples/isa/bin/demo.elf
    ```

=== "Boot SMP Linux (2 Cores)"
    ```bash
    # Boot Linux kernel across 2 SMP harts with dynamic device tree
    ./build/rv64-release/SimRV --cli --smp 2 \
      -m linux-images/rv64/fw_payload.bin \
      --dtb dynamic \
      -D linux-images/rv64/root.img \
      -s 20000000
    ```

---

## Documentation Navigation

<div class="grid cards" markdown>

-   :material-book-open-page-variant:{ .lg .middle } __[User Guide](user/index.md)__

    ---

    CLI options, TUI navigation, keybindings, bare-metal programs, and Linux boot options.

-   :material-chip:{ .lg .middle } __[System Architecture](architecture/overview.md)__

    ---

    Detailed design of the execution units, TileLink-C cache coherence, MMU, and scoreboard.

-   :material-tune-vertical:{ .lg .middle } __[CPU Models & Tuning](hardware/models.md)__

    ---

    Human-editable `.cfg` processor profiles, pipeline calibration, and tuning with `simrv-tune`.

-   :material-check-decagram:{ .lg .middle } __[RTL Parity Verification](hardware/rtl_parity.md)__

    ---

    Framework for cycle-by-cycle retirement trace comparison against physical Verilog designs.

-   :material-clipboard-check-outline:{ .lg .middle } __[Reviewer & Evaluation Guide](evaluation/reviewer.md)__

    ---

    Artifact evaluation checklist, reproducibility commands, simulator speed comparisons, and BibTeX citations.

-   :material-code-braces:{ .lg .middle } __[Developer & Contributing](dev/contributing.md)__

    ---

    Subsystem organization, C++23 standards, branching model, and writing CTest suites.

</div>

---

## Citation

If you use SimRV in your academic research, please cite:

```bibtex
@software{simrv2026,
  author = {Trunk, Lennart and Kise, Kenji},
  title = {{SimRV: A Dual-Width Explainable RISC-V System Simulator}},
  url = {https://github.com/archlab-sciencetokyo/SimRV},
  version = {3.0.0-alpha.4},
  year = {2026}
}
```
