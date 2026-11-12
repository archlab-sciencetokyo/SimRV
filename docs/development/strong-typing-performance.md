# Strong-typing migration: local performance observations

These measurements are intermediate comparisons, not qualification of the final tree. They were
collected on an Intel Core Ultra X7 358H under x86-64 WSL2, with one pinned CPU, three warmups and
ten measured runs. Values are median elapsed seconds; variability is the coefficient of variation
(CV). Runs use matching guest, instruction limit and simulator options within each comparison.

| Intermediate comparison | Before (seconds; CV) | After (seconds; CV) | Median change |
| --- | ---: | ---: | ---: |
| Compute, 100 million instructions | 0.132981; 1.58% | 0.134141; 1.45% | +0.87% |
| Branch, 100 million instructions | 0.168151; 18.43% | 0.159478; 21.15% | -5.16% |
| Memory | 0.396128; 1.44% | 0.397653; 0.97% | +0.39% |
| CoreMark | 0.039004; 4.52% | 0.039327; 3.86% | +0.83% |
| Linux boot | 0.579281; 1.70% | 0.580733; 0.93% | +0.25% |
| MMIO hole correctness fix, 1 million instructions, cycle-accurate | 0.163700; 2.82% | 0.170874; 4.02% | +4.38% |
| MMIO hole correctness fix, Linux boot, 25 million instructions | 0.591918; 3.37% | 0.604616; 4.04% | +2.15% |
| Typed device address interface, MMIO loop | 0.165212; 5.08% | 0.169051; 3.56% | +2.32% |
| Serial interface and subsequent changes, MMIO loop | 0.171195; 3.80% | 0.171040; 2.70% | -0.09% |
| Serial interface and subsequent changes, compute | 0.140114; 3.66% | 0.137543; 3.57% | -1.84% |
| Serial interface and subsequent changes, branch | 0.162191; 4.38% | 0.164068; 4.08% | +1.16% |
| Serial interface and subsequent changes, memory | 0.137802; 2.57% | 0.135065; 2.96% | -1.99% |
| Serial interface and subsequent changes, Linux boot | 0.603079; 1.65% | 0.599469; 1.01% | -0.60% |

For the physical-address reservation interface, an LR/SC loop with two harts and a 10 million
instruction limit used three warmups and 20 interleaved runs per binary on one pinned CPU. Median
elapsed time was 0.399677 seconds before (10.89% CV) and 0.401831 seconds after (10.63% CV), a
0.54% increase. Both runs used the RV64 release build, `rv64gc_zicsr_zifencei_zicntr`, and the
same fixed-instruction stop policy. Variability exceeds the 5% investigation threshold, so this
result cannot establish a small performance difference.

After the guest-physical address and framebuffer-interface changes, a 25 million instruction
Linux boot comparison used three warmups and ten interleaved runs per binary on one pinned CPU.
The median was 0.617467 seconds before (12.04% CV) and 0.618443 seconds after (17.38% CV), a
0.16% increase; the high variability makes small differences inconclusive. The RV64 executables
were 42,153,712 and 42,166,104 bytes respectively. ARM hardware remained unavailable.

Carrying the guest-physical type through fault reporting was compared on the same Linux boot
workload with three warmups and ten interleaved runs per binary on one pinned x86 CPU. The median
was 0.731287 seconds before (1.86% CV) and 0.730272 seconds after (1.74% CV), a 0.14% decrease.
The binaries were 42,166,624 and 42,166,744 bytes respectively.

For the multi-hart cycle barrier fix, a two-hart, multi-threaded cycle-accurate memory workload
used a 500,000-instruction limit, two pinned x86 CPUs, three warmups and 30 interleaved runs per
binary. The median was 0.291273 seconds before (15.14% CV) and 0.294423 seconds after (15.87% CV),
a 1.08% increase. Each completed run retired 500,001 instructions. A prior shorter comparison
timed out on the baseline, while 100 pinned runs of the updated binary completed. The high timing
variability makes small performance differences inconclusive.

For the RAM range checks, the benchmark workflow ran the `memory.elf` workload for 100 million
instructions in the RV64 release build, with three warmups and ten runs per binary on one pinned
x86 CPU. Median wall time was 0.128999 seconds before (2.24% CV) and 0.127323 seconds after
(1.13% CV); both retired 100 million instructions and reported 100,000,001 simulated cycles. This
comparison does not establish a speedup. The before and after samples ran in separate batches.

