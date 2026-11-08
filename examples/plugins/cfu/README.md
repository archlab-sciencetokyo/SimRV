# Reference Custom Function Unit (CFU) Plugin

This example demonstrates how to implement a hardware-accelerated Custom Function Unit (CFU) plugin for SimRV, compatible with both **SimRV CFU Plugin ABI v2** and **CFU-Proving-Ground / CFU-Playground**.

## Implemented Operations

The CFU implements four hardware functions triggered via custom RISC-V instructions (`custom0` / opcode `0x0b`):

| `funct3` | Operation | Description | Cycles |
|---|---|---|---|
| `0` | Dot Product | 4x signed 8-bit integer dot product $\sum_{i=0}^3 (a_i \times b_i)$ | 1 |
| `1` | SAD | 4x unsigned 8-bit sum of absolute differences $\sum_{i=0}^3 \|a_i - b_i\|$ | 2 |
| `2` | Popcount / Hamming | Population count of bitwise difference (`popcount(rs1 ^ rs2)`) | 1 |
| `3` | MAC | Multiply and accumulate into internal state register | 1 |

## Dual Compatibility (SimRV v2 + CFU-Proving-Ground)

This reference plugin exports:
1. `uint32_t cfu_op(int funct3, int funct7, uint32_t rs1, uint32_t rs2)` and `void cfu_reset(void)` for direct plug-and-play compatibility with CFU-Playground / CFU-Proving-Ground.
2. `simrv_cfu_init()` returning `SimRvCfuPlugin` with:
   - `execute`: 32-bit execution with latency reporting
   - `execute64`: 64-bit wide register execution
   - `query_latency`: Static latency query for pipeline hazard modeling without state side-effects
   - `name`: Identifies the CFU in simulation logs

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Produces `libsimrv-demo-cfu.so`.

## Running with SimRV

Option 1: Using the platform/CPU configuration file:
```bash
simrv --cli --config cfu.cfg guest_program.elf
```

Option 2: Direct CLI flags:
```bash
simrv --cli --cpu-model cfu-provingground --cfu ./build/libsimrv-demo-cfu.so guest_program.elf
```
