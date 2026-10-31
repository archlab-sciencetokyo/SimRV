# RISC-V Compliance Scope

SimRV targets the ratified RISC-V **RVA23 Profile** (`rva23s64` and `rva23u64`) as its primary 64-bit architectural profile. RVA23 is the project's primary ISA target; RV32 and narrower compatibility presets are also supported for dual-width validation and workloads. This is an implementation and verification target, not a claim of formal RISC-V International architectural certification. A release is considered verified only for behavior covered by the checked-in semantic tests, the Architectural Compatibility Test (ACT) test suite, the configured `riscv-tests` suites, vector tests, and Spike lockstep workloads run by the release gates.

**RVA23S64 is the implementation target.** Profile selection alone is not proof of conformance;
the implementation claims below are tied to instruction suites, semantic tests, ACT results, and
the profile requirements. The latest mandatory-vector and Sha evidence is recorded below, along
with the unresolved ACT/specification expectation conflicts.

CPU and SoC presets that do not implement Zic64b are outside Zic64b-specific checks; their
configuration files remain unchanged. The RVA23 target uses 64-byte cache blocks
throughout the coherent cache path and reports 64-byte CMO blocks. Runtime support for each
preset's configured cache-line geometry remains a separate platform requirement.

## Architectural scope

| Area | Implemented scope | Verification evidence |
| --- | --- | --- |
| Standard profiles | RVA23S64 target; profile support is tied to the qualification evidence below | Profile regression, ACT4 RVA23S64, vector suite at minimum VLEN, and extension-specific tests |
| Base ISA | RV32I and RV64I | RV32/RV64 ISA gates, ACT suite, and Spike lockstep |
| General-purpose extensions | M, A, F, D, C; see FP qualification note below | ISA gates, focused semantic tests, ACT suite, lockstep |
| Instruction-fetch fence | Zifencei `FENCE.I` orders prior explicit memory accesses before later instruction fetches on the same hart; remote harts require their own synchronization | RVA23 local instruction-state flush test; SMP instruction-fetch coherence tests |
| Bit manipulation | Zba, Zbb, Zbc, Zbs (`Zb*`) | Bitmanip ISA tests, ACT suite, and lockstep |
| Conditional operations | Zicond (`czero.eqz`, `czero.nez`) | Core semantic, RVA23 profile gates, and ACT suite |
| Compressed may-be-operations | Zcmop `C.MOP.n` encodings decode as architectural no-ops | RVA23 profile unit tests |
| May-be-operations | Zimop MOP.R and MOP.RR encodings write zero to `rd` | RVA23 profile unit tests |
| Additional compressed integer | Zcb's byte/halfword loads and stores, multiply, and bit-manipulation instructions | RVA23 compressed decode tests |
| Non-temporal locality hints | Zihintntl NTL.P1/PALL/S1/ALL and C.NTL variants decode as ignorable HINTs | RVA23 hint-encoding tests |
| Pause hint | Zihintpause `PAUSE` (`FENCE W,0`) decodes as a valid fence with no successor set | RVA23 pause-encoding test |
| Cache management | CMO (`cbo.clean`, `cbo.flush`, `cbo.inval`, `cbo.zero`, `prefetch.*`) | Core semantic, RVA23 profile gates, and ACT suite |
| Cache-block size | Zic64b: 64-byte naturally aligned blocks across L1/L2/L3, coherence, and CMO; FDT advertises Zic64b and all CMO block sizes | Cache geometry, CMO, and FDT regression tests |
| Misaligned main-memory accesses | Zicclsm loads and stores, including accesses spanning cache-block and page boundaries; FDT advertises Zicclsm | RVA23 main-memory alignment and FDT tests |
| Reservation-set size | Za64rs uses naturally aligned 64-byte reservation sets; FDT advertises Za64rs | RVA23 reservation-set and FDT tests |
| LR/SC eventual progress | With competing constrained attempts and no unconditional writer, one SC succeeds and invalidates the other reservation; FDT advertises Ziccrse | RVA23 multi-hart LR/SC progress test and existing atomic-coherence tests |
| Atomic instruction fetch | Ziccif aligned instruction fetches up to 32 bits are synchronized against instruction-mode SMP writes; cycle-mode fetch uses coherent I-cache line fills; FDT advertises Ziccif | RVA23 concurrent fetch/store test and cycle-mode cache-coherence tests |
| Main-memory AMOs | Ziccamoa: all Zaamo arithmetic and logical AMOs on main memory; FDT advertises Ziccamoa | RVA23 AMO integration tests across word and doubleword widths |
| Minimal half precision | Zfhmin transfers, moves, and binary16 conversions; Zvfhmin widening/narrowing vector conversions at SEW=16; FDT advertises both | RVA23 scalar conversion, vector conversion, and FDT tests |
| Additional scalar FP | Zfa RV64 instructions: FLI.S/D, FMINM/FMAXM, FLEQ/FLTQ, FROUND/FROUNDNX, and FCVTMOD.W.D; FDT advertises Zfa | RVA23 instruction decode, semantics, flags, and FDT tests |
| Wait-on-reservation-set | Zawrs WRS.NTO/WRS.STO encodings retire immediately (SimRV does not model low-power waits); FDT advertises Zawrs | RVA23 decode, reserved-encoding, retirement, and FDT tests |
| Vector bit manipulation | Zvbb operations decode and execute for supported SEW widths; FDT advertises Zvbb | Core semantic tests for bitwise, rotate, count, reverse, and widening-shift operations |
| Vector data-independent latency | All implemented Zvkt-listed operations have operand-independent guest-cycle latency; FDT advertises Zvkt | Pipeline cycle tests across vector operand patterns and three- and five-stage models |
| Data-independent latency | Implemented Zkt-listed instructions have operand-independent guest-cycle latency; DIV/REM are excluded as specified; Zkt is independent of scalar cryptography (`Zk`); FDT advertises Zkt | Pipeline tests for integer multiply and listed bit-manipulation operations across operand patterns in three- and five-stage models |
| Lower-mode XLEN | RV64 U-mode supports UXL=64 through the `mstatus`/`sstatus` CSR path; RV64 FDT advertises Ssu64xl | RVA23 CSR WARL and FDT tests |
| Supervisor timer interrupts | Sstc exposes `stimecmp` when M-mode enables STCE and asserts/deasserts supervisor timer pending from `mtime`; FDT advertises Sstc | Privilege-memory audit tests for XLEN-specific `stimecmp`, STCE access control, and timer transitions; FDT test |
| Base counters and timers | Zicntr implements `cycle`, `time`, and `instret`; lower-mode reads obey the `mcounteren`/`scounteren` gates; FDT advertises Zicntr | RVA23 counter-value and U/S access tests; FDT tests |
| Hardware performance counters | Zihpm counters 3–31 and event selectors are implemented; `mcounteren` and `scounteren` expose writable enables for implemented counters as required by Sscounterenw; FDT advertises Zihpm and Sscounterenw | HPM overflow/filtering tests and RVA23 S/U counter-enable access tests; FDT tests |
| State-enable CSR banks | `mstateen0-3`, `sstateen0-3`, and H-gated `hstateen0-3` storage is present; M/S enable bits gate CSR visibility, and `hstateen.SEO` controls guest access to `sstateen` CSRs with virtual-instruction faults | RVA23 state-enable CSR access, visibility, and guest SEO tests |
| Hypervisor counter-enable CSR | H-gated `hcounteren` is writable for implemented counters and gates VS/VU counter reads with the required virtual-instruction behavior; S/U access also observes `scounteren` | RVA23 guest-counter-enable and counter-access tests |
| Hypervisor CSR and return substrate | H/VS CSR state and access checks are present; `hgatp` accepts Bare and SvNNx4 modes, `vsatp` follows `satp` WARL modes, S CSR aliases select VS state in virtualization mode, MPV/MRET and SPV/SRET support guest returns, and synchronous traps can route to VS or HS. The RVA23S64 profile enables H; explicit ISA and CPU-model configurations retain their selected MISA | RVA23 H/VS CSR, guest return, and trap-routing tests |
| Hypervisor virtual-machine memory instructions | `HLV.*`, `HLVX.HU/WU`, and `HSV.*` decode and execute with SPVP-selected VS/VU privilege, VS/G-stage translation, load/store fault classes, `GVA`/`htval` reporting, and Ssnpm pointer-mask selection; HLVX uses execute permission while reporting load faults | RVA23 H virtual load/store tests cover decode, VS/G translation, execute-only HLVX, stores, load-page-fault routing, and retirement/writeback |
| Hypervisor interrupt routing | VS software/timer/external pending state, `hideleg`/`mideleg`, VS CSR aliases, and VS trap entry implement standard VS cause-number translation; guest external interrupt files use the H-permitted `GEILEN=0` configuration | RVA23 interrupt delegation WARL and VS trap-entry tests |
| Hypervisor fault values | `vstval` is populated on delegated VS traps; `htval` records GPA >> 2 for guest-page faults from G-stage translation, including VS Bare accesses | RVA23 VS trap-value and Shtvala guest-physical fault-value tests |
| Counter overflow filtering | Sscofpmf implements M/S/U event filtering, counter inhibit, sticky overflow reporting, and delegated LCOFIP; FDT advertises Sscofpmf | RVA23 CSR, overflow, and interrupt-delegation tests |
| User pointer masking | Ssnpm selects U-mode PMLEN=0 or PMLEN=7 through `senvcfg.PMM`; Supm is implied; RV64 FDT advertises both | RVA23 WARL and tagged physical load/store tests; FDT tests |
| NAPOT translation contiguity | Svnapot maps aligned 64 KiB regions through level-0 PTEs for Sv39/Sv48/Sv57; invalid N encodings fault; FDT advertises Svnapot | Svnapot alias translation and reserved-encoding tests; FDT tests |
| Hypervisor guest-stage translation core | CPU fetch and data translation select `vsatp` in guest mode and compose VS-stage walks with G-stage translation through `hgatp`; guest VS Bare accesses and HS HLV/HLVX/HSV accesses also pass through G-stage. `HFENCE.VVMA` and `HFENCE.GVMA` invalidate local translation state. G-stage page-table reads are currently synchronous internal RAM reads, including in cycle mode | RVA23 CPU two-stage translation through `vsatp`, H virtual-memory instruction, guest-physical walker, and HFENCE decode/privilege tests |
| Page-based memory types | Svpbmt decodes PMA/NC/IO leaf attributes and faults reserved PBMT encodings; NC/IO accesses bypass L1 data-cache paths and use coherent TileLink transfers; ordered execution and fences preserve IO and cross-attribute ordering; AMOs fault on non-PMA mappings without atomicity support; FDT advertises Svpbmt | Svpbmt PTE legality, uncached access, dirty-owner writeback, cache-bypass, non-PMA AMO-fault, and cycle-mode mixed-attribute fence-order tests |
| Accessed/dirty tracking | Mandatory Svade behavior: guest accesses fault when a required PTE A/D bit is clear; unsupported Svadu `menvcfg.ADUE` is read-only zero; FDT advertises Svade | RVA23 CPU page-walk tests for load/store page faults, PTE preservation, and ADUE WARL behavior; FDT tests |
| Supervisor trap entry | Sstvecd Direct mode stores any four-byte-aligned BASE; Sstvala records required delegated fault addresses and the faulting instruction for illegal-instruction exceptions; FDT advertises both | RVA23 supervisor trap tests for Direct vector WARL behavior and delegated fault values; FDT tests |
| TLB invalidation | Svinval (`sinval.vma`, `sfence.w.inval`, `sfence.inval.ir`) | Core semantic, RVA23 profile gates, and ACT suite |
| Vector | RISC-V Vector 1.0 (`V`), with configurable VLEN; RVA23 minimum VLEN=128 | 2391 Chips Alliance vector tests pass at VLEN=128 and 256; ACT vector checks, except the audited `vstart` expectation conflict below |
| Privilege | M/S/U execution, traps, interrupts, and CSR access (Privileged ISA 1.13) | Core semantic, ACT suite, and Linux boot gates |
| Address translation | Sv32 for RV32 and Sv39 for RV64; Bare mode is supported; Ssccptr hardware page-table reads use coherent main memory; FDT advertises Ssccptr | MMU and Svade page-walk tests, and Linux boot gates; FDT tests |

