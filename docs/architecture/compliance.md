# RISC-V Compliance Scope

SimRV targets the ratified RISC-V **RVA23 Profile** (`rva23s64` and `rva23u64`) as its primary 64-bit architectural profile. RVA23 is the project's primary ISA target; RV32 and narrower compatibility presets are also supported for dual-width validation and workloads. This is an implementation and verification target, not a claim of formal RISC-V International architectural certification. A release is considered verified only for behavior covered by the checked-in semantic tests, the Architectural Compatibility Test (ACT) test suite, the configured `riscv-tests` suites, vector tests, and Spike lockstep workloads run by the release gates.

**The current implementation is not yet a complete RVA23S64 implementation.** Selecting the
`rva23s64` profile names the intended target; it does not imply that every mandatory profile
extension is implemented or advertised. In particular, the augmented hypervisor extension (Sha),
pointer masking, and several scalar and vector extensions are not yet complete. The matrix below
records implemented areas and the qualification gaps; full
RVA23 remains the goal.

CPU and SoC presets that do not implement Zic64b are outside Zic64b-specific checks; their
configuration files remain unchanged. The RVA23 target uses 64-byte cache blocks
throughout the coherent cache path and reports 64-byte CMO blocks. Runtime support for each
preset's configured cache-line geometry remains a separate platform requirement.

## Architectural scope

| Area | Implemented scope | Verification evidence |
| --- | --- | --- |
| Standard profiles | RVA23S64 is the intended target; mandatory-extension coverage remains incomplete | Focused tests for selected RVA23 features; these are not a full profile-conformance result |
| Base ISA | RV32I and RV64I | RV32/RV64 ISA gates, ACT suite, and Spike lockstep |
| General-purpose extensions | M, A, F, D, C; see FP qualification note below | ISA gates, focused semantic tests, ACT suite, lockstep |
| Bit manipulation | Zba, Zbb, Zbc, Zbs (`Zb*`) | Bitmanip ISA tests, ACT suite, and lockstep |
| Conditional operations | Zicond (`czero.eqz`, `czero.nez`) | Core semantic, RVA23 profile gates, and ACT suite |
| Compressed may-be-operations | Zcmop `C.MOP.n` encodings decode as architectural no-ops | RVA23 profile unit tests |
| May-be-operations | Zimop MOP.R and MOP.RR encodings write zero to `rd` | RVA23 profile unit tests |
| Additional compressed integer | Zcb's byte/halfword loads and stores, multiply, and bit-manipulation instructions | RVA23 compressed decode tests |
| Non-temporal locality hints | Zihintntl NTL.P1/PALL/S1/ALL and C.NTL variants decode as ignorable HINTs | RVA23 hint-encoding tests |
| Cache management | CMO (`cbo.clean`, `cbo.flush`, `cbo.inval`, `cbo.zero`, `prefetch.*`) | Core semantic, RVA23 profile gates, and ACT suite |
| Cache-block size | Zic64b: 64-byte naturally aligned blocks across L1/L2/L3, coherence, and CMO; FDT advertises Zic64b and all CMO block sizes | Cache geometry, CMO, and FDT regression tests |
| Misaligned main-memory accesses | Zicclsm loads and stores, including accesses spanning cache-block and page boundaries; FDT advertises Zicclsm | RVA23 main-memory alignment and FDT tests |
| Reservation-set size | Za64rs uses naturally aligned 64-byte reservation sets; FDT advertises Za64rs | RVA23 reservation-set and FDT tests |
| Main-memory AMOs | Ziccamoa: all Zaamo arithmetic and logical AMOs on main memory; FDT advertises Ziccamoa | RVA23 AMO integration tests across word and doubleword widths |
| Minimal half precision | Zfhmin transfers, moves, and binary16 conversions; Zvfhmin widening/narrowing vector conversions at SEW=16; FDT advertises both | RVA23 scalar conversion, vector conversion, and FDT tests |
| Additional scalar FP | Zfa RV64 instructions: FLI.S/D, FMINM/FMAXM, FLEQ/FLTQ, FROUND/FROUNDNX, and FCVTMOD.W.D; FDT advertises Zfa | RVA23 instruction decode, semantics, flags, and FDT tests |
| Wait-on-reservation-set | Zawrs WRS.NTO/WRS.STO encodings retire immediately (SimRV does not model low-power waits); FDT advertises Zawrs | RVA23 decode, reserved-encoding, retirement, and FDT tests |
| TLB invalidation | Svinval (`sinval.vma`, `sfence.w.inval`, `sfence.inval.ir`) | Core semantic, RVA23 profile gates, and ACT suite |
| Vector | Implemented subset of RISC-V Vector 1.0 (`V`), configurable VLEN | Vector regression suite and lockstep |
| Privilege | M/S/U execution, traps, interrupts, and CSR access (Privileged ISA 1.13) | Core semantic, ACT suite, and Linux boot gates |
| Address translation | Sv32 for RV32; Sv39 for RV64 with Page Directory Cache | MMU tests and Linux boot gates |

