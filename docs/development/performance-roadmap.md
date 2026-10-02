# Instruction-Fast Performance Roadmap

## Handover baseline

The comparison baseline is Spike `7ab2efd6785e` and the CoreMark ELF with SHA-256
`882bb1ed8fecba092d649f4b7a4cc66df5a989f27b0732f42477989ae47ce240`. Both simulators must use
the canonical contract `rv64gc_zicsr_zifencei_zicntr`. CoreMark retires 1,682,311,351 instructions
before its successful `tohost` exit.

On the current development host, the matched full-workload smoke test measured approximately
9.15 seconds for the portable SimRV RV64 release and 3.46 seconds for Spike. A lower-noise,
fixed-work comparison over 20 million instructions measured 100.9 ms and 42.1 ms respectively,
or a 2.40x gap. Clang, LTO, and native-host tuning did not improve SimRV, so compiler selection is
not the next optimization lever.

The earlier saved `build/coremark-simrv-30runs.json` report is not comparative evidence: its Spike
sample arrays are empty. Do not use the historical "within 10%" estimate as a baseline.

## Reproduce the comparison

Build the native and portable release configurations, then use the same ISA string and stopping
policy for both engines. Set `COREMARK_ELF` to the CoreMark ELF under test:

```bash
export COREMARK_ELF=/path/to/coremark.riscv
```

```bash
cmake --preset rv64-release
cmake --build --preset rv64-release -j8

hyperfine --warmup 5 --runs 30 \
  --export-json build/hyperfine-coremark-20m-shared-isa.json \
  --command-name "SimRV 20M" \
  'build/rv64-release/simrv --cli --isa rv64gc_zicsr_zifencei_zicntr -m "$COREMARK_ELF" -e 20000000 -b -H 0x80001000' \
  --command-name "Spike 20M" \
  'spike --isa=rv64gc_zicsr_zifencei_zicntr --instructions=20000000 "$COREMARK_ELF"'

python3 scripts/benchmark.py \
  --simrv build/rv64-release/simrv \
  --spike "$(command -v spike)" \
  --test "$COREMARK_ELF" \
  --runs 20 --warmups 3 --timeout 30 \
  --json build/coremark-shared-isa.json
```

Record the host CPU, governor, compiler versions, SimRV revision, Spike revision, ELF hash, ISA
contract, raw samples, and command lines with every result. Prefer a pinned CPU and an otherwise
idle host. Treat WSL measurements as local regression evidence rather than portable claims.

## Optimization path

### 1. Establish profiles and invariants

- Capture `perf stat` counters for instructions, cycles, branches, branch misses, and cache misses.
- Capture an instruction-fast flame graph for both a 20M fixed-work run and full CoreMark.
- Record decode-cache hit rate and the hottest guest operation classes without enabling per-retire
  logging in the measured run.
- Add a performance-report field for simulator and guest instruction counts. Reject comparisons
  when the requested stopping policy differs.

Exit criterion: three repeatable profiles agree on the top costs, and fixed-work results have less
than 5% coefficient of variation.

### 2. Tighten the cached instruction path

- Audit `CPU::execute_cached_op_fast`, its chunk loop, and decode-cache miss transition.
- Hoist debugger, tracing, interrupt, lifecycle, and stop checks out of the per-instruction path
  when their features are disabled.
- Reduce repeated address-wrapper conversions and architectural-state loads in integer ALU,
  branch, and direct-RAM operations.
- Keep uncommon traps, MMIO, translation misses, and self-modifying-code invalidation on cold
  paths.

Exit criterion: at least 20% improvement on the 20M CoreMark run with no semantic-gate failures.

### 3. Specialize memory and branch-heavy execution

- Profile direct DRAM load/store checks separately from MMU and MMIO paths.
- Cache the validated physical-RAM window in the fast path while preserving bounds, PMP, and
  misalignment behavior.
- Measure branch-target and compressed-instruction handling before changing prediction or decode
  structures.
- Consider larger straight-line cached blocks only after invalidation, interrupt latency, and
  precise-trap tests exist for block boundaries.

Exit criterion: Spike is less than 1.75x faster on fixed-work CoreMark, with Dhrystone and the
real-world suite showing no material regression.

### 4. Evaluate structural execution blocks

If the remaining profile is dominated by per-instruction dispatch, introduce an optional
basic-block executor backed by the existing decoded-operation cache. Blocks must terminate at
control flow, traps, potentially faulting operations, debugger observation points, interrupts,
instruction limits, and code invalidation boundaries. Do not weaken GDB stepping, watchpoints,
Spike lockstep, or architectural exception precision to improve headline throughput.

Exit criterion: less than 1.5x Spike time across a multi-workload geometric mean. Treat parity
within 10% as a stretch goal that requires evidence across more than CoreMark.

## Required gates for every optimization

```bash
ctest --test-dir build/rv64-release --output-on-failure -L gate -E pty
ctest --test-dir build/rv32-release --output-on-failure -L gate -E pty
```

Also run the GDB stub tests, semantic equivalence suite, RV32/RV64 ISA suites, and matched
fixed-work benchmarks. Keep optimization commits small enough to bisect, and attach before/after
JSON reports to the review rather than committing generated benchmark artifacts.
