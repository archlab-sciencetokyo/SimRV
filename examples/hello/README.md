# Hello sample

This is a small included SimRV workload. It writes a greeting to the simulated UART, then calls
named assembly routines that demonstrate string output, loops, conditional branches, and repeated
addition before writing `1` to the SimRV `tohost` address (`0x80001000`) so the run terminates
cleanly. Its output is:

```
Hello from SimRV!
Counting: 0 1 2 3 4
Parity:1 odd
2 even
3 odd
4 even
5 odd
sum_to_n(5) = 15
```

Build it with a RISC-V bare-metal toolchain:

```bash
make -C examples/hello XLEN=64 OUT=/tmp/simrv-hello-rv64
simrv --cli --baremetal -m /tmp/simrv-hello-rv64/hello.elf
```

The package includes this source and Makefile, but not generated outputs or a cross compiler. The
Makefile also supports `XLEN=32` with the same multilib-capable `riscv64-unknown-elf-` prefix:

```bash
make -C examples/hello XLEN=32 OUT=/tmp/simrv-hello-rv32
```

The ELF is the simulator input; the `.bin` is available for firmware loaders that need a flat
binary image.
