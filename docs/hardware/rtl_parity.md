# Reference parity workflow

SimRV uses separate checks for architectural behavior and cycle timing. Spike
lockstep compares committed instructions and architectural state; cycle-trace
comparison checks timing behavior against a cycle-producing implementation or
captured hardware trace. `scripts/evaluate_rtl_parity.py` is the current cycle
parity runner. Its checked-in adapters use two example targets:
[ArchLab RVComp](https://github.com/archlab-sciencetokyo/RVComp), described in
the paper [“Design and implementation of a high-performance RISC-V SoC for
FPGAs with Linux support”](https://www.ieice.org/publications/ken/summary.php?contribution_id=138850&expandable=0&ken_id=R&lang=en&presen_date=2025-09-19&schedule_id=8813&society_cd=ESSNLS&year=2025),
and ArchLab's [CFU Proving Ground](https://github.com/archlab-sciencetokyo/CFU-Proving-Ground),
described in [“CFU Proving Ground: a framework for efficiently utilizing custom
instructions in RISC-V soft processors”](https://www.ieice.org/publications/ken/summary.php?contribution_id=137514&expandable=3&ken_id=DC&lang=en&presen_date=2025-06-10&schedule_id=8710&society_cd=ISS&year=2025):

```bash
python3 scripts/evaluate_rtl_parity.py --list-targets

python3 scripts/evaluate_rtl_parity.py rvcomp \
  --rvcomp-dir /path/to/RVComp \
  --simrv-bin build/rv32-release/simrv \
  --trace-dir build/rvcomp_parity_traces

python3 scripts/evaluate_rtl_parity.py cfu-pg \
  --cfu-dir /path/to/CFU-Proving-Ground \
  --simrv-bin build/rv32-release/simrv \
  --trace-dir build/cfu_pg_parity_traces
```

Both adapters compile the same focused C benchmarks, run the target and SimRV,
compare retirement traces, and emit JSON results. The target may be a hardware
description simulator or another implementation that can provide the documented
cycle trace. These targets demonstrate the adapter workflow; they are not
requirements for using SimRV's general timing models or parity contract. The
RVComp adapter retains detailed cache and IFU diagnostics in `evaluate_rvcomp.py`.

!!! info "CFU Playground and CFU Proving Ground are separate projects"
    The checked-in `cfu-pg` adapter targets ArchLab's CFU Proving Ground above. Google's separate
    [CFU Playground repository](https://github.com/google/CFU-Playground) and its paper,
    [“CFU Playground: Full-Stack Open-Source Framework for Tiny Machine Learning (TinyML) Acceleration on FPGAs”](https://arxiv.org/abs/2201.01863),
    are another example of an FPGA hardware/software co-design platform; they are not the target
    of this adapter. The generic adapter contract below applies to either project or another
    cycle-producing reference.

The entry point is intentionally only a dispatcher. Target metadata lives in
`scripts/rtl_parity/registry.py`, shared benchmarks and trace parsing live in
`scripts/rtl_parity/common.py`, and target implementations live under
`scripts/rtl_parity/targets/`. Adapters are imported lazily, so listing targets
does not require any target's RTL checkout or build tools.

## Cycle-reference adapter contract

An adapter for a cycle-producing reference is responsible for:

1. Building the benchmark for the target memory map and ISA.
2. Producing a simulator image without modifying the source RTL checkout.
3. Emitting retirement records in this format when parity tracing is enabled:

   ```text
   CYCLETRACE cycle=<decimal> inst=<decimal> pc=<hex> ir=<hex>
   ```

4. Reporting an unambiguous cycle and retirement checkpoint.
5. Running SimRV with the corresponding CPU model and termination address.

The CFU-PG adapter demonstrates an isolated hardware-description adapter: it copies the source into
`build/rtl_parity/cfu-pg`, generates its compile-time instruction/data memories,
and injects trace-only instrumentation into that temporary copy. Existing files
or local changes in the CFU-PG checkout are not overwritten.

## Parity levels

Keep these results separate in reports:

- architectural parity: common retirement PCs/instructions are identical;
- trace extent: event-count delta and the first unpaired event at each target's
  termination boundary;
- retirement timing parity: individual retirement cycles are identical;
- checkpoint cycle parity: total cycles at the shared termination event match;
- counter parity: implementation-specific performance counters match.

Checkpoint parity does not imply identical retirement timing. Blocking fetch
hardware can retire bursts differently from SimRV's semantic pipeline while
still producing the same architectural sequence and total cycle count.

!!! info "Report parity at the level you measured"
    Matching final cycle counts alone does not establish cycle-by-cycle equivalence. State whether
    a result covers architectural behavior, trace extent, retirement timing, checkpoint cycles, or
    implementation-specific counters.

## Adding another cycle-producing reference

1. Add a `TargetAdapter` record to `scripts/rtl_parity/registry.py`.
2. Implement a module under `scripts/rtl_parity/targets/` exposing
   `run(arguments) -> int` and an adapter-specific argument parser.
3. Reuse `rtl_parity.common` for the benchmark corpus, toolchain discovery, and
   retirement-trace comparison.
4. Create a model under `configs/models/` and add focused regression tests.

Keep source RTL checkouts read-only: copy or generate instrumented sources under
`build/rtl_parity/<target>`. Prefer configurable timing, predictor, and cache
capabilities over target-name checks in SimRV. Calibrate on focused
microbenchmarks, then confirm the model on programs that were not used for
tuning.
