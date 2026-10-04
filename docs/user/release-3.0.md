# SimRV 3.0 Release Guide

SimRV 3.0 standardizes the path from a desktop Linux GUI boot to a future
virt-compatible FPGA handoff. The exact binary or image you used should always be identified by
its Git tag and artifact manifest.

## Choose a workflow

| Goal | Starting point | Guest profile |
| --- | --- | --- |
| Explore Linux, JWM, and Links/Sixel | [Full-System Linux](linux.md) | Alpine GUI |
| Bring up a conventional serial Linux baseline | [FPGA boot artifacts](linux.md#fpga-baseline-profile) | Buildroot headless |
| Validate the simulator itself | [GA workflow](ga-workflow.md) | RV64 release |
| Verify strict-width behavior | [Installation](install.md#build-from-source) | RV32 release |

## Build with the standard CMake workflow

The normal user build is RV64. Presets keep generated files, compile commands, and ccache state
under `build/`, so a read-only home cache does not affect compiler probes or LTO configuration.

```bash
cmake --preset rv64-release
cmake --build --preset rv64-release
ctest --test-dir build/rv64-release --output-on-failure -L gate
```

Build RV32 when strict-width coverage is required:

```bash
cmake --preset rv32-release
cmake --build --preset rv32-release
ctest --test-dir build/rv32-release --output-on-failure -L gate
```

## Linux boot contract

Linux images publish a manifest alongside predictable artifacts:

- OpenSBI firmware, either separable `fw_dynamic.bin` or convenient `fw_payload.bin`.
- Linux `Image` and the generated profile DTB.
- A journaled ext4 root filesystem image.
- Boot arguments, memory sizing, hashes, and configuration metadata.

Use the payload image for a quick SimRV boot. Use the separable OpenSBI, Linux, DTB, and rootfs
artifacts when handing the system to FPGA firmware or a board boot flow. The FPGA baseline uses
BusyBox init, serial getty, DHCP, UUID/PARTUUID discovery, `rootwait`, and ordered ext4 journaling.

## GUI and FPGA profiles

The Alpine profile remains the default for graphical simulation. It carries X11/JWM and the
framebuffer/input path used by the Links/Sixel smoke test. The Buildroot profile is intentionally
headless and keeps the FPGA baseline small: serial login first, with framebuffer and input support
available for later board-specific integration.

```bash
# Default Alpine/JWM GUI image
cmake --build --preset rv64-release --target linux-images

# Optional Buildroot FPGA baseline
cmake --build --preset rv64-release --target linux-images-buildroot
```

For a board handoff, verify the DTB memory range against the simulator or board DRAM, preserve the
published rootfs UUID, and pass the manifest boot arguments unless the board profile supplies an
equivalent console and root-device configuration.

## Release evidence

The release checks are intentionally reproducible and can be run without publishing anything:

```bash
python3 scripts/release_check.py \
  --binary build/rv64-release/simrv \
  --binary build/rv32-release/simrv

python3 scripts/check-linux-artifacts.py linux-images/rv64
```

For research and bug reports, include the exact tag, architecture, profile, manifest, and host
toolchain. The stable Pages site documents the supported release line; the development site may
describe changes that have not yet reached the stable branch.