A later intermediate comparison used the retained binary from before the RAM-boundary fix and
the current working-tree RV64 release binary. On the same x86 host, `scripts/benchmark.py` ran
three warmups and ten measured runs per binary, pinned to CPU 0, with ISA
`rv64gc_zicsr_zifencei_zicntr` and a 100 million instruction limit. Median seconds (CV) were:

| Workload | Before | After |
| --- | ---: | ---: |
| Compute | 0.1636 (3.55%) | 0.1556 (3.62%) |
| Branch | 0.1814 (3.19%) | 0.1772 (1.80%) |
| Memory | 0.1532 (1.64%) | 0.1539 (4.52%) |

Each run reported 100 million instructions and 100,000,001 simulated cycles. The binaries were
42,165,504 and 42,208,944 bytes, with SHA-256 prefixes `5f6d0ca313c1` and `766e2a6a421f`.
Samples ran in separate batches, so these results do not establish small speedups. This is not
the original migration baseline. The reports are
`/tmp/simrv-intermediate-{compute,branch,memory}-{before,after}.json` when retained locally.

The same binaries were also run on the Alpine Linux boot workload without a disk, with a
25 million instruction limit, three warmups and ten interleaved runs on one pinned CPU. Median
wall times were 0.733139 seconds (1.67% CV) before and 0.718217 seconds (1.66% CV) after. The
small difference does not establish a speedup.

For the typed PMP interface, the same x86 benchmark workflow ran `memory.elf` for 100 million
instructions with three warmups and ten measured runs per binary on CPU 0. Median times were
0.1360 seconds (2.93% CV) before and 0.1341 seconds (2.94% CV) after. The two series ran in
separate batches and do not establish a speedup. An earlier run overlapped an RV32 link and had
CV above 10%, so it was discarded.

For the page-walker address changes, an Alpine Linux boot without a disk used a 25 million
instruction limit, three warmups and ten interleaved runs on one pinned x86 CPU. Median times
were 0.577742 seconds (0.82% CV) before and 0.578195 seconds (0.63% CV) after. The before binary
also predates the preceding page-walk fault-state cleanup. Executable sizes were 42,206,752 and
42,206,712 bytes. The 0.08% difference does not establish a performance change.

For the VirtIO DMA address checks, the diskless Alpine boot comparison used the same 25 million
instruction limit, three warmups and ten interleaved runs on one pinned x86 CPU. The medians were
0.580210 seconds (1.78% CV) before and 0.577160 seconds (1.23% CV) after. The baseline also
predates the cache-prewarming changes. This workload does not exercise VirtIO disk traffic and
the difference does not establish a speedup; a device-throughput comparison remains outstanding.

For the precise-tick batch eligibility guard, `memory.elf` ran without a ticking plugin using
the RV64 release build, a 100 million instruction limit, three warmups and ten measured runs per
binary on one pinned x86 CPU. Median times were 0.1338 seconds (2.59% CV) before and 0.1359
seconds (2.07% CV) after. The separate batches do not establish a small performance change. A
ticking extension intentionally uses the per-instruction runner; its throughput is not measured.

The branch result is too noisy to establish a speedup. The MMIO loop retained the same bus and
cache counts. The initial baseline executable is no longer available, so an identical-binary
comparison against the final tree remains outstanding. ARM hardware and usable host performance
counters were unavailable. The raw local reports and workload sources are in the ignored
`benchmark_logs/strong-typing-2026-11-05/` directory when present; they are not release artifacts.

A later x86 comparison used the retained pre-precise-tick-guard intermediate binary and the
RV64 release binary at `1bc43ca`. Both ran the same bare-metal guest, ISA
`rv64gc_zicsr_zifencei_zicntr`, and 100 million instruction limit on pinned CPU 0, with three
warmups and ten interleaved samples. Compute medians were 0.116722 and 0.097635 seconds
(CV 1.32% and 7.21%); branch medians were 0.141816 and 0.162783 seconds (1.31% and 2.64%);
memory medians were 0.125828 and 0.118740 seconds (1.34% and 3.53%). A repeat of the branch
workload with five warmups and 30 interleaved samples produced medians of 0.144493 and 0.159806
seconds (2.57% and 3.25%), a 10.60% regression. The branch guest alternates a conditional branch
in a short loop. These binaries differ in several execution and device changes, so the cause is
not isolated; profiling or a controlled bisection is still needed. The compute result has high
variability and should not be treated as a reliable speedup. The binaries were 42,718,352 and
45,329,600 bytes, with SHA-256 prefixes `4987b5030cf7` and `da35e96dcd80`, respectively.
This intermediate comparison does not replace the missing original baseline or ARM measurements.

