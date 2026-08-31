# RISC-V Compliance Scope

SimRV targets the `RV32GCBV` and `RV64GCBV` ISA strings. This is an implementation target, not a
claim of RISC-V architectural certification. A release is considered verified only for behavior
covered by the checked-in semantic tests, the configured `riscv-tests` suites, vector tests, and
Spike lockstep workloads run by the release gates.

## Architectural scope

| Area | Implemented scope | Verification evidence |
| --- | --- | --- |
| Base ISA | RV32I and RV64I | RV32/RV64 ISA gates and Spike lockstep |
| General-purpose extensions | M, A, F, D, C; see FP qualification note below | ISA gates, focused semantic tests, lockstep |
| Bit manipulation | Zba, Zbb, Zbc, Zbs (`B`) | Bitmanip ISA tests and lockstep |
| Vector | Implemented subset of RISC-V Vector 1.0 (`V`), configurable VLEN | Vector regression suite and lockstep |
| Privilege | M/S/U execution, traps, interrupts, and CSR access | Core semantic and Linux boot gates |
| Address translation | Sv32 for RV32; Sv39 and Sv48 for RV64 | MMU tests and Linux boot gates |

`G` includes I, M, A, F, D, Zicsr, and Zifencei. The `B` and `V` letters above are additional
extensions; they are not implied by `G`. MISA contains the single-letter extension bits only and
therefore cannot describe individual `Zb*` subsets.

The `--misa gc` profile selects G plus C and does not implicitly enable B or V. SimRV's historical
default target is now named explicitly as `gcbv`; XLEN-qualified forms such as `rv64gcbv` are also
accepted. Naming the profile explicitly does not override the vector qualification limits below.

For Vector 1.0, `vlenb` reflects the configured VLEN, `vstart` has enough writable bits for the
maximum VLMAX, prestart elements remain undisturbed, and successful vector instructions clear
`vstart`. Restartable vector memory faults retain the faulting element index. Operations that the
specification defines as non-restartable with nonzero `vstart` raise an illegal-instruction trap.
Dispatched vector instructions conservatively transition `mstatus.VS` to Dirty, including
instructions that update restart state before a fault.

## Known qualification gaps

- Scalar and vector floating-point use the host floating-point environment for RNE, RTZ, RDN, and
  RUP. The architectural RMM (nearest, ties to maximum magnitude) mode currently falls back to host
  RNE for arithmetic operations. Integer conversions implement RMM explicitly. Until an RMM
  arithmetic implementation and reference comparisons are added, F/D are implemented but not
  fully qualified for every rounding mode.
- The vector decoder and execution engine implement a substantial RVV 1.0 subset, not every
  instruction in the ratified V extension. Vector FP currently covers selected add, fused
  multiply-accumulate, and scalar move/merge operations. SEW=16 vector FP is rejected because the
  optional Zvfh extension is not advertised. The release must not describe this subset as complete
  V conformance until the missing instruction/legality matrix is closed.

Vector FP arithmetic observes `frm`, accrues host IEEE exceptions into `fflags`, canonicalizes NaN
results, and uses fused host operations for architecturally fused multiply-accumulate instructions.
Invalid dynamic rounding modes and scalar FP widths/extensions cause illegal-instruction traps.

## Privileged-state notes

- Trap entry and xRET maintain the architectural interrupt-enable stack and clear reservations.
  Reads of `mepc`/`sepc`, including implicit xRET reads, mask bit 1 whenever disabling C changes
  IALIGN from 16 to 32. RV64-hosted RV32 cause values translate the architectural interrupt bit
  without changing native RV32 cause storage.
- SXL and UXL accept only the implemented RV32/RV64 WARL encodings and are initialized consistently
  with the selected machine XLEN. When a profile omits S or U, its lower-mode status fields are
  read-only zero; MPP accepts only implemented privilege modes and xRET resets it to the least
  implemented mode. TVM, TW, TSR, MPRV, SUM, MXR, FS, and VS follow their privilege/extension
  presence rules. Obsolete draft-N user interrupt bits, delegation CSRs, and URET are not
  implemented and remain reserved/illegal. Supervisor CSR encodings are inaccessible when the
  selected MISA profile omits S, including from M-mode.
- `medeleg` exposes only exception causes that can originate below S-mode: causes 0-8, 12, 13,
  and 15. Supervisor/Machine ECALL, hypervisor, reserved, and double-trap causes are read-only zero.
- RV32 high-half counter CSRs are illegal in an RV64 personality. Unimplemented HPM counters and
  event selectors are legal hardwired-zero WARL registers, while unsupported environment-control
  (`*envcfg`) CSRs remain reserved rather than appearing as zero-valued implementations.
- `mstatus.SD` is synthesized from Dirty FS, VS, or XS state and moves to architectural bit 31 for
  an RV32 personality hosted by the RV64 build. Debug-only CSR encodings remain inaccessible to
  ordinary guest M-mode. With no implemented PMP entries, legal PMP CSRs are hardwired to zero;
  RV64's reserved odd-numbered `pmpcfg` encodings still trap as illegal instructions.
- The platform PLIC reserves interrupt source zero: its priority and enable bits read as zero and
  ignore writes. Equal-priority claims retain the standard lowest-source-ID tie break. `mip.MSIP`,
  MTIP, and MEIP are read-only signals from CLINT/PLIC; M-mode can write SSIP, STIP, and the
  software component of SEIP when S-mode exists. PLIC-driven and software-driven SEIP are tracked
  separately and read back as their architectural logical OR. Writable STIP is likewise retained
  independently of the direct-SBI timer signal, so timer reevaluation cannot erase a software-posted
  supervisor timer interrupt.
- Address translation always uses the active architectural XLEN, including Sv32 in an RV64-capable
  build and MPRV accesses using MPP's effective XLEN. Failed physical PTE reads produce the
  instruction/load/store access fault corresponding to the original access; malformed or
  permission-denied PTEs produce page faults. Instruction fetch likewise propagates physical bus
  errors and does not wrap a split 32-bit instruction across the end of RAM. Physical DRAM routing
  compares the complete address and access width, so high RV64 address bits and end-of-region
  accesses cannot alias through the RAM backing mask.

## Platform and SBI boundary

SimRV has two distinct supervisor-environment paths:

- With an FDT/OpenSBI image or multi-hart SMP configuration, supervisor ECALLs trap architecturally into the guest's M-mode
  firmware. OpenSBI owns the SBI version and extension set (including multi-hart HSM and IPI management) in this configuration.
- In single-hart direct-boot mode without external M-mode firmware, SimRV provides a built-in direct SBI environment. It advertises Base,
  TIME, RFENCE, IPI, and SRST.

The direct SBI environment follows the standard two-register return convention for SBI v0.2 and
later and intercepts only supervisor ECALLs; machine ECALLs remain ordinary architectural traps.
RFENCE and IPI hart-mask arguments are validated against the active hart set. A
successful SRST request is terminal: shutdown stops the machine and cold/warm reboot requests a
machine restart.

The device tree describes the platform independently of Linux so other supervisor software and
RTOSes can use the same UART, interrupt controller, timer, PCIe/VirtIO, and syscon power devices.

## Interpreting test results

A passing native build is not evidence of ISA compliance. Release evidence should record the XLEN,
MISA profile, VLEN, compiler, external test-suite revision, reference-model revision, and any skipped
tests. Missing external suites are reported as unavailable and must not be presented as passes.

Known deviations or untested optional behavior must be removed from the advertised profile or
listed here before release. The release manifest is the authoritative list of profiles shipped in a
particular release.
