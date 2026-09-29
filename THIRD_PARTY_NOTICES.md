# Third-Party Notices

SimRV currently vendors no third-party source code and has no Git submodules.

Build-time tools (CMake, a C++23 compiler, Python when tests/tooling are enabled,
Git, and optionally Doxygen) are discovered from the host and are not distributed
as part of SimRV. Optional verification tools such as Spike and external RISC-V
test suites are likewise caller-supplied and are not redistributed.

Project-authored source and release artifacts are licensed under the MIT License;
see `LICENSE`.

RISC-V is a registered trademark of RISC-V International. SimRV is an independent
research simulator, is not affiliated with or endorsed by RISC-V International, and
does not claim RISC-V certification. RISC-V specifications and externally supplied
RISC-V tools remain subject to their own licenses and terms.

toml++ is an optional build-time dependency used for TOML configuration files. It is
header-only and is not a runtime dependency of SimRV packages; see the Fedora/Debian
package instructions in the user guide for installation and licensing details.
