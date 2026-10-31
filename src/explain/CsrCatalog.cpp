/**
 * @file CsrCatalog.cpp
 * @brief CSR architectural specification metadata and symbolic naming.
 *
 * Citations reference The RISC-V Instruction Set Manual, Volume I & Volume II (Version 20260120).
 */
#include <array>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "simrv/explain/InstructionExplainer.hpp"

namespace simrv::explain {

namespace {

struct CsrDetailsEntry {
    uint32_t addr;
    std::string_view name;
    std::string_view description;
    simrv::xlen::PrivilegeLevel min_privilege;
    bool read_only;
    std::string_view extension;
    std::string_view spec_section;
};

}  // namespace

auto get_csr_details(uint32_t csr_addr) -> std::optional<CsrDetails> {
    using simrv::xlen::PrivilegeLevel;
    static constexpr std::array<CsrDetailsEntry, 92> kCsrEntries = {{
        // User Floating-Point (F/D)
        {0x001, "fflags", "Floating-Point Accrued Exceptions", PrivilegeLevel::User, false, "F/D",
         "Vol. I: Ch. 20 (F Floating-Point Control and Status Registers)"},
        {0x002, "frm", "Floating-Point Dynamic Rounding Mode", PrivilegeLevel::User, false, "F/D",
         "Vol. I: Ch. 20 (F Floating-Point Control and Status Registers)"},
        {0x003, "fcsr", "Floating-Point Control and Status Register (frm + fflags)",
         PrivilegeLevel::User, false, "F/D",
         "Vol. I: Ch. 20 (F Floating-Point Control and Status Registers)"},
        // User Vector (RVV 1.0)
        {0x008, "vstart", "Vector start position for resuming interrupted operations",
         PrivilegeLevel::User, false, "Vector", "Vol. I: Sec. 30.3 (Vector Control and Status)"},
        {0x009, "vxsat", "Vector fixed-point saturate flag", PrivilegeLevel::User, false, "Vector",
         "Vol. I: Sec. 30.3 (Vector Control and Status)"},
        {0x00A, "vxrm", "Vector fixed-point rounding mode", PrivilegeLevel::User, false, "Vector",
         "Vol. I: Sec. 30.3 (Vector Control and Status)"},
        {0x00F, "vcsr", "Vector control and status composite register", PrivilegeLevel::User, false,
         "Vector", "Vol. I: Sec. 30.3 (Vector Control and Status)"},
        {0xC20, "vl", "Vector length: active element count", PrivilegeLevel::User, true, "Vector",
         "Vol. I: Sec. 30.3.2 (Vector Length Register vl)"},
        {0xC21, "vtype", "Vector data type register (vsew, vlmul, vta, vma)", PrivilegeLevel::User,
         true, "Vector", "Vol. I: Sec. 30.3.1 (Vector Type Register vtype)"},
        {0xC22, "vlenb", "Vector register length in bytes (VLEN/8)", PrivilegeLevel::User, true,
         "Vector", "Vol. I: Sec. 30.3.3 (Vector Byte Length vlenb)"},
        // User Counters & Timers (Zicntr)
        {0xC00, "cycle", "Cycle counter for RDCYCLE instruction", PrivilegeLevel::User, true,
         "Zicntr", "Vol. I: Ch. 6 (Zicntr and Zihpm Counter Extensions)"},
        {0xC01, "time", "Timer for RDTIME instruction", PrivilegeLevel::User, true, "Zicntr",
         "Vol. I: Ch. 6 (Zicntr and Zihpm Counter Extensions)"},
        {0xC02, "instret", "Instructions-retired counter for RDINSTRET", PrivilegeLevel::User, true,
         "Zicntr", "Vol. I: Ch. 6 (Zicntr and Zihpm Counter Extensions)"},
        {0xC80, "cycleh", "Upper 32 bits of cycle counter (RV32)", PrivilegeLevel::User, true,
         "Zicntr", "Vol. I: Ch. 6 (Zicntr and Zihpm Counter Extensions)"},
        {0xC81, "timeh", "Upper 32 bits of timer (RV32)", PrivilegeLevel::User, true, "Zicntr",
         "Vol. I: Ch. 6 (Zicntr and Zihpm Counter Extensions)"},
        {0xC82, "instreth", "Upper 32 bits of instructions-retired counter (RV32)",
         PrivilegeLevel::User, true, "Zicntr",
         "Vol. I: Ch. 6 (Zicntr and Zihpm Counter Extensions)"},
        // Supervisor Trap Setup & Protection
        {0x100, "sstatus", "Supervisor Status Register", PrivilegeLevel::Supervisor, false,
         "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, sstatus CSR)"},
        {0x104, "sie", "Supervisor Interrupt-Enable Register", PrivilegeLevel::Supervisor, false,
         "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, sie CSR)"},
        {0x105, "stvec", "Supervisor Trap-Vector Base-Address Register", PrivilegeLevel::Supervisor,
         false, "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, stvec CSR)"},
        {0x106, "scounteren", "Supervisor Counter Enable Register", PrivilegeLevel::Supervisor,
         false, "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, scounteren CSR)"},
        {0x10A, "senvcfg", "Supervisor Environment Configuration Register",
         PrivilegeLevel::Supervisor, false, "Privileged",
         "Vol. II: Ch. 11 (Supervisor-Level ISA, senvcfg CSR)"},
        {0x140, "sscratch", "Supervisor Scratch Register", PrivilegeLevel::Supervisor, false,
         "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, sscratch CSR)"},
        {0x141, "sepc", "Supervisor Exception Program Counter", PrivilegeLevel::Supervisor, false,
         "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, sepc CSR)"},
        {0x142, "scause", "Supervisor Trap Cause Register", PrivilegeLevel::Supervisor, false,
         "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, scause CSR)"},
        {0x143, "stval", "Supervisor Trap Value Register", PrivilegeLevel::Supervisor, false,
         "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, stval CSR)"},
        {0x144, "sip", "Supervisor Interrupt-Pending Register", PrivilegeLevel::Supervisor, false,
         "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, sip CSR)"},
        {0x14D, "stimecmp", "Supervisor Timer Compare Register (Sstc)", PrivilegeLevel::Supervisor,
         false, "Sstc", "Vol. II: Ch. 12 (Sstc Supervisor-Mode Timer Interrupts)"},
        {0x15D, "stimecmph", "Supervisor Timer Compare Register High (Sstc RV32)",
         PrivilegeLevel::Supervisor, false, "Sstc",
         "Vol. II: Ch. 12 (Sstc Supervisor-Mode Timer Interrupts)"},
        {0x180, "satp", "Supervisor Address Translation and Protection", PrivilegeLevel::Supervisor,
         false, "Privileged", "Vol. II: Ch. 11 (Supervisor-Level ISA, satp CSR)"},
        // Virtual Supervisor (VS) CSR aliases and guest timer (H)
        {0x200, "vsstatus", "Virtual Supervisor Status Register", PrivilegeLevel::Supervisor, false,
         "H", "Vol. II: Ch. 14 (H Extension, vsstatus CSR)"},
        {0x204, "vsie", "Virtual Supervisor Interrupt-Enable Register", PrivilegeLevel::Supervisor,
         false, "H", "Vol. II: Ch. 14 (H Extension, vsie CSR)"},
        {0x205, "vstvec", "Virtual Supervisor Trap-Vector Base-Address Register",
         PrivilegeLevel::Supervisor, false, "H", "Vol. II: Ch. 14 (H Extension, vstvec CSR)"},
        {0x240, "vsscratch", "Virtual Supervisor Scratch Register", PrivilegeLevel::Supervisor,
         false, "H", "Vol. II: Ch. 14 (H Extension, vsscratch CSR)"},
        {0x241, "vsepc", "Virtual Supervisor Exception Program Counter", PrivilegeLevel::Supervisor,
         false, "H", "Vol. II: Ch. 14 (H Extension, vsepc CSR)"},
        {0x242, "vscause", "Virtual Supervisor Cause Register", PrivilegeLevel::Supervisor, false,
         "H", "Vol. II: Ch. 14 (H Extension, vscause CSR)"},
        {0x243, "vstval", "Virtual Supervisor Trap Value Register", PrivilegeLevel::Supervisor,
         false, "H", "Vol. II: Ch. 14 (H Extension, vstval CSR)"},
        {0x244, "vsip", "Virtual Supervisor Interrupt-Pending Register", PrivilegeLevel::Supervisor,
         false, "H", "Vol. II: Ch. 14 (H Extension, vsip CSR)"},
        {0x24D, "vstimecmp", "Virtual Supervisor Timer Compare Register",
         PrivilegeLevel::Supervisor, false, "H", "Vol. II: Ch. 14 (H Extension, vstimecmp CSR)"},
        {0x25D, "vstimecmph", "Virtual Supervisor Timer Compare High (RV32)",
         PrivilegeLevel::Supervisor, false, "H", "Vol. II: Ch. 14 (H Extension, vstimecmph CSR)"},
        {0x280, "vsatp", "Virtual Supervisor Address Translation and Protection",
         PrivilegeLevel::Supervisor, false, "H", "Vol. II: Ch. 14 (H Extension, vsatp CSR)"},
        // State-enable CSR banks (Smstateen / Ssstateen)
        {0x10C, "sstateen0", "Supervisor State-Enable Register 0", PrivilegeLevel::Supervisor,
         false, "Ssstateen", "Vol. II: Ch. 3 (Smstateen/Ssstateen, sstateen0 CSR)"},
        {0x10D, "sstateen1", "Supervisor State-Enable Register 1", PrivilegeLevel::Supervisor,
         false, "Ssstateen", "Vol. II: Ch. 3 (Smstateen/Ssstateen, sstateen1 CSR)"},
        {0x10E, "sstateen2", "Supervisor State-Enable Register 2", PrivilegeLevel::Supervisor,
         false, "Ssstateen", "Vol. II: Ch. 3 (Smstateen/Ssstateen, sstateen2 CSR)"},
        {0x10F, "sstateen3", "Supervisor State-Enable Register 3", PrivilegeLevel::Supervisor,
         false, "Ssstateen", "Vol. II: Ch. 3 (Smstateen/Ssstateen, sstateen3 CSR)"},
        // Hypervisor trap, interrupt, configuration, and translation CSRs (Sha / H)
        {0x600, "hstatus", "Hypervisor Status Register", PrivilegeLevel::Supervisor, false, "H",
         "Vol. II: Ch. 14 (H Extension, hstatus CSR)"},
        {0x602, "hedeleg", "Hypervisor Exception Delegation Register", PrivilegeLevel::Supervisor,
         false, "H", "Vol. II: Ch. 14 (H Extension, hedeleg CSR)"},
        {0x603, "hideleg", "Hypervisor Interrupt Delegation Register", PrivilegeLevel::Supervisor,
         false, "H", "Vol. II: Ch. 14 (H Extension, hideleg CSR)"},
        {0x604, "hie", "Hypervisor Interrupt-Enable Register", PrivilegeLevel::Supervisor, false,
         "H", "Vol. II: Ch. 14 (H Extension, hie CSR)"},
        {0x606, "hcounteren", "Hypervisor Counter-Enable Register", PrivilegeLevel::Supervisor,
         false, "Shcounterenw", "Vol. II: Ch. 14 (H Extension, hcounteren CSR)"},
        {0x607, "hgeie", "Hypervisor Guest External Interrupt-Enable Register",
         PrivilegeLevel::Supervisor, false, "H", "Vol. II: Ch. 14 (H Extension, hgeie CSR)"},
        {0x60A, "henvcfg", "Hypervisor Environment Configuration Register",
         PrivilegeLevel::Supervisor, false, "H", "Vol. II: Ch. 14 (H Extension, henvcfg CSR)"},
        {0x643, "htval", "Hypervisor Trap Value Register", PrivilegeLevel::Supervisor, false,
         "Shtvala", "Vol. II: Ch. 14 (H Extension, htval CSR)"},
        {0x644, "hip", "Hypervisor Interrupt-Pending Register", PrivilegeLevel::Supervisor, false,
         "H", "Vol. II: Ch. 14 (H Extension, hip CSR)"},
        {0x645, "hvip", "Hypervisor Virtual-Interrupt-Pending Register", PrivilegeLevel::Supervisor,
         false, "H", "Vol. II: Ch. 14 (H Extension, hvip CSR)"},
        {0x64A, "htinst", "Hypervisor Trap Instruction Register", PrivilegeLevel::Supervisor, false,
         "H", "Vol. II: Ch. 14 (H Extension, htinst CSR)"},
        {0x680, "hgatp", "Hypervisor Guest Address Translation and Protection",
         PrivilegeLevel::Supervisor, false, "Shgatpa", "Vol. II: Ch. 14 (H Extension, hgatp CSR)"},
        {0xE12, "hgeip", "Hypervisor Guest External Interrupt-Pending Register",
         PrivilegeLevel::Supervisor, true, "H", "Vol. II: Ch. 14 (H Extension, hgeip CSR)"},
        // Machine and hypervisor state-enable CSR banks
        {0x30C, "mstateen0", "Machine State-Enable Register 0", PrivilegeLevel::Machine, false,
         "Smstateen", "Vol. II: Ch. 3 (Smstateen, mstateen0 CSR)"},
        {0x30D, "mstateen1", "Machine State-Enable Register 1", PrivilegeLevel::Machine, false,
         "Smstateen", "Vol. II: Ch. 3 (Smstateen, mstateen1 CSR)"},
        {0x30E, "mstateen2", "Machine State-Enable Register 2", PrivilegeLevel::Machine, false,
         "Smstateen", "Vol. II: Ch. 3 (Smstateen, mstateen2 CSR)"},
        {0x30F, "mstateen3", "Machine State-Enable Register 3", PrivilegeLevel::Machine, false,
         "Smstateen", "Vol. II: Ch. 3 (Smstateen, mstateen3 CSR)"},
        {0x60C, "hstateen0", "Hypervisor State-Enable Register 0", PrivilegeLevel::Supervisor,
         false, "Ssstateen", "Vol. II: Ch. 3 (Ssstateen, hstateen0 CSR)"},
        {0x60D, "hstateen1", "Hypervisor State-Enable Register 1", PrivilegeLevel::Supervisor,
         false, "Ssstateen", "Vol. II: Ch. 3 (Ssstateen, hstateen1 CSR)"},
        {0x60E, "hstateen2", "Hypervisor State-Enable Register 2", PrivilegeLevel::Supervisor,
         false, "Ssstateen", "Vol. II: Ch. 3 (Ssstateen, hstateen2 CSR)"},
        {0x60F, "hstateen3", "Hypervisor State-Enable Register 3", PrivilegeLevel::Supervisor,
         false, "Ssstateen", "Vol. II: Ch. 3 (Ssstateen, hstateen3 CSR)"},
        // Machine Trap Setup & Handling
        {0x300, "mstatus", "Machine Status Register", PrivilegeLevel::Machine, false, "Privileged",
         "Vol. II: Ch. 2 (Machine-Level ISA, mstatus CSR)"},
        {0x301, "misa", "Machine ISA and Extensions Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, misa CSR)"},
        {0x302, "medeleg", "Machine Exception Delegation Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, medeleg CSR)"},
        {0x303, "mideleg", "Machine Interrupt Delegation Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mideleg CSR)"},
        {0x304, "mie", "Machine Interrupt-Enable Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mie CSR)"},
        {0x305, "mtvec", "Machine Trap-Vector Base-Address Register", PrivilegeLevel::Machine,
         false, "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mtvec CSR)"},
        {0x306, "mcounteren", "Machine Counter Enable Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mcounteren CSR)"},
        {0x30A, "menvcfg", "Machine Environment Configuration Register", PrivilegeLevel::Machine,
         false, "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, menvcfg CSR)"},
        {0x310, "mstatush", "Machine Status Register High (RV32)", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mstatush CSR)"},
        {0x31A, "menvcfgh", "Machine Environment Configuration High (RV32)",
         PrivilegeLevel::Machine, false, "Privileged",
         "Vol. II: Ch. 2 (Machine-Level ISA, menvcfgh CSR)"},
        {0x320, "mcountinhibit", "Machine Counter-Inhibit Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mcountinhibit CSR)"},
        {0x340, "mscratch", "Machine Scratch Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mscratch CSR)"},
        {0x341, "mepc", "Machine Exception Program Counter", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mepc CSR)"},
        {0x342, "mcause", "Machine Trap Cause Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mcause CSR)"},
        {0x343, "mtval", "Machine Trap Value Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mtval CSR)"},
        {0x344, "mip", "Machine Interrupt-Pending Register", PrivilegeLevel::Machine, false,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mip CSR)"},
        {0x747, "mseccfg", "Machine Security Configuration (Smepmp)", PrivilegeLevel::Machine,
         false, "Smepmp", "Vol. II: Ch. 5 (Smepmp Machine Security Configuration, mseccfg CSR)"},
        {0x757, "mseccfgh", "Machine Security Configuration High (Smepmp RV32)",
         PrivilegeLevel::Machine, false, "Smepmp",
         "Vol. II: Ch. 5 (Smepmp Machine Security Configuration, mseccfgh CSR)"},
        // Machine Identification
        {0xF11, "mvendorid", "Machine Vendor ID Register", PrivilegeLevel::Machine, true,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mvendorid CSR)"},
        {0xF12, "marchid", "Machine Architecture ID Register", PrivilegeLevel::Machine, true,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, marchid CSR)"},
        {0xF13, "mimpid", "Machine Implementation ID Register", PrivilegeLevel::Machine, true,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mimpid CSR)"},
        {0xF14, "mhartid", "Hardware Thread ID Register", PrivilegeLevel::Machine, true,
         "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mhartid CSR)"},
        {0xF15, "mconfigptr", "Machine Configuration Pointer Register", PrivilegeLevel::Machine,
         true, "Privileged", "Vol. II: Ch. 2 (Machine-Level ISA, mconfigptr CSR)"},
        // Machine Counters
        {0xB00, "mcycle", "Machine Cycle Counter", PrivilegeLevel::Machine, false, "Zicntr",
         "Vol. II: Ch. 2 (Machine-Level ISA, Hardware Performance Monitor CSRs)"},
        {0xB02, "minstret", "Machine Instructions-Retired Counter", PrivilegeLevel::Machine, false,
         "Zicntr", "Vol. II: Ch. 2 (Machine-Level ISA, Hardware Performance Monitor CSRs)"},
        {0xB80, "mcycleh", "Machine Cycle Counter High (RV32)", PrivilegeLevel::Machine, false,
         "Zicntr", "Vol. II: Ch. 2 (Machine-Level ISA, Hardware Performance Monitor CSRs)"},
        {0xB82, "minstreth", "Machine Instructions-Retired Counter High (RV32)",
         PrivilegeLevel::Machine, false, "Zicntr",
         "Vol. II: Ch. 2 (Machine-Level ISA, Hardware Performance Monitor CSRs)"},
    }};

    for (const auto& entry : kCsrEntries) {
        if (entry.addr == csr_addr) {
            return CsrDetails{.name = entry.name,
                              .description = entry.description,
                              .min_privilege = entry.min_privilege,
                              .read_only = entry.read_only,
                              .extension = entry.extension,
                              .spec_section = entry.spec_section};
        }
    }

    // PMP configuration registers
    if (csr_addr >= 0x3A0 && csr_addr <= 0x3A3) {
        static constexpr std::array<std::string_view, 4> kPmpcfg = {"pmpcfg0", "pmpcfg1", "pmpcfg2",
                                                                    "pmpcfg3"};
        return CsrDetails{.name = kPmpcfg[csr_addr - 0x3A0],
                          .description = "Physical Memory Protection configuration register",
                          .min_privilege = PrivilegeLevel::Machine,
                          .read_only = false,
                          .extension = "Smepmp / PMP",
                          .spec_section = "Vol. II: Ch. 2 (Physical Memory Protection CSRs)"};
    }
    // PMP address registers
    if (csr_addr >= 0x3B0 && csr_addr <= 0x3BF) {
        static constexpr std::array<std::string_view, 16> kPmpaddr = {
            "pmpaddr0",  "pmpaddr1",  "pmpaddr2",  "pmpaddr3", "pmpaddr4",  "pmpaddr5",
            "pmpaddr6",  "pmpaddr7",  "pmpaddr8",  "pmpaddr9", "pmpaddr10", "pmpaddr11",
            "pmpaddr12", "pmpaddr13", "pmpaddr14", "pmpaddr15"};
        return CsrDetails{.name = kPmpaddr[csr_addr - 0x3B0],
                          .description = "Physical Memory Protection address register",
                          .min_privilege = PrivilegeLevel::Machine,
                          .read_only = false,
                          .extension = "Smepmp / PMP",
                          .spec_section = "Vol. II: Ch. 2 (Physical Memory Protection CSRs)"};
    }

    return std::nullopt;
}

auto csr_name(uint32_t csr_addr) -> std::string {
    if (auto details = get_csr_details(csr_addr)) {
        return std::string(details->name);
    }
    if (csr_addr >= 0x323 && csr_addr <= 0x33F) {
        return std::format("mhpmevent{}", csr_addr - 0x320);
    }
    if (csr_addr >= 0xB03 && csr_addr <= 0xB1F) {
        return std::format("mhpmcounter{}", csr_addr - 0xB00);
    }
    if (csr_addr >= 0xB83 && csr_addr <= 0xB9F) {
        return std::format("mhpmcounter{}h", csr_addr - 0xB80);
    }
    if (csr_addr >= 0xC03 && csr_addr <= 0xC1F) {
        return std::format("hpmcounter{}", csr_addr - 0xC00);
    }
    if (csr_addr >= 0xC83 && csr_addr <= 0xC9F) {
        return std::format("hpmcounter{}h", csr_addr - 0xC80);
    }
    return std::format("0x{:03X}", csr_addr);
}

}  // namespace simrv::explain
