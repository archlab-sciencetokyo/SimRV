# Hello sample

This is the smallest included SimRV workload. It writes a short message to the simulated UART,
then writes `1` to the SimRV `tohost` address (`0x80001000`) so the run terminates cleanly.

Build it with a RISC-V bare-metal toolchain:

```bash
make -C examples/hello XLEN=64
```

The output is `examples/hello/build-rv64/hello.bin`. The Makefile also supports `XLEN=32`
with the same multilib-capable `riscv64-unknown-elf-` prefix:

```bash
make -C examples/hello XLEN=32
```

This writes `examples/hello/build-rv32/hello.bin`.