RVA23S64 includes every RVA23U64 mandatory extension, plus Zifencei and the privileged
requirements defined by RVA23S64. Those include Svnapot, Sstc, Sscofpmf, Ssnpm, Ssu64xl, and Sha
(H plus the required state-enable, counter-enable, guest trap-value, and guest translation
features). SimRV does not currently implement all of these requirements. The existing profile test
name and default-profile selection are not evidence that this list is complete. See the [RVA23
profile specification](https://github.com/riscv/riscv-profiles/blob/main/src/rva23-profile.adoc)
for the normative mandatory-extension list.

`G` includes I, M, A, F, D, Zicsr, and Zifencei. The `Zb` and `V` extensions above are additional
extensions; they are not implied by `G`. MISA contains the single-letter extension bits only and
therefore cannot describe individual `Zb*` subsets.

The `--isa gc` preset selects G plus C and does not implicitly enable B or V. SimRV's canonical
extension preset is `gcv` (or `gc` for profiles without vector); RVA23 profile selection is the
primary way to request the project's target architecture. XLEN-qualified forms
such as `rv64gcv` and `rv64gc` are standard. Ratified Bitmanip sub-extensions (`zba`, `zbb`, `zbc`, `zbs`)
are appended canonically (e.g. `rv64gcv_zba_zbb_zbc_zbs` or `rv64gc_zba_zbb_zbc_zbs`). Historical draft shorthands such as `gcbv` and `rv64gcbv`
are rejected with diagnostic guidance. Naming the preset explicitly does not override the vector qualification limits below.

For Vector 1.0, `vlenb` reflects the configured VLEN, `vstart` has enough writable bits for the
maximum VLMAX, prestart elements remain undisturbed, and successful vector instructions clear
`vstart`. Restartable vector memory faults retain the faulting element index. Operations that the
specification defines as non-restartable with nonzero `vstart` raise an illegal-instruction trap.
Dispatched vector instructions conservatively transition `mstatus.VS` to Dirty, including
instructions that update restart state before a fault.

## Architectural Compatibility Testing (ACT)

SimRV verifies its default 64-bit profile (`rva23s64`) against the RISC-V Architectural Compatibility Test (ACT) framework:

- **Signature dumping**: Bare-metal execution supports `--signature <file>` or `+signature=<file>`, extracting memory between `begin_signature` (or `rvtest_sig_begin`) and `end_signature` (or `rvtest_sig_end`) to format-compliant hex output files.
- **RVA23 Mandatory Extensions**: Automated regression suites verify Zicond, CMO, Svinval, Bitmanip (`zba`, `zbb`, `zbs`), and profile CSR configurations against golden architectural signatures.
- **Continuous Integration**: The `rva23-act` suite is registered as a required verification gate in release evidence generation and verified across dual-width CI runs.

## Known qualification gaps

- Full RVA23S64 is not yet qualified. The following mandatory requirements remain incomplete or
  need profile-level qualification:
  - Unprivileged extensions: Zkt, Zvbb, Zvkt, and Supm.
  - Memory requirements: the Ziccif and Ziccrse main-memory PMA guarantees.
  - Supervisor profile requirements: Ssnpm pointer masking, Sscofpmf, Ssu64xl, and the augmented
    hypervisor architecture Sha, including H, Ssstateen, and its required guest state and
    translation behavior.


  This list is a starting qualification checklist, not a claim that every other mandatory item
  has passed conformance testing. See the [normative RVA23 profile specification](https://github.com/riscv/riscv-profiles/blob/main/src/rva23-profile.adoc)
  and its mandatory requirements when closing each item. FDT advertisements describe SimRV's
  present capabilities and must not be read as a declaration of complete RVA23 conformance.

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
  with the selected machine XLEN. Without S-mode, supervisor status fields are read-only zero;
  without U-mode, MPRV and UXL are read-only zero. MPP accepts only implemented privilege modes and
  xRET resets it to U when U-mode
  is implemented, otherwise M. SRET clears SPP to zero. TVM, TW, TSR, MPRV, SUM, MXR, FS, and VS
  follow their privilege/extension
  presence rules. Obsolete draft-N user interrupt bits, delegation CSRs, and URET are not
  implemented and remain reserved/illegal. Supervisor CSR encodings are inaccessible when the
  selected ISA preset omits S, including from M-mode.
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

The direct SBI environment follows the standard two-register return convention for ratified SBI
(v2.0 / v3.0, returning specification version 2.0) and intercepts only supervisor ECALLs; machine
ECALLs remain ordinary architectural traps. RFENCE and IPI hart-mask arguments are validated against
the active hart set. A successful SRST request is terminal: shutdown stops the machine and cold/warm
reboot requests a machine restart.

The device tree describes the platform independently of Linux so other supervisor software and
RTOSes can use the same UART, interrupt controller, timer, PCIe/VirtIO, and syscon power devices.

## Interpreting test results

A passing native build is not evidence of ISA compliance. Release evidence should record the XLEN,
ISA preset, VLEN, compiler, external test-suite revision, reference-model revision, and any skipped
tests. Missing external suites are reported as unavailable and must not be presented as passes.

Known deviations or untested optional behavior must be removed from the advertised profile or
listed here before release. The release manifest is the authoritative list of profiles shipped in a
particular release.
