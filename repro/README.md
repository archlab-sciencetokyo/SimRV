# SimRV Research Companion

This directory defines the reproducible experiment interface for SimRV 2.0. External projects are
pinned in `release/release-manifest.json` and downloaded into ignored directories; their licenses
do not automatically permit bundling their binaries or workloads with SimRV.

## Prepare

Requirements are Linux x86-64, CMake, Ninja, GCC 14+ or Clang 20+, Python 3.10+, Git, Go, a RISC-V
cross-compiler, and enough space to build Linux, OpenSBI, Spike, and vector tests.

Install the locked Python development tools and validate the checked-in manifests:

```bash
uv sync --dev
uv run --frozen python scripts/validate_schemas.py
```

```bash
python3 scripts/prepare_repro.py --build-vector-tests
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
python3 scripts/reproduce.py --mode quick --output repro/results
python3 scripts/reproduce.py --mode full --output repro/results
```

Quick mode validates metadata and locally configured regression tests. Full mode performs clean
RV32/RV64 builds and required correctness suites, then runs configured performance workloads.
Required dependencies that are absent are reported as `unavailable` and make full evidence fail.

The experiment manifest records XLEN, ISA, VLEN, execution mode, simulator arguments, repetitions,
warmups, timeouts, workloads, and output locations. Raw results are immutable inputs. Aggregate
JSON, Markdown, and SVG files are regenerated deterministically with
`scripts/aggregate_experiments.py`.

Each raw benchmark result includes a stable configuration fingerprint, exact SimRV and Spike
commands, binary and workload SHA-256 digests, simulator versions, repository revision, and host
identity. The fingerprint uses only the architectural and stopping configuration, allowing results
from different hosts to be grouped without discarding provenance. `--isa` is the canonical
simulator option; `--misa` remains a compatibility alias for older main-branch commands.

## Release bundle

`scripts/package_repro.py` packages the manifest, schemas, scripts, source documentation, raw
results, derived tables/figures, evidence, logs, checksums, and license metadata. Inspect the
generated file list before publishing. The bundle deliberately excludes downloaded dependencies,
guest images, and generated executables.