RVA23S64 includes every RVA23U64 mandatory extension, plus Zifencei and the privileged
requirements defined by RVA23S64. The profile's mandatory unprivileged set starts with the
little-endian RV64I base, followed by M, A, F, D,
C, B, Zicsr, Zicntr, Zihpm, Ziccif, Ziccrse, Ziccamoa, Zicclsm, Za64rs, Zihintpause, Zic64b,
Zicbom, Zicbop, Zicboz, Zfhmin, Zkt, V, Zvfhmin, Zvbb, Zvkt, Zihintntl, Zicond, Zimop, Zcmop,
Zcb, Zfa, Zawrs, and Supm. RVA23S64 additionally mandates Zifencei, Ss1p13, Svbare, Sv39,
Svade, Ssccptr, Sstvecd, Sstvala, Sscounterenw, Svpbmt, Svinval, Svnapot, Sstc, Sscofpmf, Ssnpm,
Ssu64xl, and Sha. Sha comprises H, Ssstateen, Shcounterenw, Shvstvala, Shtvala, Shvstvecd,
Shvsatpa, and Shgatpa. The table above records SimRV's implementation and evidence for these
requirements; the [ratified RVA23 v1.0 profile](https://docs.riscv.org/reference/rva23/v1.0/rva23-profiles.html)
defines their normative behavior.

RVA23 optional expansions are not part of the mandatory target. They include, among others,
Sv48, Sv57, Zkr, Svadu, Sdtrig, Ssstrict, Svvptc, and Sspm on the supervisor side. In particular,
the RVA23S64 profile does not require Ssstrict; its mandatory hypervisor and base profile
requirements still apply.

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
- **Continuous Integration**: The `rva23-act` suite is registered as a required verification gate in RV64 release evidence generation. It is not an RV32 profile test; the RV32 CI matrix runs its own width-specific gates.

The full upstream ACT4 RVA23S64 ELF suite was run with VLEN=128 and 12 jobs: **3138/3188 passed**.
The 50 remaining failures are two expectation conflicts with the ratified specifications:

- 42 `ExceptionsVx-*` checks expect successful vector instructions to leave a nonzero `vstart`.
  [Vector 1.0 requires all vector instructions to reset `vstart` to zero when execution
  completes](https://docs.riscv.org/reference/isa/v20260120/unpriv/v-st-ext.html).
- 8 `InterruptsS_priority_{sie,sip}_{S,U,VS,VU}` checks expect VSEI to outrank SEI when both are
  pending for HS-mode. [H 1.0 specifies SEI above VSEI for HS-mode destinations in the interrupt
  priority order](https://docs.riscv.org/reference/isa/v20260120/priv/hypervisor.html).

These checks remain failures in the raw ACT result; SimRV preserves the specified behavior. This
run is evidence for the tested behaviors, not a claim of ACT-clean or complete RVA23 conformance.
The source suite revision is the `tests/riscv-arch-test` submodule revision. [ACT run instructions
are maintained in the repository](https://github.com/archlab-sciencetokyo/SimRV/blob/dev/tests/act4/README.md).

## RVA23S64 qualification status

The current mandatory-feature audit found no remaining implementation gap. Evidence includes the
profile-specific regression tests, 2391 Chips Alliance base-V tests passing at both VLEN=128 and
VLEN=256, and the full upstream ACT4 RVA23S64 run. Sha's seven constituent ACT categories also
pass: Ssstateen, Shcounterenw, Shvstvala, Shtvala, Shvstvecd, Shvsatpa, and Shgatpa. The ACT suite's
50 recorded failures are the audited Vector and H expectation conflicts above; they are preserved
as failures in the raw report and do not justify deviating from the ratified specifications.

This evidence is not formal RISC-V International certification. The table and exact mandatory
inventory above follow the [ratified RVA23 v1.0 profile](https://docs.riscv.org/reference/rva23/v1.0/rva23-profiles.html).
FDT advertisements describe implemented capabilities and are not themselves evidence of
conformance.

- Scalar and vector floating-point support RNE, RTZ, RDN, RUP, and RMM. Scalar RMM arithmetic,
  fused operations, narrowing, and integer-to-float conversions use software tie detection over
  host RNE results; integer conversions to integer and binary16 conversions implement RMM
  explicitly. Focused scalar and vector semantic tests cover halfway cases and accrued flags. This
  has focused halfway-case and accrued-flag coverage; the vector suite additionally exercises the
  mandatory base vector FP instruction families at VLEN=128 and VLEN=256.
- The Chips Alliance vector suite covers the base V instruction families and passes at the RVA23
  minimum VLEN=128 and at VLEN=256. The profile-mandated Zvbb, Zvfhmin, and Zvkt features have
  focused implementation tests. Zvkb is not a separate RVA23 mandatory extension. Optional Zvfh
  arithmetic is not advertised; SEW=16 vector FP is limited to the conversions required by
  Zvfhmin.

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
