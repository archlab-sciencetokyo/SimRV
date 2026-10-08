# Release Validation

SimRV 3.0 is intended to be useful as a portable architecture-debugging workbench, not only as
an instruction runner. This page is the short acceptance workflow for a release installation.

## Diagnose the host

Run the environment check before starting an interactive session:

```bash
simrv --doctor
simrv --isa-info
```

`--doctor` reports whether the process has an interactive terminal, whether the environment gives
the TUI a Sixel hint, and which local attach transport is available. A missing Sixel terminal is
not an error: the text TUI remains supported. `--isa-info` is the authoritative user-facing
summary of the qualified ISA and the explicitly unqualified areas.

## Run the golden bare-metal path

```bash
make -C examples/hello XLEN=64
simrv --cli --baremetal -m examples/hello/build-rv64/hello.elf
```

The guest writes to the simulated UART and exits through `tohost`. For a graphical Linux image,
use the Linux image instructions and verify the Alpine/JWM desktop with VirtIO GPU, input, sound,
and networking enabled. Sixel display rendering is an optional host presentation layer; guest
framebuffer behavior is validated independently.

## Attach and inspect a running guest

```bash
simrv --cli --listen-tui /tmp/simrv.sock -m examples/hello/build-rv64/hello.elf
simrv --attach /tmp/simrv.sock
```

Pause, step, inspect registers and memory, then detach and reconnect. The guest state must remain
alive across detach. The same guest can be debugged through GDB with `--gdb`; TUI and GDB are
separate frontends by design.

## Export a compatible SoC description

```bash
simrv --dump-soc-manifest virt-pcie soc.json
```

The manifest contains both `schema_version` and `manifest_version`. Consumers should reject an
unknown schema or compatibility version instead of silently guessing device addresses.

The repository smoke check combines these steps:

```bash
python3 scripts/ga_smoke.py --simrv build/rv64-release/simrv \
  --guest examples/hello/build-rv64/hello.elf
```

Before handing off release binaries or guest images, verify their manifests and keep reproducible
evidence with the exact Git tag, architecture, guest profile, manifest, and host toolchain:

```bash
python3 scripts/release_check.py \
  --binary build/rv64-release/simrv \
  --binary build/rv32-release/simrv
python3 scripts/check-linux-artifacts.py linux-images/rv64
```
