# Install SimRV

SimRV publishes one Linux x86-64 simulator build. The installed `simrv` executable runs RV64
guests and provides RV32 guest compatibility. Native RV32 builds remain a source-level validation
target and are not distributed separately.

Download the newest package bundle or portable archive from
[GitHub Releases](https://github.com/archlab-sciencetokyo/SimRV/releases). Beta releases are marked
as prereleases.

## Portable archive

The portable archive contains the runtime component only. Extract it into `/usr/local` while
removing the archive's leading `usr/` directory:

```bash
tar -tzf SimRV-linux-x86_64-vVERSION.tar.gz
sudo tar -xzf SimRV-linux-x86_64-vVERSION.tar.gz \
  --strip-components=1 -C /usr/local
simrv --version
```

Remove the installed files using the archive's file list, or replace them by extracting a newer
archive at the same prefix. Use a native package when managed upgrades and removal matter.

## Fedora and RPM systems

The RPM bundle contains a standard repository index and three packages. For a normal simulator
installation, install only the runtime package:

```bash
mkdir simrv-rpm && tar -xzf SimRV-rpm-packages-vVERSION.tar.gz -C simrv-rpm
sudo dnf install ./simrv-rpm/simrv-[0-9]*.rpm
simrv --version
```

Install `simrv-tools-*.rpm` for model and parity utilities, or `simrv-benchmark-*.rpm` for the
benchmark driver. Upgrade by installing the corresponding packages from a newer bundle. Remove
them with `sudo dnf remove simrv simrv-tools simrv-benchmark`.

## Debian and Ubuntu

The DEB bundle includes a `Packages` dependency manifest. Install the runtime package with APT so
host-library dependencies are resolved normally:

```bash
mkdir simrv-deb && tar -xzf SimRV-deb-packages-vVERSION.tar.gz -C simrv-deb
sudo apt install ./simrv-deb/simrv_*_amd64.deb
simrv --version
```

The optional tools and benchmark packages depend on `simrv`. Install their `.deb` files with the
same command when needed. Upgrade from a newer bundle or remove the packages with
`sudo apt remove simrv simrv-tools simrv-benchmark`.

## Build from source

Source builds require a C++23 compiler, CMake 3.31 or newer, and Ninja:

```bash
git clone https://github.com/archlab-sciencetokyo/SimRV.git
cd SimRV
cmake --preset rv64-release
cmake --build --preset rv64-release -j"$(nproc)"
./build/rv64-release/simrv --version
```

To install the runtime, tools, and documentation below `/usr/local`:

```bash
sudo cmake --install build/rv64-release --prefix /usr/local
simrv --version
```

Build `rv32-release` only when a native RV32 strict-width implementation is required for
verification. Normal users should use the RV64-capable build.

## Next steps

- Follow the [CLI quickstart](index.md) to run a bare-metal image.
- Read [Bare-Metal and Embedded](baremetal.md) for physical-memory and platform details.
- Read [Full-System Linux](linux.md) to build and boot a Linux image.