To narrow the branch result, revision `4a2a3c0` was built separately with GCC 16.2.1,
`-O3 -DNDEBUG`, `-march=x86-64-v3`, and LTO disabled. With the same guest and stop policy,
five warmups and 30 interleaved samples on CPU 0, the retained pre-guard binary and `4a2a3c0`
had medians of 0.147311 and 0.184401 seconds (CV 3.77% and 2.22%). A separate interleaved run
of `4a2a3c0` and `1bc43ca` produced 0.182681 and 0.161015 seconds (CV 2.40% and 1.50%).
The slowdown therefore predates the later block-chaining changes, which improved this workload
relative to `4a2a3c0`; the individual earlier change responsible remains unknown. The intermediate
binary was 43,319,256 bytes with SHA-256 prefix `b82cf11e688b`. The preceding revision
`bc258a4` could not be built because its bit-manipulation functions were defined twice.

A native C++ subprocess adapter compared the build immediately before the engine split
(`9e183b7`) with `4a2a3c0` using the same branch guest, ISA and 100 million instruction limit.
With five warmups and 30 interleaved samples pinned to x86 CPU 10, median wall times were
0.141622 seconds (2.22% CV) and 0.180720 seconds (2.29% CV), respectively. Both binaries used
GCC 16.2.1, `-O3 -DNDEBUG`, `-march=x86-64-v3`, and no LTO. This locates the regression in the
engine-split interval, which also contains other correctness and execution changes; it does not
identify the responsible instruction or justify reverting those changes. The pre-split binary was
45,411,672 bytes with SHA-256 prefix `e5847d0acbc0`. Two attempts on CPU 0 had over 30% CV under
host load and were discarded.

An x86 experiment checked a cached decoded branch target before looking up a basic block. On CPU
15, five warmups and 30 interleaved branch samples gave medians of 0.167503 and 0.165137 seconds
(CV 2.69% and 2.70%). The candidate also included a CLI summary formatting change, so this was
not a perfectly isolated comparison. The difference was too small to justify changing dispatch;
the experiment was discarded. A separate run on CPU 10 had CV above 24% and was discarded.

The IALIGN=32 correctness fix was compared against the retained pre-fix RV64 binary on the
compressed branch workload, using the native C++ subprocess adapter, five warmups and 30
interleaved samples on x86 CPU 15. Median times were 0.162476 seconds (4.97% CV) before and
0.157763 seconds (2.58% CV) after. The result shows no material regression on this workload;
the variability and a concurrent CLI summary change prevent attributing the small difference to
the guard. SHA-256 prefixes were `da35e96dcd80` and `802f2bb0b512`.

For the typed TileLink/coherence addresses, resumable transfer address, and instruction explainer
PC changes, an RV64 release executable comparison used the `compute.elf`, `branch.elf`, and
`memory.elf` guests from `benchmark_logs/strong-typing-2026-11-05/`. Each run used
`rv64gc_zicsr_zifencei_zicntr`, the fast bare-metal engine, and a 100 million instruction limit.
Hyperfine ran five warmups and 30 measured samples pinned to x86 CPU 0. Medians in seconds and
CVs were:

| Workload | Before | After |
| --- | ---: | ---: |
| Compute | 0.116711 (1.52%) | 0.118295 (2.36%) |
| Branch | 0.352744 (17.55%) | 0.259118 (9.85%) |
| Memory | 0.118551 (2.39%) | 0.117202 (2.16%) |

The branch samples are too variable to support a performance conclusion. Compute and memory
differences are below 5%. The compared binaries were 45,461,824 and 45,463,920 bytes, with SHA-256
`c4432f39320abfc629783eba443d31883320a88c4d31e42cd369a34bbbc786be` and
`c793440ae6aa77747d29b06d558850051845cff8cef6d084417f67ff9d18a377`. This is an intermediate
working-tree comparison rather than the original migration baseline. The host was an Intel Core
Ultra X7 358H running x86-64 WSL2 with GCC 16.2.1, `-O3 -DNDEBUG`, and `-march=x86-64-v3`. ARM
hardware remains unavailable, so no ARM performance conclusion is made.

