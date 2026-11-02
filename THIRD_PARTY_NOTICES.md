# Third-Party Notices

SimRV currently vendors no third-party source code and has no Git submodules.

Build-time tools (CMake, a C++23 compiler, Python when tests/tooling are enabled,
and Git) are discovered from the host and are not distributed as part of SimRV.
Optional verification tools such as Spike and external RISC-V
test suites are likewise caller-supplied and are not redistributed.

Project-authored source and release artifacts are licensed under the MIT License;
see `LICENSE`.

## Architectural Specification Attribution

Normative instruction descriptions, CSR definitions, operational formulas, and chapter/section
references in `simrv::explain` (`include/simrv/explain/`, `src/explain/`) are adapted from and
cite the official ratified RISC-V International specifications (licensed under the Creative Commons
Attribution 4.0 International License, CC-BY-4.0):

- *The RISC-V Instruction Set Manual, Volume I: Unprivileged Architecture*, Document Version
  `20260120` (January 2026), Editors Andrew Waterman and Krste Asanović, RISC-V International.
- *The RISC-V Instruction Set Manual, Volume II: Privileged Architecture*, Document Version
  `20260120` (January 2026), Editors Andrew Waterman, Krste Asanović, and John Hauser, RISC-V
  International.
