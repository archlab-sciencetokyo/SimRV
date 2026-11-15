# Extending SimRV: Adding New ISA Extensions

This guide outlines the process for onboarding new RISC-V extensions (e.g., Vector `V`,
Bit-Manipulation `Zb*`, Crypto `K`) into the SimRV architecture.

The simulator uses a decode-and-dispatch pipeline model, so new extensions follow a
linear integration path through define → decode → execute → state → TUI.

Extension-family policy is kept in canonical modules: `isa/Zb.hpp`/`Zb.cpp` for bit manipulation,
`isa/Zk.hpp`/`Zk.cpp` for scalar cryptography, and `execute/ExecuteUnitZb.cpp` for Zb execution.
The central decoder remains the dispatcher; extension-specific classification should live in the
corresponding ISA module.

## Preset-level CFU and crypto declarations

CPU presets can describe integration requirements before the RTL implementation is connected.
This keeps the simulator, RTL wrapper generator, and software build using one source of truth:

```ini
[isa]
extensions = "zkn,zkr,zkt"

[cfu]
enabled = true
opcode = "custom-0"
default_latency = 1
rtl_module = "rvproc_cfu"
interface = "rs1-rs2-rd"
```

`zk` is accepted as shorthand for `zkn,zkr,zkt`. These are multi-letter extensions and are
reported separately from the canonical `isa_preset`; they do not consume individual MISA bits.
The current implementation executes the available `Zbkb`/`Zbkc` building blocks (`pack*` and
`clmul*`) through the existing integer B unit when `zkn` is enabled. AES/SHA instructions and
the `seed` CSR are added incrementally.

CFUs use the existing `custom-0` execution path. A simulator plugin can be supplied with
`--cfu-plugin`; the preset's `default_latency` controls the built-in CFU model and
`rtl_module`/`interface` provide the information needed by an HDL wrapper or hand-written RTL.
CFU/plugin latency values count total cycles including the issue cycle; pipeline `*_latency`
settings instead count additional stall cycles after issue.
The current decoder intentionally reserves the complete `custom-0` opcode for the configured CFU.

### Compiling and Loading a Dynamic CFU Plugin

CFU plugins interface with SimRV via a lightweight C ABI defined in `simrv/execute/CfuUnit.hpp`.
To build a custom accelerator shared library:

```c
// my_cfu.c
#include <stdint.h>

struct SimRvCfuPlugin {
    uint32_t (*execute)(uint32_t funct7, uint32_t funct3, uint32_t src1, uint32_t src2,
                        uint32_t* latency_cycles);
    void (*reset)(void);
};

static uint32_t my_execute(uint32_t funct7, uint32_t funct3, uint32_t src1, uint32_t src2,
                           uint32_t* latency_cycles) {
    if (latency_cycles) {
        // Report execution latency in cycles (total cycles, including issue)
        *latency_cycles = (funct3 == 1) ? 3 : 1;
    }
    // Custom arithmetic / bitwise accelerator operation
    return (src1 ^ src2) + funct7;
}

static void my_reset(void) {}

static const struct SimRvCfuPlugin kPlugin = {
    .execute = my_execute,
    .reset = my_reset,
};

const struct SimRvCfuPlugin* simrv_cfu_init(void) {
    return &kPlugin;
}
```

Compile the plugin into a position-independent shared object (`.so`):

```bash
# Using GCC
gcc -O3 -shared -fPIC -o libcfu_custom.so my_cfu.c

# Using Clang
clang -O3 -shared -fPIC -o libcfu_custom.so my_cfu.c
```

Run SimRV with the compiled accelerator plugin:

```bash
simrv --cfu-plugin=./libcfu_custom.so --cpu-preset=cfu-provingground target_program.elf
```

### Dual Extension Architecture: CFU vs. External Device Plugins

SimRV follows a unified plugin philosophy across both execution pipeline and SoC bus domains:

| Extension Type | CLI Flag | Target Subsystem | C ABI Entry Point | C Descriptor Struct | Primary Use Case |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **CFU Plugin** | `--cfu-plugin=<lib.so>` | Execute Stage (`custom-0`) | `simrv_cfu_init` | `SimRvCfuPlugin` | Tightly-coupled instruction accelerators (SIMD, Galois field, crypto, TPU ALU) |
| **Device Plugin** | `--plugin <name>[=args]` | TileLink Interconnect / Memory | `simrv_device_plugin_init` | `SimRvExternalDevice` | Loosely-coupled peripherals (MMC, Ethernet, reset controller, FPGA MMIO) |

Both plugin interfaces use stable, versioned C ABIs loaded at runtime without modifying or rebuilding the core simulator. For complete documentation on authoring and auto-discovering memory-mapped external device plugins, see [Extending Platforms: Dynamic External Device Plugins](extending-platforms.md#dynamic-external-device-plugins-c-abi).

