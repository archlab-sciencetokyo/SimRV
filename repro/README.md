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

On a benchmark server, run the preflight first. Its defaults use dependencies prepared under
`.cache/repro` and Linux images under `linux-images`:

```bash
uv run --frozen python scripts/run_paper_server.py --preflight-only
uv run --frozen python scripts/run_paper_server.py
```

Override `--riscv-tests-dir`, `--vector-tests-dir`, `--linux-images-root`, or `--spike` when the
server keeps shared dependencies elsewhere. The executed run stores `server-preflight.json` inside
the packaged results so the submission records the resolved tools and inputs.

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

The full command implements the six artifact stages:

1. Validate the experiment/release manifests and pinned metadata.
2. Build GCC and Clang RV32/RV64 configurations and collect correctness evidence.
3. Run exactly the performance configurations and workloads declared in the experiment manifest.
4. Generate deterministic bootstrap confidence intervals, a Markdown table, and an SVG plot.
5. Optionally compare against a frozen aggregate baseline with the declared regression policy.
6. Merge and verify evidence, write an index, and create a deterministic source-and-results archive
   plus SHA-256 checksum.

To include the optional baseline comparison:

```bash
uv run --frozen python scripts/reproduce.py --mode full --output repro/results \
  --riscv-tests-dir /path/to/riscv-tests \
  --vector-tests-dir /path/to/riscv-vector-tests \
  --spike /path/to/spike \
  --baseline /path/to/frozen-baseline-aggregate.json
```

By default the archive is `SimRV-paper-artifact.tar.gz`. Use `--archive PATH` to select the
submission filename, or `--no-package` while developing the experiment. Performance thresholds are
evidence-only unless the manifest policy is changed to `enforced`; correctness failures always stop
the workflow.

The experiment manifest records XLEN, ISA, VLEN, execution mode, simulator arguments, repetitions,
warmups, timeouts, workloads, and output locations. Raw results are immutable inputs. Aggregate
JSON, Markdown, and SVG files are regenerated deterministically with
`scripts/aggregate_experiments.py`.

Each raw benchmark result includes a stable configuration fingerprint, exact SimRV and Spike
commands, binary and workload SHA-256 digests, simulator versions, repository revision, and host
identity. The fingerprint uses only the architectural and stopping configuration, allowing results
from different hosts to be grouped without discarding provenance. `--isa` is the canonical
simulator option; `--misa` remains a compatibility alias for older main-branch commands.

The archive contains the complete Git-tracked source tree, locked Python environment, manifests,
schemas, raw measurements, derived outputs, correctness evidence, and artifact index. Downloaded
toolchains, Spike, test suites, and Linux images are not redistributed; their pinned revisions and
local provenance remain recorded.

## Release bundle

`scripts/package_repro.py` packages the manifest, schemas, scripts, source documentation, raw
results, derived tables/figures, evidence, logs, checksums, and license metadata. Inspect the
generated file list before publishing. The bundle deliberately excludes downloaded dependencies,
guest images, and generated executables.
