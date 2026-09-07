# Third-Party Notices

SimRV currently vendors no third-party source code and has no Git submodules.

Build-time tools (CMake, a C++23 compiler, Python when tests/tooling are enabled,
Git, and optionally Doxygen) are discovered from the host and are not distributed
as part of SimRV. Optional verification tools such as Spike and external RISC-V
test suites are likewise caller-supplied and are not redistributed.

Project-authored source and release artifacts are licensed under the MIT License;
see `LICENSE`.
