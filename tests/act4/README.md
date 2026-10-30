# RVA23 ACT4 tests

This directory contains SimRV's DUT configuration for the upstream RISC-V
Architecture Certification Tests (ACT4) RVA23S64 suite. The upstream ACT4
repository is kept at `tests/riscv-arch-test`.

## Generate and run the suite

Install the ACT4 prerequisites documented by the upstream project, including
its Python dependencies, a RISC-V GCC toolchain, and the Sail RISC-V simulator.
Make those executables available on `PATH`, then build SimRV. From the project
root, generate the RVA23 suite and run the resulting ELFs with:

```sh
make -C tests/riscv-arch-test \
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
successful instructions to retain nonzero `vstart` (Vector 1.0 requires it to
clear), and 8 interrupt tests expect VSEI above SEI for HS-mode (H 1.0 specifies
SEI above VSEI). The runner intentionally reports these as failures; do not
change architectural behavior to satisfy those expectations. See
[`docs/architecture/compliance.md`](../../docs/architecture/compliance.md) for
qualification scope and status.

This test run is useful implementation evidence; it is not RISC-V
certification.
