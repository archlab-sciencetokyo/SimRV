# SimCore/RISC-V since 2018-07-05

&copy; [ArchLab. TokyoTech](https://www.arch.cs.titech.ac.jp)

[![C/C++ CI](https://github.com/archlab-tokyotech/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=main)](https://github.com/archlab-tokyotech/SimRV/actions/workflows/c-cpp.yml)

## How to run

1. Configure and build with CMake + Ninja
```
$ cmake --preset ninja-clang-release
$ cmake --build --preset ninja-clang-release
```

This generates `SimRV` under `build/ninja-clang-release/` using LLVM `clang++` and C++23.

2. Run an app image
```
$ ./build/ninja-clang-release/SimRV -m img/hello.bin
```

3. Run Linux image
```
$ ./build/ninja-clang-release/SimRV -m img/bbl.bin -d img/root.bin -c img/devicetree.dtb
```
Once login prompt appears, type `root` to login and enjoy linux.
Please type Control+`q` to quit the simulation.

4. Read help message of SimCore/RISC-V (SimRV) by
```
$ ./build/ninja-clang-release/SimRV
```

## Regression Baseline (Phase 0)

Run the baseline regression harness:

```
$ cmake --build --preset ninja-clang-release --target regress
```

This always checks:

* build success
* help output path
* unknown-option CLI error path

Optional smoke tests are enabled by environment variables:

```
# app smoke
SIMRV_APP_IMG=img/hello.bin cmake --build --preset ninja-clang-release --target regress

# linux smoke
SIMRV_LINUX_MEM_IMG=img/bbl.bin \
SIMRV_LINUX_DISK_IMG=img/root.bin \
SIMRV_LINUX_DTB=img/devicetree.dtb \
cmake --build --preset ninja-clang-release --target regress
```

Logs are written to `regression_logs/` by default.

## riscv-isa-tests (experimental)

You can run a small subset of riscv-isa-tests with:

```
$ cmake --build --preset ninja-clang-release --target isa-tests
```

For the curated Phase 1 RV32IM smoke gate:

```
$ cmake --build --preset ninja-clang-release --target isa-smoke
```

To run both baseline checks and ISA smoke together:

```
$ cmake --build --preset ninja-clang-release --target phase1-gate
```

## Documentation

Architecture and ADR docs live under `docs/`:

* `docs/ARCHITECTURE.md`
* `docs/adr/`

API docs can be generated with Doxygen (when installed):

```
$ cmake --build --preset ninja-clang-release --target docs
```

Generated HTML output is written to `docs/api/html/`.

## GitHub Flow (dev branch)

Simple team flow:

1. `main` stays stable and release-ready.
2. `dev` is the integration branch for ongoing work.
3. Create short-lived feature branches from `dev`:
    - `feature/<topic>`
    - `fix/<topic>`
4. Open pull requests into `dev` for review and CI.
5. Periodically open a pull request from `dev` to `main` for release cuts.

Recommended local commands:

```
# one-time
git fetch origin
git checkout dev

# start new work
git checkout -b feature/my-change dev

# keep branch updated
git fetch origin
git rebase origin/dev

# after merge, clean up
git checkout dev
git pull
git branch -d feature/my-change
```

## Project Layout

* `src/`: simulator implementation (`*.cpp`)
* `include/simrv/`: simulator headers (`*.hpp`)
* `scripts/`: regression and ISA test runners
* `docs/`: architecture notes and ADRs

## Refactor Progress

Completed (mechanical, behavior-preserving):

* Slice 1: Decode wrapper (`DecodeUnit`) integrated in `Machine` path.
* Slice 2: Execute wrapper (`ExecuteUnit`) integrated in `Machine` path.

Remaining planned slices:

* Pipeline context extraction (`r_*` grouping)
* Memory access extraction

Default `RISCV_TESTS_DIR` is `../../tests/riscv-tests` (relative to SimRV root).
You can still override it:

```
$ RISCV_TESTS_DIR=/path/to/riscv-tests cmake --build --preset ninja-clang-release --target isa-tests
```

Current defaults run these tests if present in `RISCV_TESTS_DIR/isa`:

* rv32ui-p-add
* rv32ui-p-addi
* rv32ui-p-and
* rv32ui-p-beq
* rv32ui-p-bne
* rv32ui-p-lw
* rv32ui-p-sw

Useful variables:

```
# Override objcopy tool
RISCV_OBJCOPY=riscv64-unknown-elf-objcopy

# Custom comma-separated test list
SIMRV_ISA_LIST=rv32ui-p-add,rv32ui-p-beq

# Stop controls
SIMRV_ISA_TIMEOUT=20
SIMRV_ISA_END=2000000

# tohost RAM address watched by SimRV in ISA-test mode
SIMRV_ISA_TOHOST=0x80001000
```

Notes:

* SimRV uses `-T` mode to watch RAM `tohost` updates and print pass/fail markers.
* This flow is intended for RV32 ISA tests during bring-up and may require tuning of `SIMRV_ISA_TOHOST` depending on your test build/link setup.
* If `riscv-tests` top-level build fails with missing `-lm` or `-lgcc`, run the SimRV ISA target anyway. The runner builds ISA tests from `riscv-tests/isa` directly and bypasses benchmarks.
* `rv32uc-p-rvc` currently does not emit a tohost pass/fail marker under current SimRV behavior and is not part of `isa-smoke` yet.

## Recommended compilers to be used

* clang++ 17+ (default via CMake preset)
* gcc/g++ 12+ (supported via a custom CMake configure/build preset)

## TODO

* memory mapped I/O:

    ```c
    0x40008000 tohost

    0x40009000 arg0
    0x40009004 arg1
    0x40009008 arg2
    0x4000900c arg3
    0x40009010 arg4

    0x4000a000 QueueState pointer // for console (two entries)
    0x4000b000 QueueState pointer // for disk (four entries)

    0x90000000 ~    Disk
    ```

* support the compressed instructions ?

## History

| Date | Version | Changes |
| ---------- | ------ | -------------------- |
| 2020-07-11 | v1.3.7 |  Remove some system registers from the trace and update misa in RTOS mode |
| 2020-07-09 | v1.3.6 |  Add the register value `mtimecmp` to the trace output in RTOS mode |
| 2020-07-08 | v1.3.5 |  Change start pc to 0 and not include TLB value in trace file in RTOS mode |
| 2020-07-07 | v1.3.4 |  add option `-x`, output instruction mix to instmix.txt |
| 2020-07-07 | v1.3.3 |  add option `-r`, RTOS  mode |
| 2020-06-04 | v1.3.2 |  Fixed two parameters of  frequency in embedded device tree binary to 100MHz |
| 2020-05-30 | v1.3.1 |  add option `-l`, enable timer after N cycles Linux boots |
| 2020-05-27 | v1.3.0 |  Added the update of p. reserved to the function generating init_reg.txt |
| 2020-03-04 | v1.2.9 |  change the format of  initreg.txt |
| 2019-XX-XX | v1.2.8 |  add option `-w`,  generate trace file bpred.txt for branch prediction |
| 2019-11-11 | v1.2.7 |  device_tree file can be specified |
| 2019-11-11 | v1.2.6 |  uint32_t misa  =  0x00141105; |
| 2019-10-05 | v1.2.5 |  |
| 2019-09-19 | v1.2.4 |  fina names by -i option  were changed. |
| 2019-09-19 | v1.2.3 |  modified void Machine::LD_() |
| 2019-09-09 | v1.2.2 |  added a parameter NUM for -q option |
| 2019-09-09 | v1.2.1 |  -q option was added to  generate tracepc.txt |
| 2019-09-09 | v1.2.0 |  -b option generates 9MB  + 4KB + 16MB (26,218,496 byte)inits.bin file |
| 2019-09-09 | v1.1.9 |  modifiled -b option to  generate inits.bin |
| 2019-09-03 | v1.1.8 |  added `mc_code` for  micro-controller code, dsk_ld/st by 4byte |
| 2019-09-03 | v1.1.7 |  modified INI and timer &  keyboard detection logic! |
| 2019-09-03 | v1.1.6 |  renamed some variables |
| 2019-09-01 | v1.1.5 |  load_file is eliminated |
| 2019-09-01 | v1.1.4 |  devicetree.bin is  embedded. simrv.dtb is not used |
| 2019-09-01 | v1.1.3 |  implement mm.s_use_uc  and '-s' option to use micro-controller |
| 2019-09-01 | v1.1.2 |  rename: IOCON ->  Microcn, m -> mm, c -> cc |
| 2019-08-31 | v1.1.1 |  eliminate some std  (std::string s_fnXXX -> char *), 3424 lines |
| 2019-08-31 | v1.1.0 |  refactoring |
| 2019-08-30 | v1.0.9 |  debug the keyborad  errro, 0 -> 2 |
| 2019-08-16 | v1.0.3 |  add option `-b` as the  mode generating initmem.bin file |
| 2019-07-25 | v1.0.2 |  cpu->reserved is not  update if PAGEFAULT happend, Local mem size 32KB |
| 2019-07-22 | v0.9.9 |  QueueState (uint16_t  last_avail_idx) -> uint32_t |
| 2019-07-15 | v0.9.4 |  sector -> mdskresize |
| 2019-07-04 | v0.9.1 |  consume_descriptor() ->  update_descriptor() |
| 2019-07-03 | v0.8.8 |  memory.h and memory.cc  were removed. 2,833 lines |
| 2019-06-30 | v0.8.4 |  2,872 lines |
| 2019-06-30 | v0.8.3 |  -c option was implemented |
| 2019-06-30 | v0.8.2 |  -z option is -g option  now |
| 2019-06-30 | v0.8.1 |  -z option to show debug  info for VirtIO was implemented |
| 2019-06-28 | v0.7.8 |  mmu.c mmu.h are removed,  3,046 lines |
| 2019-06-28 | v0.7.6 |  supported initreg.txt  generation |
| 2019-06-25 | v0.7.1 |  3010 lines |
| 2019-06-25 | v0.6.8 |  IF_ -> IFA, IFB, and IFC |
| 2019-06-25 | v0.6.7 |  MMU::target_read_inst16  -> MMU::insn_fetch |
| 2019-06-20 | v0.6.3 |  3574 lines, refactoring |
| 2019-06-20 | v0.6.2 |  trace.txt format has  changed |
| 2019-06-20 | v0.6.1 |  |
| 2019-06-20 | v0.6.0 |  3593 lines, application  mode was tested, INI(), FIN() were added
| 2019-06-20 | v0.5.9 |  3582 lines, CPUState ->  CPU, debug.* are removed |
| 2019-06-19 | v0.5.8 |  some global variables  were removed |
| 2019-06-18 | v0.5.7 |  3785 lines, today's  version |
| 2019-06-18 | v0.5.5 |  refactoring (main.cc) |
| 2019-06-18 | v0.5.4 |  -i option was implemented |
| 2019-06-17 | v0.5.3 |  today's version |
| 2019-06-16 | v0.5.0 |  |

## Naming convention

* CB_ : combinational logic circuit
* w_  : wire
* r_  : register
* s_  : system configuration of class Machine
* e_  : evaluation results   of class Machine

## Verify

/home/share/FPGA/riscv/data_vefify/

* initmem00m.txt

## Schedule

1. generate a memory image file
2. generate a architecture state image file
3. generate a log file
4. verify the verilog code with SimRV
