# SimRV Cache Study Session - Workload Binaries

This directory contains assembly sources and build scripts for the **60-minute SimRV Cache Technologies Study Session**.

## Module Setup

Load the SimRV simulator module before running the exercises:

```bash
module load archlab/simrv
```

## Contents

- `01_spatial_locality.S` & `01_spatial_locality.bin`: Demonstrates cold compulsory misses vs 7 consecutive spatial hits within a 32-byte cache line.
- `02_temporal_locality.S` & `02_temporal_locality.bin`: Demonstrates repeated iteration over a small array with 100% temporal hit rate after warm-up.
- `03_conflict_thrashing.S` & `03_conflict_thrashing.bin`: Demonstrates 5 conflicting addresses mapped to Set 0 in a 4-way set-associative cache causing LRU thrashing.
- `build.sh`: Bash script to recompile `.S` assembly files into bare-metal RV32 `.elf` and `.bin` images using `riscv64-unknown-elf-gcc`.

## Building Binaries

```bash
./build.sh
```

## Running in Interactive TUI Mode (Visual Inspection)

Launch SimRV in cycle-accurate bare-metal mode:

```bash
# Exercise 1: Spatial Locality
simrv -m /mnt/archlab/study/cache/01_spatial_locality.bin -b -C

# Exercise 2: Temporal Locality
simrv -m /mnt/archlab/study/cache/02_temporal_locality.bin -b -C

# Exercise 3: Conflict Misses & Thrashing
simrv -m /mnt/archlab/study/cache/03_conflict_thrashing.bin -b -C
```

### TUI Cache Inspection Controls:
- Press `r` to cycle left pane views until reaching the **Cache** inspector.
- Press `s` or `Space` to step instruction-by-instruction.
- Observe **HIT** (green), **MISS** (coral), **REPLACED** (peach), **LRU** counters, and **Set/Way valid bits**.

## Running in Headless / CLI Mode

```bash
simrv -m /mnt/archlab/study/cache/01_spatial_locality.bin -b -c -C -s 50
simrv -m /mnt/archlab/study/cache/02_temporal_locality.bin -b -c -C -s 100
simrv -m /mnt/archlab/study/cache/03_conflict_thrashing.bin -b -c -C -s 100
```
