# Install SimRV

For normal use, download the latest release archive from
[GitHub Releases](https://github.com/archlab-sciencetokyo/SimRV/releases). The release provides
RV32 and RV64 simulator binaries for an x86-64 Linux host.

## Portable archive

The portable archive contains a fully static musl-linked binary. It does not need the host's
glibc or libstdc++ runtime and is intended for Ubuntu 22.04+ and comparable current Linux
distributions.

```bash
tar -xzf SimRV-rv64-linux-x86_64-vVERSION.tar.gz
./SimRV --version
```

Use the RV32 archive when the simulator itself must model XLEN=32. The host is still x86-64;
RV32 and RV64 describe the guest architecture.

## Build from source

Source builds require a C++23 compiler, CMake, and Ninja. Configure the target width explicitly:

```bash
cmake --preset rv64-release
cmake --build --preset rv64-release

# Or build the strict RV32 target:
cmake --preset rv32-release
cmake --build --preset rv32-release
```

The executable is `build/rv64-release/SimRV` or `build/rv32-release/SimRV`.

## Choose the next guide

- [CLI and TUI quickstart](index.md)
- [Bare-metal programs](baremetal.md)
- [Linux images](linux.md)
- [Architecture overview](../architecture/overview.md)
