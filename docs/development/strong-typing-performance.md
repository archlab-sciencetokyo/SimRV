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

The branch result is too noisy to establish a speedup. The MMIO loop retained the same bus and
cache counts. The initial baseline executable is no longer available, so an identical-binary
comparison against the final tree remains outstanding. ARM hardware and usable host performance
counters were unavailable. The raw local reports and workload sources are in the ignored
`benchmark_logs/strong-typing-2026-11-05/` directory when present; they are not release artifacts.
