<section class="simrv-hero" markdown>

<p class="simrv-eyebrow">RISC-V simulation, from instruction to system</p>

# SimRV

<p class="simrv-hero-copy">Explore RV32 and RV64 systems with fast architectural simulation, cycle-accurate pipelines, custom SoCs, and full-system Linux.</p>

<div class="simrv-hero-actions">
  <a class="md-button md-button--primary" href="user/install/">Get started</a>
  <a class="md-button" href="architecture/overview/">Explore the architecture</a>
</div>

</section>

<div class="simrv-badges" aria-label="Project status">
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=dev" alt="C/C++ CI"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml/badge.svg?branch=dev" alt="Docs CI"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License"/></a>
</div>

---

## Key Highlights

=== "Full-System Linux SMP"
    SimRV boots un-modified RISC-V Linux kernels and OpenSBI across **2 to 16 SMP cores** using dynamic Flattened Device Tree (FDT) synthesis. It features VirtIO block storage, 16550A UART, CLINT/ACLINT timers, PLIC/AIA interrupt controllers, and an Alpine/JWM graphical path with an idempotent Games menu.

=== "Cycle-Accurate Modeling"
    Features inlined per-cycle transition kernels for **3-stage** and **5-stage** pipelines with an authoritative hardware register scoreboard (tracking INT, FP, and Vector dependencies), configurable branch predictors (Bimodal, GShare, Tournament), and multi-level L1/L2/L3 MESI directory cache coherence.

=== "Hardware RTL Parity"
    Validated bit-for-bit against physical Verilog implementations (such as CFU-Proving-Ground RVProc and Archlab RVComp) through Verilator. SimRV matches cycle-by-cycle retirement traces and hardware performance counters.

=== "Interactive TUI & Education"
    Interactive split-screen terminal monitor displaying live register files, pipeline stages, cache tags, hazard graphs, disassembly explainers, and an interactive Linux PTY console. Includes student guidance and instruction-level explanation.

---

## Quickstart

Download the portable archive or native package bundle from
[GitHub Releases](https://github.com/archlab-sciencetokyo/SimRV/releases), then follow the
[installation guide](user/install.md). Build from source when developing SimRV itself.

### 1. Build from Source

SimRV supports x86-64 Linux hosts. On Windows, use WSL2 with a supported Linux
distribution; native Windows builds are not currently supported.

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
    ./build/rv64-release/simrv -m examples/isa/bin/demo.elf
    ```

=== "Headless CLI Execution"
    ```bash
    # Fast instruction simulation
    ./build/rv64-release/simrv --cli -m examples/isa/bin/demo.elf -b

    # Cycle-accurate simulation with 5-stage pipeline
    ./build/rv64-release/simrv --cli --mode cycle-accurate --pipeline 5stage -m examples/isa/bin/demo.elf
    ```

=== "Boot SMP Linux (2 Cores)"
    ```bash
    # Boot Linux kernel across 2 SMP harts with dynamic device tree
    ./build/rv64-release/simrv --cli --smp 2 \
      -m linux-images/rv64/fw_payload.bin \
      --dtb dynamic \
      -D linux-images/rv64/root.img \
      -s 20000000
    ```

---

## Documentation Navigation

<div class="grid cards" markdown>

- :material-download:{ .lg .middle } **[Install SimRV](user/install.md)**

    ---

    Pick a package, install SimRV, and run your first program.

- :material-linux:{ .lg .middle } **[Boot a Linux guest](user/linux.md)**

    ---

    Buildroot and Alpine images, firmware handoff, root filesystems, and SMP boot.

- :material-memory:{ .lg .middle } **[Extend a CPU or SoC](hardware/extending-platforms.md)**

    ---

    Configure CPU presets, add MMIO devices, or map custom hardware ranges.

- :material-chart-timeline-variant:{ .lg .middle } **[Validate a model](hardware/rtl_parity.md)**

    ---

    Compare architectural or cycle behavior, then tune latency and predictor settings.

</div>

---

## Citation

If you use SimRV in your academic research, please cite:

```bibtex
@software{simrv,
  title = {{SimRV: A Dual-Width Explainable RISC-V System Simulator}},
  author = {{SimRV Contributors}},
  url = {https://github.com/archlab-sciencetokyo/SimRV},
  note = {Cite the exact tagged release used for the experiment},
  year = {2026}
}
```
