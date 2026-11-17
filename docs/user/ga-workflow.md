# Release Validation

SimRV 3.0 is intended to be useful as a portable architecture-debugging workbench, not only as
an instruction runner. This page is the short acceptance workflow for a release installation.

## Diagnose the host

Run the environment check before starting an interactive session:

```bash
simrv --doctor
simrv --isa-info
```

`--doctor` reports whether the process has an interactive terminal and the host platform. Its
SIXEL environment hint is advisory; the interactive TUI checks for Kitty graphics and SIXEL before
offering its display tool. A terminal without either protocol can still use the text TUI.
`--isa-info` is the authoritative user-facing summary of the qualified ISA and the explicitly
unqualified areas.

## Run the golden bare-metal path

```bash
make -C examples/hello XLEN=64
simrv --cli --baremetal -m examples/hello/build-rv64/hello.elf
```

The guest writes to the simulated UART and exits through `tohost`. For a graphical Linux image,
use the Linux image instructions and verify the Alpine/JWM desktop with VirtIO GPU, input, sound,
and networking enabled. Sixel display rendering is an optional host presentation layer; guest
framebuffer behavior is validated independently of terminal graphics rendering.

## Export a compatible SoC description

```bash
simrv config export-soc virt-pcie soc.json
```

The manifest contains both `schema_version` and `manifest_version`. Consumers should reject an
unknown schema or compatibility version instead of silently guessing device addresses.
