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

make -C "$ACT_DIR" \
  CONFIG_FILES="$PWD/tests/act4/simrv-rva23s64/test_config.yaml" \
  CERTIFICATE=RVA23 JOBS=12 \
  WORKDIR="$PWD/build/act4-rva23s64"

python3 scripts/run_act4_tests.py \
  --simrv build/rv64-debug/simrv \
  --elf-dir build/act4-rva23s64/simrv-rva23s64/elfs \
  --report build/act4-rva23s64/simrv-rva23s64/simrv-report.json \
  --jobs 12
```

The generated Sail configuration fixes VLEN at 128 bits; the runner passes the
same `--vlen 128` setting to SimRV. The generated suite and report belong under
`build/` and are not committed.

In the current full-suite run, SimRV passes 3138 of 3188 ELFs. The remaining
50 checks are audited conflicts with the ratified specs: 42 vector tests expect
successful instructions to retain nonzero `vstart` ([Vector 1.0 requires all
vector instructions to clear it on completion](https://docs.riscv.org/reference/isa/v20260120/unpriv/v-st-ext.html)),
and 8 interrupt tests expect VSEI above SEI for HS-mode ([H 1.0 orders SEI above
VSEI for HS-mode destinations](https://docs.riscv.org/reference/isa/v20260120/priv/hypervisor.html)).
The runner intentionally reports these as failures; do not change architectural
behavior to satisfy those expectations. See
[`docs/architecture/compliance.md`](../../docs/architecture/compliance.md) for
qualification scope and status.

This test run is useful implementation evidence; it is not RISC-V
certification.
