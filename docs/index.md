# SimRV: Dual-Width Explainable RISC-V System Simulator

<p align="center">
  <strong>An explainable, dual-width (RV32 / RV64) RISC-V research simulator with an interactive terminal workbench (TUI), cycle-accurate in-order pipeline modeling, cache hierarchy inspection, and full-system Linux OS emulation.</strong>
</p>

<p align="center">
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=main" alt="C/C++ CI"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml/badge.svg?branch=main" alt="Docs CI"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/releases"><img src="https://img.shields.io/badge/version-2.0.2-blue.svg" alt="SimRV Version"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License"/></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-purple.svg" alt="C++23"/>
  <img src="https://img.shields.io/badge/architecture-RV32%20%7C%20RV64-orange.svg" alt="Architecture"/>
</p>

---

## Key Highlights

=== "Explainable Dual-Width Architecture"
    SimRV provides compile-time fixed 32-bit and 64-bit simulator targets (**RV32GCBV** and **RV64GCBV**). It supports Base Integer (`I`/`E`), Multiply/Divide (`M`), Atomics (`A`), Single/Double Floating Point (`F`/`D`), Compressed (`C`), Bit Manipulation (`B`), and Vector 1.0 (`V`) extensions with Sv32/Sv39 virtual memory translation.

=== "Cycle-Accurate Pipeline Timing"
    Features a cycle-accurate in-order pipeline execution kernel modeling instruction latency, data hazard stalls, forwarding paths, branch prediction (Bimodal, 2-level adaptive, RAS, and BTB), and multi-way L1 instruction and data cache hierarchies.

=== "Interactive TUI Workbench"
    A rich terminal user interface (TUI) providing live split-screen inspection of integer, floating-point, and vector register banks, pipeline slots, cache set tags, hazard indicators, instruction decoding explainers, memory viewers, and guest terminal PTY interaction.

=== "Full-System Linux Emulation"
    Boots un-modified RISC-V Linux kernels with OpenSBI/BBL, featuring a 16550A UART serial console, VirtIO block storage, CLINT timer interrupts, and PLIC interrupt controller routing.

---

## Quickstart

### 1. Build from Source

SimRV requires a modern C++23 compiler (**Clang 20+** or **GCC 14+**), **CMake 3.20+**, and **Ninja**.

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
    # Launch interactive terminal workbench with a baremetal binary
    ./build/rv64-release/SimRV -b -m img/hello.bin
    ```

=== "Headless Fast CLI Execution"
    ```bash
    # Fast functional execution without TUI
    ./build/rv64-release/SimRV -b -m img/hello.bin --cli
    ```

=== "Cycle-Accurate Simulation"
    ```bash
    # Run in cycle-accurate mode with step limit
    ./build/rv64-release/SimRV -b -m img/hello.bin --ca --cli -s 1000000
    ```

=== "Full-System Linux Boot"
    ```bash
    # Boot Linux kernel with root filesystem and devicetree
    ./build/rv64-release/SimRV --os \
      -m linux-images/rv64/fw_payload.bin \
      -D linux-images/rv64/root.bin \
      -f linux-images/rv64/devicetree.dtb
    ```

---

## Documentation

<div class="grid cards" markdown>

- :material-book-open-page-variant:{ .lg .middle } **[User Guide](user/index.md)**

    ---

    Quickstart, CLI flags, interactive TUI controls, hotkeys, and execution modes.

- :material-chip:{ .lg .middle } **[Architecture & Compliance](architecture/overview.md)**

    ---

    Core execution units, pipeline modeling, cache hierarchy, MMU, and [ISA compliance scope](architecture/compliance.md).

- :material-memory:{ .lg .middle } **[Bare-Metal & Linux](user/baremetal.md)**

    ---

    [Bare-metal development](user/baremetal.md), memory map, peripheral MMIO, and [booting full-system Linux](user/linux.md).

- :material-code-braces:{ .lg .middle } **[Development & Verification](dev/contributing.md)**

    ---

    Contributing standards, CTest gate suites, [reviewer reproduction guide](evaluation/reviewer.md), and [release qualification](evaluation/release.md).

</div>

---

## Citation

If you use SimRV in your academic research, please cite:

```bibtex
@software{simrv,
  title = {{SimRV: A Dual-Width Explainable RISC-V System Simulator}},
  author = {{SimRV Contributors}},
  url = {https://github.com/archlab-sciencetokyo/SimRV},
  version = {2.0.2},
  year = {2026}
}
```
