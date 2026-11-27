# Reference Custom Function Unit (CFU) Plugin

This example shows a hardware-accelerated CFU shared library using SimRV's versioned C ABI.
It implements four operations using custom RISC-V instructions (`custom-0`, opcode `0x0b`):

| `funct3` | Operation | Description | Cycles |
| --- | --- | --- | --- |
| `0` | Dot product | Four signed 8-bit products and sum | 1 |
| `1` | SAD | Four unsigned 8-bit absolute differences | 2 |
| `2` | Hamming distance | `popcount(rs1 ^ rs2)` | 1 |
| `3` | Multiply-accumulate | Multiply and accumulate in plugin state | 1 |

The descriptor exports `simrv_cfu_init()` and includes 32-bit and 64-bit execution callbacks, a
reset callback, a static latency query, and a diagnostic name. Build with CMake:

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

Load it with a custom model configuration that enables a CFU:

```sh
simrv --config ./my-cfu.cfg --cfu-plugin ./build/libsimrv-demo-cfu.so guest.elf
```
