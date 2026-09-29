# SimRV: Dual-Width Explainable RISC-V System Simulator

<p align="center">
  <strong>A practical RISC-V simulator for running programs, inspecting architecture, and teaching computer systems.</strong>
</p>

<p align="center">
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=main" alt="C/C++ CI"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml/badge.svg?branch=main" alt="Docs CI"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License"/></a>
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

Choose the shortest path for your goal:

- **Install a release:** use the [installation guide](user/install.md), including the hello samples.
- **Build from source:** follow the build steps below.
- **Run a program:** start with the [CLI and TUI guide](user/index.md).
- **Study the machine:** read the [architecture overview](architecture/overview.md).

### 1. Build from Source

SimRV supports x86-64 Linux hosts. On Windows, use WSL2 with a supported Linux
distribution; native Windows builds are not currently supported.

SimRV requires a modern C++23 compiler (**Clang 20+** or **GCC 14+**), **CMake 3.25+**, and **Ninja**.

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
    make -C examples/hello XLEN=64
    ./build/rv64-release/SimRV -b -m examples/hello/build-rv64/hello.bin
    ```

=== "Headless Fast CLI Execution"
    ```bash
    # Fast functional execution without TUI
    ./build/rv64-release/SimRV -b -m examples/hello/build-rv64/hello.bin --cli
    ```

=== "Cycle-Accurate Simulation"
    ```bash
    # Run in cycle-accurate mode with step limit
    ./build/rv64-release/SimRV -b -m examples/hello/build-rv64/hello.bin --ca --cli -s 1000000
    ```

=== "Full-System Linux Boot"
    ```bash
    # Boot Linux kernel with root filesystem and devicetree
    ./scripts/build-linux-image.sh --arch rv64
    source linux-images/rv64/setup.sh
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

- :material-code-braces:{ .lg .middle } **[Development & Verification](development/contributing.md)**

    ---

    Contributing standards and CTest gate suites for simulator development.

</div>

---

## Citation

If you use SimRV in your academic research, please cite:

```bibtex
@software{simrv,
  title = {{SimRV: A Dual-Width Explainable RISC-V System Simulator}},
  author = {{SimRV Contributors}},
  url = {https://github.com/archlab-sciencetokyo/SimRV},
  version = {2.0.3},
  year = {2026}
}
```
