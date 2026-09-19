# RTL parity workflow

`scripts/evaluate_rtl_parity.py` is the common entry point for comparing an RTL
target with SimRV. It currently provides adapters for RVComp and CFU Proving
Ground:

```bash
python3 scripts/evaluate_rtl_parity.py --list-targets

python3 scripts/evaluate_rtl_parity.py rvcomp \
  --rvcomp-dir ../RVComp \
  --simrv-bin build/rv32-release/SimRV \
  --trace-dir build/rvcomp_parity_traces

python3 scripts/evaluate_rtl_parity.py cfu-pg \
  --cfu-dir ../CFU-Proving-Ground \
  --simrv-bin build/rv32-release/SimRV \
  --trace-dir build/cfu_pg_parity_traces
```

Both adapters compile the same focused C benchmarks, run RTL and SimRV, compare
retirement traces, and emit JSON results. RVComp retains its detailed cache/IFU
diagnostics in `evaluate_rvcomp.py`.

The entry point is intentionally only a dispatcher. Target metadata lives in
`scripts/rtl_parity/registry.py`, shared benchmarks and trace parsing live in
`scripts/rtl_parity/common.py`, and target implementations live under
`scripts/rtl_parity/targets/`. Adapters are imported lazily, so listing targets
does not require any target's RTL checkout or build tools.

## Adapter contract

An RTL adapter is responsible for:

1. Building the benchmark for the target memory map and ISA.
2. Producing a simulator image without modifying the source RTL checkout.
3. Emitting retirement records in this format when parity tracing is enabled:

   ```text
   CYCLETRACE cycle=<decimal> inst=<decimal> pc=<hex> ir=<hex>
   ```

4. Reporting an unambiguous cycle and retirement checkpoint.
5. Running SimRV with the corresponding CPU model and termination address.

The CFU-PG adapter demonstrates an isolated adapter: it copies RTL into
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

## Adding another target

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
