# Install SimRV

SimRV publishes one Linux x86-64 simulator build. The installed `simrv` executable runs RV64
guests and provides RV32 guest compatibility. Native RV32 builds remain a source-level validation
target and are not distributed separately.

The supported host platform is x86-64 Linux. Windows users should run SimRV inside
[WSL2](https://learn.microsoft.com/windows/wsl/) with a supported Linux distribution; native
Windows builds are not currently supported.

Download the newest package bundle or portable archive from
[GitHub Releases](https://github.com/archlab-sciencetokyo/SimRV/releases). Beta releases are marked
as prereleases.

## Portable archive

The portable archive contains the runtime component only and is fully statically linked against
musl. It needs an x86-64 Linux kernel but no host glibc or SimRV runtime libraries. Extract it into
`/usr/local` while removing the archive's leading `usr/` directory:

```bash
tar -tzf SimRV-linux-x86_64-vVERSION.tar.gz
sudo tar -xzf SimRV-linux-x86_64-vVERSION.tar.gz \
  --strip-components=1 -C /usr/local
simrv --version
```

This is the recommended option for older distributions, containers, and compute servers.

Remove the installed files using the archive's file list, or replace them by extracting a newer
archive at the same prefix. Use a native package when managed upgrades and removal matter.

## Fedora and RPM systems

The RPM bundle contains a standard repository index and four packages. For a normal simulator
installation, install only the runtime package:

```bash
mkdir simrv-rpm && tar -xzf SimRV-rpm-packages-vVERSION.tar.gz -C simrv-rpm
sudo dnf install ./simrv-rpm/simrv-[0-9]*.rpm
simrv --version
```

Install `simrv-devel-*.rpm` to develop C++ applications against SimRV, `simrv-tools-*.rpm` for
model and parity utilities, or `simrv-benchmark-*.rpm` for the benchmark driver. Upgrade by
installing the corresponding packages from a newer bundle. Remove them with
`sudo dnf remove simrv simrv-devel simrv-tools simrv-benchmark`.

To install the runtime and SDK together from the downloaded bundle:

```bash
sudo dnf install ./simrv-rpm/simrv-[0-9]*.rpm ./simrv-rpm/simrv-devel-[0-9]*.rpm
```

## Debian and Ubuntu

The DEB bundle includes a `Packages` dependency manifest. Install the runtime package with APT so
host-library dependencies are resolved normally:

```bash
mkdir simrv-deb && tar -xzf SimRV-deb-packages-vVERSION.tar.gz -C simrv-deb
sudo apt install ./simrv-deb/simrv_*_amd64.deb
simrv --version
```

The optional `simrv-devel` package provides the C++ SDK and depends on the matching `simrv`
version. The tools and benchmark packages also depend on `simrv`. Install the needed `.deb` files
with the same command. Upgrade from a newer bundle or remove packages with
`sudo apt remove simrv simrv-devel simrv-tools simrv-benchmark`.

To install the runtime and SDK together from the downloaded bundle:

```bash
sudo apt install ./simrv-deb/simrv_*_amd64.deb ./simrv-deb/simrv-devel_*_amd64.deb
```

## C++ development package

The native `simrv-devel` package contains SimRV's public C++ headers, the static runtime library,
and the CMake package that exports `SimRV::runtime`. It is optional and is not part of the portable
TGZ. Native SDK packages are qualified on Debian sid and Fedora 44; the SDK expects a C++23-capable
compiler with a standard library ABI compatible with the host package. Source installs also
provide the CMake package and do not require a native development package.

Link the exported target from a CMake project:

```cmake
find_package(SimRV 3.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE SimRV::runtime)
```

The target supplies the C++23 compile requirement and required system link dependencies. A
consumer can then include public headers such as `simrv/core/MachineConfig.hpp`.

The native package puts the CMake config in the system search path. A full source install also
installs the SDK under its chosen prefix; for the default `/usr/local/simrv` source prefix, pass
`-DCMAKE_PREFIX_PATH=/usr/local/simrv` when configuring the consumer. The runtime-only TGZ has no
headers, library, or CMake package and cannot serve as an SDK.

## Supported Linux distributions

Native packages are qualified in release CI on the following x86-64 distributions:

!!! info "Choose an install format"
    Use the RPM or DEB bundle for OS-managed installation and removal. Choose the static TGZ for
    older distributions, containers, or servers where you want to avoid host C++ runtime
    dependencies.

| Package | Tested distributions | Notes |
| --- | --- | --- |
| RPM | Fedora 44 (the release-build baseline) | Install with `dnf`; older RPM distributions should use the static TGZ unless their glibc/libstdc++ ABI is newer enough. |
| DEB | Debian sid (the release-build baseline) | Install with `apt`; older Debian/Ubuntu releases should use the static TGZ. |
| Static TGZ | Fedora 40, Ubuntu 22.04/24.04, Debian 12/13, and newer x86-64 Linux | Does not require glibc, libstdc++, or TOML runtime packages. |

The native packages are tested rather than promised for every derivative. Use the static TGZ when a
distribution is outside this matrix or when package-manager integration is unavailable.

## What the packages include

| Package | Included content | Compatibility notes |
| --- | --- | --- |
| `simrv` | Simulator, CPU model configs, user docs, example assembly/source, and classroom mission files | x86-64 Linux host; RV64 guests and RV32 guest compatibility |
| `simrv-devel` | Public C++ headers, static runtime library, and `SimRV::runtime` CMake package | Optional native SDK; C++23 compiler and host-compatible C++ standard library |
| `simrv-tools` | CPU model wizard, tuning and RTL parity utilities, and their Python support files | Requires Python 3; RTL parity also needs the selected external RTL/toolchain environment |
| `simrv-benchmark` | Benchmark driver and experiment metadata helper | Requires Python 3 and separately prepared benchmark workloads |

Packages do not include a RISC-V cross compiler, generated guest ELF files, Linux images, or
external test suites. The example Makefiles build into `/tmp` by default; install a RISC-V ELF
toolchain to build them. The sample mission and its source are installed under
`/usr/share/doc/SimRV/examples/isa`. Copy that tree to a writable directory before building an
installed example. For example, with a RISC-V GCC toolchain whose executable is named
`riscv64-unknown-elf-gcc`:

```bash
example_dir="$(mktemp -d)"
cp -a /usr/share/doc/SimRV/examples/isa "$example_dir/"
cd "$example_dir/isa"
make XLEN=64 RISCV_PREFIX=riscv64-unknown-elf-
simrv --tui --baremetal \
  -m /tmp/simrv-isa-rv64/control-flow-calls.elf \
  --mission lessons/control-flow-calls.mission
```

To use another cross toolchain, set `RISCV_PREFIX` to its compiler prefix and ensure it targets
the selected XLEN and ABI. The generated ELF stays in `/tmp`; the mission path is relative to the
copied source tree. The side-by-side mission layout needs a terminal at least 144 columns wide.

## Build from source

Source builds require a C++23 compiler, CMake 3.31 or newer, and Ninja:

```bash
git clone https://github.com/archlab-sciencetokyo/SimRV.git
cd SimRV
cmake --preset rv64-release
cmake --build --preset rv64-release -j"$(nproc)"
./build/rv64-release/simrv --version
```

By default, source installs keep SimRV's files separate from other applications under
`/usr/local/simrv`. The executable is `/usr/local/simrv/bin/simrv`; add that `bin` directory to
your `PATH` to invoke it as `simrv`:

```bash
sudo cmake --install build/rv64-release --prefix /usr/local/simrv
export PATH="/usr/local/simrv/bin:$PATH"
simrv --version
```

Put the `export PATH=...` line in your shell's startup file if you want it in future sessions. To
choose another prefix, configure with `-DCMAKE_INSTALL_PREFIX=/path/to/prefix` or override it for
one install with `cmake --install build/rv64-release --prefix /path/to/prefix`. In either case,
the executable is installed under `<prefix>/bin` and SimRV data under `<prefix>/share/SimRV`.

The install also includes
`/usr/local/simrv/share/SimRV/SimRVBuildMetadata.json`. It records the SimRV version, source Git
revision, target XLEN, compiler, generator, host platform, and optional `SOURCE_DATE_EPOCH` used
for reproducible builds. Its schema is installed at
`/usr/local/simrv/share/SimRV/schemas/simrv-build-metadata.schema.json`.

Runtime installs also include Bash, Zsh, and Fish command completions under
`/usr/local/simrv/share`. Native packages place them in the standard system completion paths and
are normally discovered automatically. With a custom source prefix, point the shell at its
completion directory; for example:

```bash
# Bash, for the current shell (put this in ~/.bashrc to persist)
source /usr/local/simrv/share/bash-completion/completions/simrv

# Zsh, add to ~/.zshrc before compinit
fpath=(/usr/local/simrv/share/zsh/site-functions $fpath)

# Fish, for future shells
set -Ua fish_complete_path /usr/local/simrv/share/fish/vendor_completions.d
```

Build `rv32-release` only when a native RV32 strict-width implementation is required for
verification. Normal users should use the RV64-capable build.

## Next steps

- Follow the [CLI quickstart](index.md) to run a bare-metal image.
- Read [Bare-Metal and Embedded](baremetal.md) for physical-memory and platform details.
- Read [Full-System Linux](linux.md) to build and boot a Linux image.
