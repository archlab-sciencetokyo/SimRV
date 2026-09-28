# SimRV Research Companion

This directory defines the reproducible experiment interface for SimRV 3.0. External projects are
pinned in `release/release-manifest.json` and downloaded into ignored directories; their licenses
do not automatically permit bundling their binaries or workloads with SimRV.

## Prepare

Requirements are Linux x86-64, CMake, Ninja, GCC 16+ or Clang 22+, Python 3.10+, Git, Go, a RISC-V
cross-compiler, and enough space to build Linux, OpenSBI, Spike, and vector tests.

Install the locked Python development tools and validate the checked-in manifests:

```bash
uv sync --dev
uv run --frozen python scripts/validate_schemas.py
```

```bash
python3 scripts/reproduce.py --prepare --build-vector-tests
```

Build Linux inputs separately for each XLEN. The script downloads the pinned source versions from
their upstream projects:

```bash
scripts/build-linux-image.sh --arch rv32
scripts/build-linux-image.sh --arch rv64
```

No upstream source, workload, image, or binary is included in a release bundle. Retain its license
and obtain it from the URL recorded in the release manifest.

## Run

```bash
uv run --frozen python scripts/reproduce.py --mode quick --output repro/results
uv run --frozen python scripts/reproduce.py --mode full --output repro/results \
  --riscv-tests-dir /path/to/riscv-tests \
  --vector-tests-dir /path/to/riscv-vector-tests \
  --spike /path/to/spike
```

Quick mode validates metadata and locally configured regression tests. Full mode performs clean
RV32/RV64 builds and required correctness suites, then runs configured performance workloads.
Required dependencies that are absent are reported as `unavailable` and make full evidence fail.

Full mode validates manifests, performs the clean correctness matrix, runs exactly the declared
workloads, generates deterministic bootstrap confidence intervals/tables/plots, optionally compares
a frozen baseline, verifies merged evidence, and packages the tracked source plus results. Add
`--baseline /path/to/aggregate.json` for comparison, `--archive PATH` to name the submission bundle,
or `--no-package` while developing the experiment.

The experiment manifest records XLEN, ISA, VLEN, execution mode, simulator arguments, repetitions,
warmups, timeouts, workloads, and output locations. Raw results are immutable inputs. Aggregate
JSON, Markdown, and SVG files are regenerated deterministically with `scripts/benchmark.py
aggregate`.

Each raw benchmark result includes a stable configuration fingerprint, exact SimRV and Spike
commands, binary and workload SHA-256 digests, simulator versions, repository revision, and host
identity. The fingerprint uses only the architectural and stopping configuration, allowing results
from different hosts to be grouped without discarding provenance.

## Release bundle

`python3 scripts/reproduce.py --package --output <path>` packages the complete tracked source tree,
locked Python environment, manifest, schemas, raw
results, derived tables/figures, evidence, logs, checksums, and license metadata. Inspect the
generated file list before publishing. The bundle deliberately excludes downloaded dependencies,
guest images, and generated executables.
