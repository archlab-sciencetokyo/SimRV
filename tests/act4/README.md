# RVA23 ACT4 tests

This directory contains SimRV's DUT configuration for the upstream RISC-V
Architecture Certification Tests (ACT4) RVA23S64 suite.

## Configuration & Prerequisites

The test runner resolves the ACT4 repository path via the `RISCV_ARCH_TEST_DIR`
environment variable.

To configure an external checkout:

```sh
export RISCV_ARCH_TEST_DIR="/path/to/riscv-arch-test"
```

Install the ACT4 prerequisites documented by the upstream project, including
its Python dependencies, a RISC-V GCC toolchain, and the Sail RISC-V simulator.
Make those executables available on `PATH`, then build SimRV.

## Generate and run the suite

From the project root:

```sh
ACT_DIR="${RISCV_ARCH_TEST_DIR:?Set RISCV_ARCH_TEST_DIR to the riscv-arch-test checkout}"
ACT_REVISION="$(git -C "$ACT_DIR" rev-parse HEAD)"
scripts/check-act4-upstream.sh "$ACT_REVISION"
ACT_WORKDIR="${SIMRV_ACT4_WORKDIR:-$PWD/../../tests/act4/$ACT_REVISION}"

make -C "$ACT_DIR" \
  CONFIG_FILES="$PWD/tests/act4/simrv-rva23s64/test_config.yaml" \
  CERTIFICATE=RVA23 JOBS=12 \
  WORKDIR="$ACT_WORKDIR"

ACT_OUTPUT="$ACT_WORKDIR/rva23s64"
ELF_COUNT="$(find "$ACT_OUTPUT/elfs" -type f -name '*.elf' | wc -l)"
{
  printf 'act_revision=%s\n' "$ACT_REVISION"
  printf 'profile=RVA23S64\n'
  printf 'vlen=128\n'
  printf 'elf_count=%s\n' "$ELF_COUNT"
  printf 'config_sha256=%s\n' \
    "$(sha256sum "$PWD/tests/act4/simrv-rva23s64/test_config.yaml" | cut -d ' ' -f 1)"
} > "$ACT_OUTPUT/simrv-act4-provenance.txt"

simrv-act \
  --simrv build/rv64-debug/simrv \
  --elf-dir "$ACT_OUTPUT/elfs" \
  --report "$ACT_OUTPUT/simrv-report.json" \
  --jobs 12 --compare-detailed-on-failure
```

By default, generated output is stored in the shared workspace test folder
`../../tests/act4/<ACT commit>`, outside the repository build tree. The ACT
revision is the only version-specific directory; profile output uses the
generic `rva23s64` name, without a simulator-specific namespace. Set
`SIMRV_ACT4_WORKDIR` to override the output directory. Each suite also records
its upstream commit, RVA23 profile, VLEN, ELF count, and profile configuration
hash in `simrv-act4-provenance.txt` beside the ELFs and report.

The optional `--compare-detailed-on-failure` flag reruns failed ELFs without instruction batching.
It helps separate issues in the fast path from behavior shared with instruction-detailed execution;
the fast-mode result still determines the report totals and exit status.

The generated Sail configuration fixes VLEN at 128 bits; the runner passes the
same `--vlen 128` setting to SimRV. Generated ACT4 ELFs, logs, and reports stay
in `ACT_WORKDIR`, separate from disposable CMake build output; do not commit the
generated binaries.

The rc.19 workflow resolves the current upstream `act4` branch head when the run
starts and checks that it has not moved before and after qualification. The latest locally
verified head is `1c3e9f2a89e103eeed0a3e6ba1bc699f100fd481` (2026-11-14). Its full
RVA23S64 suite generated 3202 ELFs at VLEN=128 and passed **3202/3202**, with zero
failures or timeouts. Release qualification requires every generated ACT4 ELF to
pass. See
[`docs/architecture/compliance.md`](../../docs/architecture/compliance.md) for
the qualification status and findings.

This test run is useful implementation evidence; it is not RISC-V
certification.
