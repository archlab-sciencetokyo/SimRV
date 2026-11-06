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

The branch result is too noisy to establish a speedup. The MMIO loop retained the same bus and
cache counts. The initial baseline executable is no longer available, so an identical-binary
comparison against the final tree remains outstanding. ARM hardware and usable host performance
counters were unavailable. The raw local reports and workload sources are in the ignored
`benchmark_logs/strong-typing-2026-11-05/` directory when present; they are not release artifacts.
