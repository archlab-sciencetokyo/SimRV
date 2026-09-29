# Hello sample

This is the smallest included SimRV workload. It writes a short message to the simulated UART,
then writes `1` to the SimRV `tohost` address (`0x80001000`) so the run terminates cleanly.

Build it with a RISC-V bare-metal toolchain:

```bash
make -C examples/hello XLEN=64
```

The output is `examples/hello/build-rv64/hello.bin`. Use `XLEN=32` and a matching
`riscv32-unknown-elf-` prefix for an RV32 image.
