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
