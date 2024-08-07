# SimCore/RISC-V since 2018-07-05

&copy; [ArchLab. TokyoTech](https://www.arch.cs.titech.ac.jp)

[![C/C++ CI](https://github.com/archlab-tokyotech/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=main)](https://github.com/archlab-tokyotech/SimRV/actions/workflows/c-cpp.yml)

## Overview

SimCore/RISC-V is a simulator for the RISC-V instruction set architecture (ISA). It has been developed by the ArchLab at Tokyo Tech. The simulator is designed to be efficient and accurate, making it ideal for research and educational purposes.

The simulator supports RISC-V instructions (RV32IMACZicsr_Zifencei) and includes several features for advanced simulation. It provides options for generating trace files, specifying device tree files, and simulating different modes of operation, including RTOS mode.

The project is written in C++ and can be built and run on any system with a C++ compiler and make. It also supports Doxygen for generating documentation from the annotated source code.

## Requirements

To run and build this project, you will need:

- `make`: A build automation tool used to manage and build this project.
- `g++`: A popular C++ compiler. You can install it on Ubuntu with `sudo apt install g++` or `sudo apt install build-essential`.
- `doxygen`: A tool for generating documentation from annotated C++ sources. Install it on Ubuntu with `sudo apt install doxygen`.

Please ensure these are installed and properly configured in your system's PATH before proceeding with the build process.

## How to Run

1. Set up your environment: Ensure you have the necessary dependencies installed.
2. Clone the repository
3. Navigate into the repository: `cd SimRV`
4. Compile the program: `make`
5. Run the program: `./simrv`

## Documentation

The source code is annotated with Doxygen comments. To generate the documentation, run `make docs` in the project directory. The documentation will be generated in the `docs` directory.

The generated documentation can be viewed by opening `docs/html/index.html` in a web browser.

To build the documentation the submodule doxygen-awesome-css is required. If you have already cloned the repository you can run `git submodule update --init --recursive`.
Or to clone the repository with the submodule run `git clone --recurse-submodules`. 

## History

| Date | Version | Changes |
| ---------- | ------ | -------------------- |
| 2020-07-11 | v1.3.7 | Removed some system registers from the trace and updated misa in RTOS mode |
| 2020-07-09 | v1.3.6 | Added the register value `mtimecmp` to the trace output in RTOS mode |
| 2020-07-08 | v1.3.5 | Changed start pc to 0 and excluded TLB value in trace file in RTOS mode |
| 2020-07-07 | v1.3.4 | Added option `-x` to output instruction mix to instmix.txt |
| 2020-07-07 | v1.3.3 | Added option `-r` for RTOS mode |
| 2020-06-04 | v1.3.2 | Fixed two parameters of frequency in embedded device tree binary to 100MHz |
| 2020-05-30 | v1.3.1 | Added option `-l` to enable timer after N cycles Linux boots |
| 2020-05-27 | v1.3.0 | Added the update of p. reserved to the function generating init_reg.txt |
| 2020-03-04 | v1.2.9 | Changed the format of initreg.txt |
| 2019-XX-XX | v1.2.8 | Added option `-w` to generate trace file bpred.txt for branch prediction |
| 2019-11-11 | v1.2.7 | Made device_tree file specifiable |
| 2019-11-11 | v1.2.6 | Set uint32_t misa to 0x00141105 |
| 2019-10-05 | v1.2.5 | No changes specified |
| 2019-09-19 | v1.2.4 | Changed file names by `-i` option |
| 2019-09-19 | v1.2.3 | Modified void Machine::LD_() |
| 2019-09-09 | v1.2.2 | Added a parameter NUM for `-q` option |
| 2019-09-09 | v1.2.1 | Added `-q` option to generate tracepc.txt |
| 2019-09-09 | v1.2.0 | `-b` option generates 9MB + 4KB + 16MB (26,218,496 byte) inits.bin file |
| 2019-09-09 | v1.1.9 | Modified `-b` option to generate inits.bin |
| 2019-09-03 | v1.1.8 | Added `mc_code` for micro-controller code, dsk_ld/st by 4byte |
| 2019-09-03 | v1.1.7 | Modified INI and timer & keyboard detection logic |
| 2019-09-03 | v1.1.6 | Renamed some variables |
| 2019-09-01 | v1.1.5 | Eliminated load_file |
| 2019-09-01 | v1.1.4 | Embedded devicetree.bin. simrv.dtb is not used |
| 2019-09-01 | v1.1.3 | Implemented mm.s_use_uc and '-s' option to use micro-controller |
| 2019-09-01 | v1.1.2 | Renamed: IOCON -> Microcn, m -> mm, c -> cc |
| 2019-08-31 | v1.1.1 | Eliminated some std (std::string s_fnXXX -> char *), 3424 lines |
| 2019-08-31 | v1.1.0 | Refactoring |
| 2019-08-30 | v1.0.9 | Debugged the keyboard error, 0 -> 2 |
| 2019-08-16 | v1.0.3 | Added option `-b` as the mode generating initmem.bin file |
| 2019-07-25 | v1.0.2 | Updated cpu->reserved only if PAGEFAULT didn't occur, set local memory size to 32KB |
| 2019-07-22 | v0.9.9 | Changed QueueState's last_avail_idx from uint16_t to uint32_t |
| 2019-07-15 | v0.9.4 | Renamed `sector` to `mdskresize` |
| 2019-07-04 | v0.9.1 | Renamed `consume_descriptor()` to `update_descriptor()` |
| 2019-07-03 | v0.8.8 | Removed memory.h and memory.cc, reducing the codebase by 2,833 lines |
| 2019-06-30 | v0.8.4 | Codebase reduced to 2,872 lines |
| 2019-06-30 | v0.8.3 | Implemented `-c` option |
| 2019-06-30 | v0.8.2 | Replaced `-z` option with `-g` option |
| 2019-06-30 | v0.8.1 | Implemented `-z` option to show debug info for VirtIO |
| 2019-06-28 | v0.7.8 | Removed mmu.c and mmu.h, reducing the codebase by 3,046 lines |
| 2019-06-28 | v0.7.6 | Added support for generating initreg.txt |
| 2019-06-25 | v0.7.1 | Codebase reduced to 3,010 lines |
| 2019-06-25 | v0.6.8 | Renamed `IF_` to `IFA`, `IFB`, and `IFC` |
| 2019-06-25 | v0.6.7 | Renamed `MMU::target_read_inst16` to `MMU::insn_fetch` |
| 2019-06-20 | v0.6.3 | Codebase reduced to 3,574 lines, refactoring performed |
| 2019-06-20 | v0.6.2 | Changed the format of trace.txt |
| 2019-06-20 | v0.6.1 | No changes specified |
| 2019-06-20 | v0.6.0 | Codebase reduced to 3,593 lines, tested application mode, added `INI()`, `FIN()` |
| 2019-06-20 | v0.5.9 | Codebase reduced to 3,582 lines, renamed `CPUState` to `CPU`, removed debug.* |
| 2019-06-19 | v0.5.8 | Removed some global variables |
| 2019-06-18 | v0.5.7 | Codebase reduced to 3,785 lines, updated to the version of the day |
| 2019-06-18 | v0.5.5 | Refactored main.cc |
| 2019-06-18 | v0.5.4 | Implemented `-i` option |
| 2019-06-17 | v0.5.3 | Updated to the version of the day |
| 2019-06-16 | v0.5.0 | No changes specified |