## Seminar snapshot: SimRV versus Spike

These measurements were collected on the same x86-64 WSL2 host (Intel Core Ultra X7 358H),
using the RV64 release build, `rv64gc_zicsr_zifencei_zicntr`, two warmups and five measured runs.
The synthetic guests used a fixed 100 million instruction stop. CoreMark ran to its own
completion point and retired 1,682,374,656 instructions per run (about 1.68 billion, not an
exact 2 billion). The native C++ benchmark runner compared SimRV and Spike with the same guest
and stop policy.

| Workload | Stop policy | SimRV median (CV) | Spike median (CV) | SimRV / Spike throughput |
| --- | --- | ---: | ---: | ---: |
| Compute | 100M instructions | 103.833 ms (1.55%) | 140.817 ms (8.16%) | 1.36x |
| Branch | 100M instructions | 159.708 ms (1.79%) | 147.881 ms (1.23%) | 0.93x |
| Memory | 100M instructions | 114.537 ms (2.46%) | 146.483 ms (2.75%) | 1.28x |
| CoreMark | workload completion; 1.682B retired | 2869.873 ms (0.74%) | 2702.929 ms (0.77%) | 0.94x |

Throughput ratios are derived from median elapsed time because instruction counts match within each
pair. The compute Spike samples have 8.16% CV, so treat that ratio as noisy. These are workload
results, not a claim of broad simulator superiority. The JSON records are
`/tmp/{compute,branch,memory,coremark}-final-spike.json`; they are local temporary files and are
not included in the repository.

The available Linux image manifest identifies Linux 7.3, OpenSBI 1.9, and Buildroot 2026.08.1
with an Alpine root filesystem and 512 MiB DRAM. A separate diskless boot workload was measured
with a 25 million instruction cap: the prior intermediate binaries measured 0.733139 s before and
0.718217 s after (1.67% and 1.66% CV). This is capped boot progress, not time to a completed OS
boot, and no directly comparable Spike result is available. ARM hardware and hardware performance
counters were unavailable; `perf` could not load its `libslang.so.2` dependency. A Callgrind run of
the branch guest capped at 1 million retired instructions (one run, no warmup) resolved simulator
symbols. `CPU::run_fast_baremetal_batch` accounted for 85.1% of instrumented instruction events;
the annotated profile attributed 11.1% to the direct `FastOp::Add` case and 12.8% to the
non-branch ALU loop. These are instrumentation counts, not timing shares. They identify the
functional execution kernel as the place to investigate, but do not establish which source-level
change would improve native throughput. A focused dispatcher experiment is recorded below. The
profile output is `/tmp/simrv-branch.callgrind` and is not part of the repository.

## Fast-op dispatcher simplification

Callgrind highlighted the bare-metal execution kernel, so I tested removing eight duplicated
`FastOp` checks (`Add`, `Addi`, `Slli`, `Srli`, `Addiw`, `Addw`, `Andi`, and `Mulw`) and routing
them through the existing operation `switch`. Both binaries used the same RV64 release settings,
ISA, guests and 100-million-instruction stop. Hyperfine pinned runs to x86 CPU 0, used three
warmups and interleaved 30 compute or 20 branch/memory samples. The host/compiler settings are the
same Intel Core Ultra X7 358H, x86-64 WSL2 and GCC 16.2.1 settings listed above.

| Workload | Before median (CV) | After median (CV) | Change |
| --- | ---: | ---: | ---: |
| Compute | 109.036 ms (2.08%) | 99.497 ms (1.58%) | 9.2% faster |
| Branch | 165.613 ms (0.75%) | 164.495 ms (1.37%) | 0.7% faster |
| Memory | 116.532 ms (2.01%) | 116.179 ms (1.98%) | 0.3% faster |

All three comparisons retired the same 100 million guest instructions in each sample. The before
and after RV64 executables were 45,564,848 and 45,557,624 bytes. Compute improved beyond the 5%
threshold with low variability; branch and memory remained within that threshold. The result
supports keeping the simpler dispatch on this x86 host. It is not an ARM measurement or a claim
that the change helps every host. Raw hyperfine records are in `/tmp/fastop-paired-*.json` and are
not checked in.
