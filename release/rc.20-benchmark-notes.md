# rc.20 benchmark notes

Captured 2026-11-17 on WSL2, Linux 6.18.58.1, x86_64, Intel Core Ultra X7 358H
(16 CPUs). The benchmark binary was built from the rc.20 worktree with the
`rv64-dev` preset. These results are local qualification evidence, not a
release performance guarantee.

## CoreMark against Spike

Workload: `/home/ren/workspace/lab/tests/coremark/coremark.riscv`, ISA
`rv64gc_zicsr_zifencei_zicntr`, fast SimRV mode, 2 warmups and 5 measured runs
for each simulator. SimRV retired 1,682,375,948 instructions to completion.

| Simulator | Median time | Median throughput | CV |
| --- | ---: | ---: | ---: |
| SimRV | 2,799.732 ms | 600.91 MIPS | 1.40% |
| Spike 1.1.1-dev | 2,893.423 ms | 581.45 MIPS | 1.41% |

SimRV was 1.033x faster in this run. Raw JSON: `/tmp/simrv-rc20-coremark-spike-final.json`.

## Linux boot and poweroff

Each run used the prepared RV64 Alpine GUI firmware, Linux 7.3 kernel, and root
filesystem, then logged in as root on `ttyS0` and ran `poweroff`. Each reached
SimRV's `guest poweroff` stop reason with exit status 0. ISA-specific temporary
DTBs advertised only extensions enabled in the selected profile; the firmware,
kernel, root filesystem, DRAM, simulator engine, and command path were otherwise
the same. Counts below are SimRV internal retired-instruction totals from the
execution JSON, which remain meaningful when Linux inhibits `minstret`.

| Guest ISA profile | Retired instructions | Host wall time |
| --- | ---: | ---: |
| RV64GC (`rva20s64`) | 2,201,621,549 | 59.1 s |
| RVA23S64 | 2,139,315,122 | 50.7 s |
| RVB23S64 | 2,137,868,308 | 49.1 s |

These are one-shot lifecycle counts, not timing comparisons: Linux boot work
depends on timers, entropy, and asynchronous first-boot services. Temporary
images, DTBs, transcripts, and full summaries are in
`/tmp/simrv-rc20-linux-bench.8mWIt4/`.

## RV32 Buildroot lifecycle check

The C++ builder also produced a Linux 7.3 / Buildroot 2026.08.1 RV32IMA image.
It reached the root login prompt, wrote and read back a marker under `/root` on
the ext4 root disk, ran `sync`, then accepted `poweroff`. SimRV exited with status
0 after OpenSBI reported `Power down`. The boot emitted BusyBox/libc SIGSEGV
reports from optional startup services, but networking, login, shell I/O, and
shutdown completed. The bootable image is retained under
`images/rc20-proof/rv32/buildroot-fpga/`. SimRV attaches block images through
a temporary copy-on-write overlay, so guest writes are visible for the boot and
are discarded afterward; the retained base image remains unchanged.

## Unstable follow-up run

A later repeat was excluded: a 10-run `rv64-release` sample measured 20.94% CV
for SimRV and 8.80% for Spike; a 3-run `rv64-dev` sample measured 7.60% CV for
SimRV. The host's 5- and 15-minute load averages were still elevated after the
long release builds (28.29 and 25.58 on a 16-CPU WSL guest). These runs do not
replace the stable five-run result above; repeat the comparison on an idle host
before using a new performance point.
