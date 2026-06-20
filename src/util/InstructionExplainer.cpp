/**
 * @file InstructionExplainer.cpp
 * @brief Visual/educational instruction decoder and explainer.
 */
#include "simrv/util/InstructionExplainer.hpp"

#include <print>
#include <format>
#include <string>
#include <string_view>
#include <array>
#include <unistd.h>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::util {

using namespace simrv::util::ansi;

auto csr_name(uint32_t csr_addr) -> std::string {
    switch (static_cast<Csr>(csr_addr)) {
        case Csr::Ustatus: return "ustatus";
        case Csr::Uie: return "uie";
        case Csr::Utvec: return "utvec";
        case Csr::Uscratch: return "uscratch";
        case Csr::Uepc: return "uepc";
        case Csr::Ucause: return "ucause";
        case Csr::Utval: return "utval";
        case Csr::Uip: return "uip";
        case Csr::Fflags: return "fflags";
        case Csr::Frm: return "frm";
        case Csr::Fcsr: return "fcsr";
        case Csr::Pmpcfg0: return "pmpcfg0";
        case Csr::Pmpaddr0: return "pmpaddr0";
        case Csr::Cycle: return "cycle";
        case Csr::Time: return "time";
        case Csr::Instret: return "instret";
        case Csr::Sstatus: return "sstatus";
        case Csr::Sedeleg: return "sedeleg";
        case Csr::Sideleg: return "sideleg";
        case Csr::Sie: return "sie";
        case Csr::Stvec: return "stvec";
        case Csr::Scounteren: return "scounteren";
        case Csr::Sscratch: return "sscratch";
        case Csr::Sepc: return "sepc";
        case Csr::Scause: return "scause";
        case Csr::Stval: return "stval";
        case Csr::Sip: return "sip";
        case Csr::Satp: return "satp";
        case Csr::Mvendorid: return "mvendorid";
        case Csr::Marchid: return "marchid";
        case Csr::Mimpid: return "mimpid";
        case Csr::Mhartid: return "mhartid";
        case Csr::Mconfigptr: return "mconfigptr";
        case Csr::Mstatus: return "mstatus";
        case Csr::Misa: return "misa";
        case Csr::Medeleg: return "medeleg";
        case Csr::Mideleg: return "mideleg";
        case Csr::Mie: return "mie";
        case Csr::Mtvec: return "mtvec";
        case Csr::Mcounteren: return "mcounteren";
        case Csr::Mscratch: return "mscratch";
        case Csr::Mepc: return "mepc";
        case Csr::Mcause: return "mcause";
        case Csr::Mtval: return "mtval";
        case Csr::Mip: return "mip";
        case Csr::Mcycle: return "mcycle";
        case Csr::Minstret: return "minstret";
        case Csr::Mcycleh: return "mcycleh";
        case Csr::Minstreth: return "minstreth";
        case Csr::Cycleh: return "cycleh";
        case Csr::Timeh: return "timeh";
        case Csr::Instreth: return "instreth";
        default: return std::format("0x{:03X}", csr_addr);
    }
}

namespace {

enum class InstFormat : uint8_t {
    R,
    I,
    S,
    B,
    U,
    J,
    R4,
    Unknown
};

const std::array<const char*, 32> ABI_NAMES = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0/fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3",    "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5",    "t6"};

const std::array<const char*, 32> FP_ABI_NAMES = {
    "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
    "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
    "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

auto reg_name(uint32_t r, bool is_fp = false) -> std::string {
    if (r >= 32) return std::format("r{}", r);
    if (is_fp) {
        return std::format("f{} ({})", r, FP_ABI_NAMES[r]);
    } else {
        return std::format("x{} ({})", r, ABI_NAMES[r]);
    }
}

auto get_format(simrv::pipeline::Opcode op) -> InstFormat {
    using enum simrv::pipeline::Opcode;
    switch (op) {
        case kLoad:
        case kLoadFp:
        case kMiscMem:
        case kOpImm:
        case kOpImm32:
        case kJalr:
        case kSystem:
            return InstFormat::I;
        case kStore:
        case kStoreFp:
            return InstFormat::S;
        case kBranch:
            return InstFormat::B;
        case kAuipc:
        case kLui:
            return InstFormat::U;
        case kJal:
            return InstFormat::J;
        case kOp:
        case kOp32:
        case kAmo:
        case kOpFp:
            return InstFormat::R;
        case kMadd:
        case kMsub:
        case kNmsub:
        case kNmadd:
            return InstFormat::R4;
        default:
            return InstFormat::Unknown;
    }
}

auto get_format_name(InstFormat fmt) -> std::string_view {
    switch (fmt) {
        case InstFormat::R: return "R-Type (Register-Register)";
        case InstFormat::I: return "I-Type (Register-Immediate / Load / Jump)";
        case InstFormat::S: return "S-Type (Store)";
        case InstFormat::B: return "B-Type (Branch)";
        case InstFormat::U: return "U-Type (Upper Immediate)";
        case InstFormat::J: return "J-Type (Unconditional Jump)";
        case InstFormat::R4: return "R4-Type (Fused Multiply-Add)";
        case InstFormat::Unknown: return "Unknown / Custom Format";
    }
    return "Unknown";
}

auto get_description(::OperationId op_id) -> std::pair<std::string_view, std::string_view> {
    static constexpr std::pair<std::string_view, std::string_view> kDefaultDesc = {
        "UNKNOWN", "Instruction not explicitly detailed or custom extension opcode. Verify against compiler specification."
    };

    static const auto kOpDescriptions = []() {
        std::array<std::pair<std::string_view, std::string_view>, kOperationIdCount> arr;
        arr.fill(kDefaultDesc);

        arr[LUI] = {"LUI", "Load Upper Immediate. Loads the 20-bit immediate into the upper 20 bits of the destination register rd, filling the lowest 12 bits with zeros."};
        arr[AUIPC] = {"AUIPC", "Add Upper Immediate to PC. Adds the 20-bit sign-extended immediate to the PC and stores the result in rd."};
        arr[JAL] = {"JAL", "Jump and Link. Jump to the address PC + offset, and store the return address (PC + 4) in rd."};
        arr[JALR] = {"JALR", "Jump and Link Register. Jump to the address (rs1 + offset) & ~1, and store the return address (PC + 4) in rd."};
        arr[BEQ] = {"BEQ", "Branch if Equal. Branch to PC + offset if the values in rs1 and rs2 are equal."};
        arr[BNE] = {"BNE", "Branch if Not Equal. Branch to PC + offset if the values in rs1 and rs2 are not equal."};
        arr[BLT] = {"BLT", "Branch if Less Than. Branch to PC + offset if the value in rs1 is less than rs2 (signed comparison)."};
        arr[BGE] = {"BGE", "Branch if Greater than or Equal. Branch to PC + offset if the value in rs1 is greater than or equal to rs2 (signed comparison)."};
        arr[BLTU] = {"BLTU", "Branch if Less Than (Unsigned). Branch to PC + offset if the value in rs1 is less than rs2 (unsigned comparison)."};
        arr[BGEU] = {"BGEU", "Branch if Greater than or Equal (Unsigned). Branch to PC + offset if the value in rs1 is greater than or equal to rs2 (unsigned comparison)."};
        arr[LB] = {"LB", "Load Byte. Read an 8-bit value from memory address (rs1 + offset), sign-extend it to XLEN, and store it in rd."};
        arr[LH] = {"LH", "Load Halfword. Read a 16-bit value from memory address (rs1 + offset), sign-extend it to XLEN, and store it in rd."};
        arr[LW] = {"LW", "Load Word. Read a 32-bit value from memory address (rs1 + offset), sign-extend it to XLEN (in RV64) or XLEN, and store it in rd."};
        arr[LD] = {"LD", "Load Doubleword. Read a 64-bit value from memory address (rs1 + offset) and store it in rd."};
        arr[LBU] = {"LBU", "Load Byte (Unsigned). Read an 8-bit value from memory address (rs1 + offset), zero-extend it, and store it in rd."};
        arr[LHU] = {"LHU", "Load Halfword (Unsigned). Read a 16-bit value from memory address (rs1 + offset), zero-extend it, and store it in rd."};
        arr[LWU] = {"LWU", "Load Word (Unsigned). Read a 32-bit value from memory address (rs1 + offset), zero-extend it, and store it in rd."};
        arr[SB] = {"SB", "Store Byte. Write the lower 8 bits of rs2 to memory address (rs1 + offset)."};
        arr[SH] = {"SH", "Store Halfword. Write the lower 16 bits of rs2 to memory address (rs1 + offset)."};
        arr[SW] = {"SW", "Store Word. Write the lower 32 bits of rs2 to memory address (rs1 + offset)."};
        arr[SD] = {"SD", "Store Doubleword. Write the 64-bit value of rs2 to memory address (rs1 + offset)."};
        arr[ADDI] = {"ADDI", "Add Immediate. Add the sign-extended 12-bit immediate to rs1 and store the result in rd."};
        arr[SLTI] = {"SLTI", "Set if Less Than Immediate. Set rd to 1 if rs1 is less than the sign-extended immediate (signed comparison), otherwise set rd to 0."};
        arr[SLTIU] = {"SLTIU", "Set if Less Than Immediate (Unsigned). Set rd to 1 if rs1 is less than the sign-extended immediate (unsigned comparison), otherwise set rd to 0."};
        arr[XORI] = {"XORI", "Bitwise XOR Immediate. Performs bitwise XOR of rs1 and the sign-extended immediate, and stores the result in rd."};
        arr[ORI] = {"ORI", "Bitwise OR Immediate. Performs bitwise OR of rs1 and the sign-extended immediate, and stores the result in rd."};
        arr[ANDI] = {"ANDI", "Bitwise AND Immediate. Performs bitwise AND of rs1 and the sign-extended immediate, and stores the result in rd."};
        arr[SLLI] = {"SLLI", "Shift Left Logical Immediate. Performs logical left shift of rs1 by the shift amount (shamt) and stores the result in rd."};
        arr[SRLI] = {"SRLI", "Shift Right Logical Immediate. Performs logical right shift of rs1 by the shift amount (shamt) and stores the result in rd."};
        arr[SRAI] = {"SRAI", "Shift Right Arithmetic Immediate. Performs arithmetic right shift of rs1 by the shift amount (shamt), preserving the sign bit, and stores the result in rd."};
        arr[ADDIW] = {"ADDIW", "Add Immediate Word. Adds the sign-extended 12-bit immediate to rs1, truncating the result to 32 bits, sign-extending to 64 bits, and storing in rd."};
        arr[SLLIW] = {"SLLIW", "Shift Left Logical Immediate Word. Performs logical left shift of 32-bit rs1 by shamt, sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[SRLIW] = {"SRLIW", "Shift Right Logical Immediate Word. Performs logical right shift of 32-bit rs1 by shamt, sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[SRAIW] = {"SRAIW", "Shift Right Arithmetic Immediate Word. Performs arithmetic right shift of 32-bit rs1 by shamt, sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[ADD] = {"ADD", "Add. Adds the values in rs1 and rs2 and stores the result in rd."};
        arr[SUB] = {"SUB", "Subtract. Subtracts the value in rs2 from rs1 and stores the result in rd."};
        arr[SLL] = {"SLL", "Shift Left Logical. Performs logical left shift of rs1 by the amount in rs2 (lower 5/6 bits) and stores the result in rd."};
        arr[SLT] = {"SLT", "Set if Less Than. Set rd to 1 if rs1 is less than rs2 (signed comparison), otherwise set rd to 0."};
        arr[SLTU] = {"SLTU", "Set if Less Than (Unsigned). Set rd to 1 if rs1 is less than rs2 (unsigned comparison), otherwise set rd to 0."};
        arr[XOR] = {"XOR", "Bitwise XOR. Performs bitwise XOR of rs1 and rs2, and stores the result in rd."};
        arr[SRL] = {"SRL", "Shift Right Logical. Performs logical right shift of rs1 by the amount in rs2 (lower 5/6 bits) and stores the result in rd."};
        arr[SRA] = {"SRA", "Shift Right Arithmetic. Performs arithmetic right shift of rs1 by the amount in rs2 (lower 5/6 bits), preserving the sign, and stores the result in rd."};
        arr[OR] = {"OR", "Bitwise OR. Performs bitwise OR of rs1 and rs2, and stores the result in rd."};
        arr[AND] = {"AND", "Bitwise AND. Performs bitwise AND of rs1 and rs2, and stores the result in rd."};
        arr[ADDW] = {"ADDW", "Add Word. Adds the lower 32 bits of rs1 and rs2, sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[SUBW] = {"SUBW", "Subtract Word. Subtracts the lower 32 bits of rs2 from rs1, sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[SLLW] = {"SLLW", "Shift Left Logical Word. Performs logical left shift of 32-bit rs1 by the shift amount in rs2 (lower 5 bits), sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[SRLW] = {"SRLW", "Shift Right Logical Word. Performs logical right shift of 32-bit rs1 by the shift amount in rs2 (lower 5 bits), sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[SRAW] = {"SRAW", "Shift Right Arithmetic Word. Performs arithmetic right shift of 32-bit rs1 by the shift amount in rs2 (lower 5 bits), sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[FENCE] = {"FENCE", "Fence. Orders memory accesses and instruction fetches across threads."};
        arr[FENCE_I] = {"FENCE.I", "Fence Instruction. Synchronizes the instruction cache with data writes."};
        arr[ECALL] = {"ECALL", "Environment Call. Triggers a system call exception corresponding to the current privilege mode."};
        arr[EBREAK] = {"EBREAK", "Breakpoint. Triggers a breakpoint exception, handing control back to a debugger."};
        arr[CSRRW] = {"CSRRW", "CSR Read/Write. Atomically swaps the value of a CSR with the value in rs1, storing the old CSR value in rd."};
        arr[CSRRS] = {"CSRRS", "CSR Read and Set Bits. Atomically sets bits in a CSR based on the mask in rs1, storing the old CSR value in rd."};
        arr[CSRRC] = {"CSRRC", "CSR Read and Clear Bits. Atomically clears bits in a CSR based on the mask in rs1, storing the old CSR value in rd."};
        arr[CSRRWI] = {"CSRRWI", "CSR Read/Write Immediate. Atomically swaps the value of a CSR with a zero-extended 5-bit immediate, storing the old CSR value in rd."};
        arr[CSRRSI] = {"CSRRSI", "CSR Read and Set Bits Immediate. Atomically sets bits in a CSR based on a zero-extended 5-bit immediate mask, storing the old CSR value in rd."};
        arr[CSRRCI] = {"CSRRCI", "CSR Read and Clear Bits Immediate. Atomically clears bits in a CSR based on a zero-extended 5-bit immediate mask, storing the old CSR value in rd."};
        arr[URET] = {"URET", "User-mode Return. Returns from an exception/interrupt handler in User mode, restoring PC from uepc."};
        arr[SRET] = {"SRET", "Supervisor-mode Return. Returns from an exception/interrupt handler in Supervisor mode, restoring PC from sepc and privilege level from mstatus.spp."};
        arr[MRET] = {"MRET", "Machine-mode Return. Returns from an exception/interrupt handler in Machine mode, restoring PC from mepc and privilege level from mstatus.mpp."};
        arr[WFI] = {"WFI", "Wait For Interrupt. Suspends instruction execution until an interrupt is received, saving power."};
        arr[SFENCE_VMA] = {"SFENCE.VMA", "Supervisor Fence Virtual Memory Address. Flushes the TLB cache, synchronizing page table writes with address translation."};
        arr[MUL] = {"MUL", "Multiply. Multiplies rs1 and rs2 and stores the lower XLEN bits of the result in rd."};
        arr[MULH] = {"MULH", "Multiply High (Signed). Multiplies rs1 and rs2 and stores the upper XLEN bits of the signed product in rd."};
        arr[MULHSU] = {"MULHSU", "Multiply High (Signed/Unsigned). Multiplies signed rs1 and unsigned rs2, storing the upper XLEN bits of the product in rd."};
        arr[MULHU] = {"MULHU", "Multiply High (Unsigned). Multiplies unsigned rs1 and rs2, storing the upper XLEN bits of the product in rd."};
        arr[DIV] = {"DIV", "Divide. Divides rs1 by rs2 (signed division) and stores the quotient in rd."};
        arr[DIVU] = {"DIVU", "Divide (Unsigned). Divides rs1 by rs2 (unsigned division) and stores the quotient in rd."};
        arr[REM] = {"REM", "Remainder. Calculates the remainder of signed division of rs1 by rs2, storing the result in rd."};
        arr[REMU] = {"REMU", "Remainder (Unsigned). Calculates the remainder of unsigned division of rs1 by rs2, storing the result in rd."};
        arr[MULW] = {"MULW", "Multiply Word. Multiplies 32-bit values from rs1 and rs2, sign-extending the lower 32 bits of the product to 64 bits and storing in rd."};
        arr[DIVW] = {"DIVW", "Divide Word. Divides 32-bit signed value from rs1 by 32-bit signed value from rs2, sign-extending the 32-bit quotient to 64 bits and storing in rd."};
        arr[DIVUW] = {"DIVUW", "Divide Word (Unsigned). Divides 32-bit unsigned value from rs1 by 32-bit unsigned value from rs2, sign-extending the 32-bit quotient to 64 bits and storing in rd."};
        arr[REMW] = {"REMW", "Remainder Word. Calculates remainder of 32-bit signed division of rs1 by rs2, sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[REMUW] = {"REMUW", "Remainder Word (Unsigned). Calculates remainder of 32-bit unsigned division of rs1 by rs2, sign-extending the 32-bit result to 64 bits and storing in rd."};
        arr[LR_W] = {"LR.W", "Load-Reserved Word. Loads a word from address rs1 into rd, and registers a reservation on that memory address."};
        arr[SC_W] = {"SC.W", "Store-Conditional Word. Conditionally writes a word from rs2 to address rs1 if a reservation on that address is active, storing 0 in rd on success, or non-zero on failure."};
        arr[AMOSWAP_W] = {"AMOSWAP.W", "Atomic Swap Word. Atomically loads a word from address rs1 into rd, and stores rs2 to address rs1."};
        arr[AMOADD_W] = {"AMOADD.W", "Atomic Add Word. Atomically loads a word from address rs1 into rd, adds rs2 to it, and stores the result back to address rs1."};
        arr[AMOXOR_W] = {"AMOXOR.W", "Atomic XOR Word. Atomically loads a word from address rs1 into rd, XORs rs2 with it, and stores the result back to address rs1."};
        arr[AMOAND_W] = {"AMOAND.W", "Atomic AND Word. Atomically loads a word from address rs1 into rd, ANDs rs2 with it, and stores the result back to address rs1."};
        arr[AMOOR_W] = {"AMOOR.W", "Atomic OR Word. Atomically loads a word from address rs1 into rd, ORs rs2 with it, and stores the result back to address rs1."};
        arr[AMOMIN_W] = {"AMOMIN.W", "Atomic Min Word. Atomically loads a word from address rs1 into rd, calculates the signed min with rs2, and stores the result back to address rs1."};
        arr[AMOMAX_W] = {"AMOMAX.W", "Atomic Max Word. Atomically loads a word from address rs1 into rd, calculates the signed max with rs2, and stores the result back to address rs1."};
        arr[AMOMINU_W] = {"AMOMINU.W", "Atomic Min Word (Unsigned). Atomically loads a word from address rs1 into rd, calculates the unsigned min with rs2, and stores the result back to address rs1."};
        arr[AMOMAXU_W] = {"AMOMAXU.W", "Atomic Max Word (Unsigned). Atomically loads a word from address rs1 into rd, calculates the unsigned max with rs2, and stores the result back to address rs1."};
        arr[FLW] = {"FLW", "Floating-Point Load Word. Loads a 32-bit floating-point value from memory address rs1 + immediate into floating-point register rd."};
        arr[FSW] = {"FSW", "Floating-Point Store Word. Stores a 32-bit floating-point value from floating-point register rs2 to memory address rs1 + immediate."};
        arr[FLD] = {"FLD", "Floating-Point Load Double. Loads a 64-bit floating-point value from memory address rs1 + immediate into floating-point register rd."};
        arr[FSD] = {"FSD", "Floating-Point Store Double. Stores a 64-bit floating-point value from floating-point register rs2 to memory address rs1 + immediate."};

        return arr;
    }();

    auto const idx = static_cast<size_t>(op_id);
    if (idx < kOpDescriptions.size()) {
        return kOpDescriptions[idx];
    }
    return kDefaultDesc;
}

auto c_code(std::string_view ansi_code, bool use_color) -> std::string_view {
    return use_color ? ansi_code : "";
}

auto print_r_format(uint32_t funct7_val, uint32_t rs2_val, uint32_t rs1_val, uint32_t funct3_val, uint32_t rd_val, simrv::pipeline::Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (R-Type format):");
    std::println("  31          25 24      20 19      15 14  12 11        7 6           0");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::println("  |   funct7   |   rs2    |   rs1    | f3 |    rd    |   opcode    |");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::print("  |   {}{:07b}{}  |  {}{:05b}{}   |  {}{:05b}{}   | {}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
               c(kBrightRed), funct7_val, c(kReset),
               c(kBrightMagenta), rs2_val, c(kReset),
               c(kBrightYellow), rs1_val, c(kReset),
               c(kBrightCyan), funct3_val, c(kReset),
               c(kBrightGreen), rd_val, c(kReset),
               c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +------------+----------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue), std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}x{:<2}{} ({:05b}) -> Destination Register: {}", c(kBrightGreen), rd_val, c(kBrightGreen), rd_val, reg_name(rd_val), c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> Sub-function selector", c(kBrightCyan), funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}x{:<2}{} ({:05b}) -> Source Register 1: {}", c(kBrightYellow), rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val), c(kReset));
    std::println("  rs2     : {}x{:<2}{} ({:05b}) -> Source Register 2: {}", c(kBrightMagenta), rs2_val, c(kBrightMagenta), rs2_val, reg_name(rs2_val), c(kReset));
    std::println("  funct7  : {}0x{:02X}{} ({:07b}) -> Operations modifier", c(kBrightRed), funct7_val, c(kBrightRed), funct7_val, c(kReset));
}

auto print_i_format(uint32_t imm_bits, int32_t imm_val, uint32_t rs1_val, uint32_t funct3_val, uint32_t rd_val, simrv::pipeline::Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (I-Type format):");
    std::println("  31                20 19      15 14  12 11        7 6           0");
    std::println("  +----------------------+----------+----+----------+-------------+");
    std::println("  |      immediate       |   rs1    | f3 |    rd    |   opcode    |");
    std::println("  +----------------------+----------+----+----------+-------------+");
    std::print("  |     {}{:012b}{}     |  {}{:05b}{}   | {}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
               c(kBrightRed), imm_bits, c(kReset),
               c(kBrightYellow), rs1_val, c(kReset),
               c(kBrightCyan), funct3_val, c(kReset),
               c(kBrightGreen), rd_val, c(kReset),
               c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +----------------------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue), std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}x{:<2}{} ({:05b}) -> Destination Register: {}", c(kBrightGreen), rd_val, c(kBrightGreen), rd_val, reg_name(rd_val), c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> Sub-function selector", c(kBrightCyan), funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}x{:<2}{} ({:05b}) -> Source Register 1: {}", c(kBrightYellow), rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val), c(kReset));
    std::println("  imm     : {}0x{:03X}{}  ({:012b}) -> Sign-extended 12-bit Immediate", c(kBrightRed), imm_bits, c(kBrightRed), imm_bits, c(kReset));

    std::println("\nImmediate Reconstruction:");
    std::println("  imm[11:0] = inst[31:20] = {:012b}", imm_bits);
    std::println("  Sign-extended to 32 bits: {}{} (0x{:X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
}

auto print_s_format(uint32_t imm_hi, uint32_t imm_lo, int32_t imm_val, uint32_t rs1_val, uint32_t rs2_val, uint32_t funct3_val, simrv::pipeline::Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (S-Type format):");
    std::println("  31          25 24      20 19      15 14  12 11        7 6           0");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::println("  |  imm[11:5] |   rs2    |   rs1    | f3 | imm[4:0] |   opcode    |");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::print("  |   {}{:07b}{}  |  {}{:05b}{}   |  {}{:05b}{}   | {}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
               c(kBrightRed), imm_hi, c(kReset),
               c(kBrightMagenta), rs2_val, c(kReset),
               c(kBrightYellow), rs1_val, c(kReset),
               c(kBrightCyan), funct3_val, c(kReset),
               c(kBrightRed), imm_lo, c(kReset),
               c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +------------+----------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue), std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  imm_lo  : {}0x{:02X}{}   ({:05b}) -> Lower bits of immediate", c(kBrightRed), imm_lo, c(kBrightRed), imm_lo, c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> Sub-function selector", c(kBrightCyan), funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}x{:<2}{} ({:05b}) -> Base Address Register: {}", c(kBrightYellow), rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val), c(kReset));
    std::println("  rs2     : {}x{:<2}{} ({:05b}) -> Source Register (Value to store): {}", c(kBrightMagenta), rs2_val, c(kBrightMagenta), rs2_val, reg_name(rs2_val), c(kReset));
    std::println("  imm_hi  : {}0x{:02X}{}   ({:07b}) -> Upper bits of immediate", c(kBrightRed), imm_hi, c(kBrightRed), imm_hi, c(kReset));

    std::println("\nImmediate Reconstruction:");
    std::println("  imm[11:5] = inst[31:25] = {:07b}", imm_hi);
    std::println("  imm[4:0]  = inst[11:7]  = {:05b}", imm_lo);
    std::println("  Combined  = {:012b}", (imm_hi << 5) | imm_lo);
    std::println("  Sign-extended to 32 bits: {}{} (0x{:X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
}

auto print_b_format(uint32_t inst, uint32_t imm_hi, uint32_t imm_lo, int32_t imm_val, uint32_t rs1_val, uint32_t rs2_val, uint32_t funct3_val, simrv::pipeline::Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (B-Type format):");
    std::println("  31          25 24      20 19      15 14  12 11        7 6           0");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::println("  |imm[12|10:5]|   rs2    |   rs1    | f3 |imm[4:1|11]|   opcode    |");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::print("  |   {}{:07b}{}  |  {}{:05b}{}   |  {}{:05b}{}   | {}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
               c(kBrightRed), imm_hi, c(kReset),
               c(kBrightMagenta), rs2_val, c(kReset),
               c(kBrightYellow), rs1_val, c(kReset),
               c(kBrightCyan), funct3_val, c(kReset),
               c(kBrightRed), imm_lo, c(kReset),
               c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +------------+----------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue), std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  imm_lo  : {}0x{:02X}{}   ({:05b}) -> Branch offset bits [4:1, 11]", c(kBrightRed), imm_lo, c(kBrightRed), imm_lo, c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> Branch condition selector", c(kBrightCyan), funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}x{:<2}{} ({:05b}) -> Source Register 1: {}", c(kBrightYellow), rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val), c(kReset));
    std::println("  rs2     : {}x{:<2}{} ({:05b}) -> Source Register 2: {}", c(kBrightMagenta), rs2_val, c(kBrightMagenta), rs2_val, reg_name(rs2_val), c(kReset));
    std::println("  imm_hi  : {}0x{:02X}{}   ({:07b}) -> Branch offset bits [12, 10:5]", c(kBrightRed), imm_hi, c(kBrightRed), imm_hi, c(kReset));

    std::println("\nImmediate Reconstruction:");
    uint32_t const b31 = (inst >> 31) & 1;
    uint32_t const b7 = (inst >> 7) & 1;
    uint32_t const b30_25 = (inst >> 25) & 0x3F;
    uint32_t const b11_8 = (inst >> 8) & 0xF;
    std::println("  imm[12]   = inst[31]    = {}", b31);
    std::println("  imm[11]   = inst[7]     = {}", b7);
    std::println("  imm[10:5] = inst[30:25] = {:06b}", b30_25);
    std::println("  imm[4:1]  = inst[11:8]  = {:04b}", b11_8);
    std::println("  imm[0]    = 0           = 0 (Implicitly zero for 2-byte alignment)");
    std::println("  Combined  = {:013b}", (b31 << 12) | (b7 << 11) | (b30_25 << 5) | (b11_8 << 1));
    std::println("  Sign-extended to 32 bits: {}{} bytes (0x{:X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
}

auto print_u_format(uint32_t imm_bits, int32_t imm_val, uint32_t rd_val, simrv::pipeline::Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (U-Type format):");
    std::println("  31                                12 11        7 6           0");
    std::println("  +--------------------------------------+----------+-------------+");
    std::println("  |              immediate               |    rd    |   opcode    |");
    std::println("  +--------------------------------------+----------+-------------+");
    std::print("  |         {}{:020b}{}       |  {}{:05b}{}   |   {}{:07b}{}   |\n",
               c(kBrightRed), imm_bits, c(kReset),
               c(kBrightGreen), rd_val, c(kReset),
               c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +--------------------------------------+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue), std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}x{:<2}{} ({:05b}) -> Destination Register: {}", c(kBrightGreen), rd_val, c(kBrightGreen), rd_val, reg_name(rd_val), c(kReset));
    std::println("  imm     : {}0x{:05X}{} ({:020b}) -> Upper 20-bit Immediate", c(kBrightRed), imm_bits, c(kBrightRed), imm_bits, c(kReset));

    std::println("\nImmediate Reconstruction:");
    std::println("  imm[31:12] = inst[31:12] = {:020b}", imm_bits);
    std::println("  imm[11:0]  = 0            = 000000000000");
    std::println("  Combined Value           : {}{} (0x{:X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
}

auto print_j_format(uint32_t inst, uint32_t imm_bits, int32_t imm_val, uint32_t rd_val, simrv::pipeline::Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (J-Type format):");
    std::println("  31                                12 11        7 6           0");
    std::println("  +--------------------------------------+----------+-------------+");
    std::println("  |              immediate               |    rd    |   opcode    |");
    std::println("  +--------------------------------------+----------+-------------+");
    std::print("  |         {}{:020b}{}       |  {}{:05b}{}   |   {}{:07b}{}   |\n",
               c(kBrightRed), imm_bits, c(kReset),
               c(kBrightGreen), rd_val, c(kReset),
               c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +--------------------------------------+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue), std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}x{:<2}{} ({:05b}) -> Destination Register: {}", c(kBrightGreen), rd_val, c(kBrightGreen), rd_val, reg_name(rd_val), c(kReset));
    std::println("  imm_bits: {}0x{:05X}{} ({:020b}) -> Raw scrambled immediate bits", c(kBrightRed), imm_bits, c(kBrightRed), imm_bits, c(kReset));

    std::println("\nImmediate Reconstruction:");
    uint32_t const j31 = (inst >> 31) & 1;
    uint32_t const j19_12 = (inst >> 12) & 0xFF;
    uint32_t const j20 = (inst >> 20) & 1;
    uint32_t const j30_21 = (inst >> 21) & 0x3FF;
    std::println("  imm[20]    = inst[31]     = {}", j31);
    std::println("  imm[19:12] = inst[19:12]  = {:08b}", j19_12);
    std::println("  imm[11]    = inst[20]     = {}", j20);
    std::println("  imm[10:1]  = inst[30:21]  = {:010b}", j30_21);
    std::println("  imm[0]     = 0            = 0 (Implicitly zero for 2-byte alignment)");
    std::println("  Combined   = {:021b}", (j31 << 20) | (j19_12 << 12) | (j20 << 11) | (j30_21 << 1));
    std::println("  Sign-extended to 32 bits: {}{} bytes (0x{:X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
}

auto print_r4_format(uint32_t funct7_val, uint32_t rs3_val, uint32_t rs2_val, uint32_t rs1_val, uint32_t funct3_val, uint32_t rd_val, simrv::pipeline::Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (R4-Type format):");
    std::println("  31    27 26 25 24      20 19      15 14  12 11        7 6           0");
    std::println("  +-----+----+--+----------+----------+----+----------+-------------+");
    std::println("  | rs3 |fmt |..|   rs2    |   rs1    | f3 |    rd    |   opcode    |");
    std::println("  +-----+----+--+----------+----------+----+----------+-------------+");
    std::print("  | {}{:05b}{} | {:02b} |00|  {}{:05b}{}   |  {}{:05b}{}   | {}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
               c(kBrightWhite), rs3_val, c(kReset),
               funct7_val & 3,
               c(kBrightMagenta), rs2_val, c(kReset),
               c(kBrightYellow), rs1_val, c(kReset),
               c(kBrightCyan), funct3_val, c(kReset),
               c(kBrightGreen), rd_val, c(kReset),
               c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  +-----+----+--+----------+----------+----+----------+-------------+");

    std::println("\nField Decoded Meanings:");
    std::println("  opcode  : {}0x{:02X}{} ({:07b}) -> Major Opcode", c(kBrightBlue), std::to_underlying(op), c(kBrightBlue), std::to_underlying(op), c(kReset));
    std::println("  rd      : {}f{:<2}{} ({:05b}) -> FP Destination Register: {}", c(kBrightGreen), rd_val, c(kBrightGreen), rd_val, reg_name(rd_val, true), c(kReset));
    std::println("  funct3  : {}0x{:01X}{}  ({:03b}) -> FP rounding mode or sub-function", c(kBrightCyan), funct3_val, c(kBrightCyan), funct3_val, c(kReset));
    std::println("  rs1     : {}f{:<2}{} ({:05b}) -> FP Source Register 1: {}", c(kBrightYellow), rs1_val, c(kBrightYellow), rs1_val, reg_name(rs1_val, true), c(kReset));
    std::println("  rs2     : {}f{:<2}{} ({:05b}) -> FP Source Register 2: {}", c(kBrightMagenta), rs2_val, c(kBrightMagenta), rs2_val, reg_name(rs2_val, true), c(kReset));
    std::println("  rs3     : {}f{:<2}{} ({:05b}) -> FP Source Register 3: {}", c(kBrightWhite), rs3_val, c(kBrightWhite), rs3_val, reg_name(rs3_val, true), c(kReset));
}

auto format_r_type(::OperationId op_id, std::string_view mnemonic, uint32_t rd_val, uint32_t rs1_val, uint32_t rs2_val, bool is_fp_sys) -> std::string {
    bool const is_dst_fp = (op_id >= FLW && op_id <= FSW) ||
                          (op_id >= FADD_S && op_id <= FMAX_S) ||
                          (op_id >= FCVT_S_W && op_id <= FMV_W_X) ||
                          (op_id >= FLD && op_id <= FSD) ||
                          (op_id >= FADD_D && op_id <= FMAX_D) ||
                          (op_id >= FCVT_D_W && op_id <= FMV_D_X);

    bool const is_src_fp = (op_id >= FADD_S && op_id <= FCVT_W_S) ||
                          (op_id >= FEQ_S && op_id <= FCLASS_S) ||
                          (op_id >= FADD_D && op_id <= FCVT_W_D) ||
                          (op_id >= FEQ_D && op_id <= FCLASS_D);

    std::string const rd_str = ABI_NAMES[rd_val];
    std::string const rs1_str = ABI_NAMES[rs1_val];
    std::string const rs2_str = ABI_NAMES[rs2_val];
    std::string const frd_str = FP_ABI_NAMES[rd_val];
    std::string const frs1_str = FP_ABI_NAMES[rs1_val];
    std::string const frs2_str = FP_ABI_NAMES[rs2_val];

    if (op_id == SFENCE_VMA) {
        return std::format("sfence.vma {}, {}", rs1_str, rs2_str);
    } else if (op_id >= LR_W && op_id <= SC_W) {
        if (op_id == LR_W) {
            return std::format("lr.w {}, ({})", rd_str, rs1_str);
        } else {
            return std::format("sc.w {}, {}, ({})", rd_str, rs2_str, rs1_str);
        }
    } else if (op_id >= AMOSWAP_W && op_id <= AMOMAXU_W) {
        return std::format("{}.aqrl {}, {}, ({})", mnemonic, rd_str, rs2_str, rs1_str);
    } else if (is_fp_sys) {
        // Conversions and Moves
        if ((op_id == FMV_X_W || op_id == FMV_X_D) ||
            (op_id == FCLASS_S || op_id == FCLASS_D) ||
            (op_id >= FCVT_W_S && op_id <= FCVT_LU_S) ||
            (op_id >= FCVT_W_D && op_id <= FCVT_LU_D)) {
            return std::format("{} {}, {}", mnemonic, rd_str, frs1_str);
        } else if ((op_id == FMV_W_X || op_id == FMV_D_X) ||
                   (op_id >= FCVT_S_W && op_id <= FCVT_S_LU) ||
                   (op_id >= FCVT_D_W && op_id <= FCVT_D_LU)) {
            return std::format("{} {}, {}", mnemonic, frd_str, rs1_str);
        }
    } else if (is_dst_fp || is_src_fp) {
        if (op_id == FSQRT_S || op_id == FSQRT_D) {
            return std::format("{} {}, {}", mnemonic, frd_str, frs1_str);
        } else if (op_id == FEQ_S || op_id == FLT_S || op_id == FLE_S ||
                   op_id == FEQ_D || op_id == FLT_D || op_id == FLE_D) {
            return std::format("{} {}, {}, {}", mnemonic, rd_str, frs1_str, frs2_str);
        } else {
            return std::format("{} {}, {}, {}", mnemonic, frd_str, frs1_str, frs2_str);
        }
    }
    return std::format("{} {}, {}, {}", mnemonic, rd_str, rs1_str, rs2_str);
}

auto format_i_type(::OperationId op_id, std::string_view mnemonic, uint32_t rd_val, uint32_t rs1_val, int32_t imm_val, uint32_t csr_val, simrv::pipeline::Opcode op, bool is_load, bool is_csr) -> std::string {
    std::string const rd_str = ABI_NAMES[rd_val];
    std::string const rs1_str = ABI_NAMES[rs1_val];
    std::string const frd_str = FP_ABI_NAMES[rd_val];

    if (is_load) {
        if (op == simrv::pipeline::Opcode::kLoadFp) {
            return std::format("{} {}, {}({})", mnemonic, frd_str, imm_val, rs1_str);
        } else {
            return std::format("{} {}, {}({})", mnemonic, rd_str, imm_val, rs1_str);
        }
    } else if (op_id == JALR) {
        return std::format("jalr {}, {}({})", rd_str, imm_val, rs1_str);
    } else if (is_csr) {
        std::string const csr_str = csr_name(csr_val);
        if (op_id == CSRRWI || op_id == CSRRSI || op_id == CSRRCI) {
            return std::format("{} {}, {}, {}", mnemonic, rd_str, csr_str, rs1_val); // rs1 field acts as uimm
        } else {
            return std::format("{} {}, {}, {}", mnemonic, rd_str, csr_str, rs1_str);
        }
    } else if (op_id == ECALL || op_id == EBREAK) {
        return std::string(mnemonic);
    } else if (op_id == FENCE) {
        return "fence";
    } else if (op_id == FENCE_I) {
        return "fence.i";
    } else if (op_id == SLLI || op_id == SRLI || op_id == SRAI ||
               op_id == SLLIW || op_id == SRLIW || op_id == SRAIW) {
        uint32_t const shamt = imm_val & 0x3F;
        return std::format("{} {}, {}, {}", mnemonic, rd_str, rs1_str, shamt);
    }
    return std::format("{} {}, {}, {}", mnemonic, rd_str, rs1_str, imm_val);
}

auto format_s_type(std::string_view mnemonic, uint32_t rs1_val, uint32_t rs2_val, int32_t imm_val, simrv::pipeline::Opcode op) -> std::string {
    std::string const rs1_str = ABI_NAMES[rs1_val];
    std::string const rs2_str = ABI_NAMES[rs2_val];
    std::string const frs2_str = FP_ABI_NAMES[rs2_val];

    if (op == simrv::pipeline::Opcode::kStoreFp) {
        return std::format("{} {}, {}({})", mnemonic, frs2_str, imm_val, rs1_str);
    } else {
        return std::format("{} {}, {}({})", mnemonic, rs2_str, imm_val, rs1_str);
    }
}

auto format_b_type(std::string_view mnemonic, uint32_t rs1_val, uint32_t rs2_val, int32_t imm_val) -> std::string {
    std::string const rs1_str = ABI_NAMES[rs1_val];
    std::string const rs2_str = ABI_NAMES[rs2_val];
    return std::format("{} {}, {}, {}", mnemonic, rs1_str, rs2_str, imm_val);
}

auto format_u_type(std::string_view mnemonic, uint32_t rd_val, int32_t imm_val) -> std::string {
    std::string const rd_str = ABI_NAMES[rd_val];
    return std::format("{} {}, 0x{:X}", mnemonic, rd_str, static_cast<uint32_t>(imm_val));
}

auto format_j_type(uint32_t rd_val, int32_t imm_val) -> std::string {
    std::string const rd_str = ABI_NAMES[rd_val];
    return std::format("jal {}, {}", rd_str, imm_val);
}

auto format_r4_type(std::string_view mnemonic, uint32_t rd_val, uint32_t rs1_val, uint32_t rs2_val, uint32_t rs3_val) -> std::string {
    std::string const frd_str = FP_ABI_NAMES[rd_val];
    std::string const frs1_str = FP_ABI_NAMES[rs1_val];
    std::string const frs2_str = FP_ABI_NAMES[rs2_val];
    std::string const frs3_str = FP_ABI_NAMES[rs3_val];
    return std::format("{} {}, {}, {}, {}", mnemonic, frd_str, frs1_str, frs2_str, frs3_str);
}

} // namespace

void explain_instruction(uint32_t raw_inst) {
    bool const use_color = simrv::util::is_terminal(STDOUT_FILENO);
    auto c = [use_color](std::string_view ansi_code) -> std::string_view {
        return use_color ? ansi_code : "";
    };

    std::println("\n{}=== SimRV Educational Instruction Explainer ==={}\n", c(kBoldFgBrightBlue), c(kReset));

    uint32_t inst = raw_inst;
    bool const is_compressed = (raw_inst & 0x3) != 0x3;

    if (is_compressed) {
        uint32_t const raw_16 = raw_inst & 0xFFFF;
        std::println("Compressed Instruction Detected (16-bit):");
        std::println("  Hex Value: {}0x{:04X}{}", c(kBrightYellow), raw_16, c(kReset));
        std::println("  Binary   : {}{:016b}{}", c(kBrightYellow), raw_16, c(kReset));

        inst = simrv::pipeline::decompressInstruction(raw_16, simrv::xlen::kIsXLen64);
        std::println("\nDecompression Translation Step:");
        std::println("  16-bit compressed instruction translated to 32-bit canonical instruction:");
        std::println("  Hex Value: {}0x{:08X}{}", c(kBrightGreen), inst, c(kReset));
        std::println("  Binary   : {}{:032b}{}", c(kBrightGreen), inst, c(kReset));
        std::println("--------------------------------------------------------------------------------");
    } else {
        std::println("Standard 32-bit Instruction Word:");
        std::println("  Hex Value: {}0x{:08X}{}", c(kBrightGreen), inst, c(kReset));
        std::println("  Binary   : {}{:032b}{}", c(kBrightGreen), inst, c(kReset));
        std::println("--------------------------------------------------------------------------------");
    }

    // Extract core fields
    auto const op = static_cast<simrv::pipeline::Opcode>(inst & 0x7F);
    uint32_t const rd_val = (inst >> 7) & 0x1F;
    uint32_t const funct3_val = (inst >> 12) & 0x7;
    uint32_t const rs1_val = (inst >> 15) & 0x1F;
    uint32_t const rs2_val = (inst >> 20) & 0x1F;
    uint32_t const funct7_val = (inst >> 25) & 0x7F;
    uint32_t const rs3_val = (inst >> 27) & 0x1F;
    uint32_t const csr_val = (inst >> 20) & 0xFFF;

    InstFormat const fmt = get_format(op);
    std::println("Instruction Format: {}{}{}", c(kBold), get_format_name(fmt), c(kReset));

    // Print Visual Fields
    std::println("");
    if (fmt == InstFormat::R) {
        print_r_format(funct7_val, rs2_val, rs1_val, funct3_val, rd_val, op, use_color);
    } else if (fmt == InstFormat::I) {
        uint32_t const imm_bits = (inst >> 20) & 0xFFF;
        int32_t const imm_val = static_cast<int32_t>(inst) >> 20;
        print_i_format(imm_bits, imm_val, rs1_val, funct3_val, rd_val, op, use_color);
    } else if (fmt == InstFormat::S) {
        uint32_t const imm_hi = (inst >> 25) & 0x7F;
        uint32_t const imm_lo = (inst >> 7) & 0x1F;
        int32_t const imm_val = (static_cast<int32_t>(inst & 0xFE000000) >> 20) | ((inst >> 7) & 0x1F);
        print_s_format(imm_hi, imm_lo, imm_val, rs1_val, rs2_val, funct3_val, op, use_color);
    } else if (fmt == InstFormat::B) {
        uint32_t const imm_hi = (inst >> 25) & 0x7F;
        uint32_t const imm_lo = (inst >> 7) & 0x1F;
        int32_t const imm_val = (static_cast<int32_t>(inst & 0x80000000) >> 19) | ((inst & 0x7E000000) >> 20) |
                               ((inst & 0x00000F00) >> 7) | ((inst & 0x00000080) << 4);
        print_b_format(inst, imm_hi, imm_lo, imm_val, rs1_val, rs2_val, funct3_val, op, use_color);
    } else if (fmt == InstFormat::U) {
        uint32_t const imm_bits = (inst >> 12) & 0xFFFFF;
        auto const imm_val = static_cast<int32_t>(inst & 0xFFFFF000);
        print_u_format(imm_bits, imm_val, rd_val, op, use_color);
    } else if (fmt == InstFormat::J) {
        uint32_t const imm_bits = ((inst >> 12) & 0xFFFFF);
        int32_t const imm_val = (static_cast<int32_t>(inst & 0x80000000) >> 11) | (inst & 0x000FF000) |
                               ((inst & 0x00100000) >> 9) | ((inst & 0x7FE00000) >> 20);
        print_j_format(inst, imm_bits, imm_val, rd_val, op, use_color);
    } else if (fmt == InstFormat::R4) {
        print_r4_format(funct7_val, rs3_val, rs2_val, rs1_val, funct3_val, rd_val, op, use_color);
    } else {
        std::println("Visual Bit Fields Breakdown: Format unrecognized.");
    }

    // Decode Operation ID
    std::println("--------------------------------------------------------------------------------");
    ::OperationId const op_id = simrv::pipeline::decoder(inst);
    auto const [mnemonic, desc] = get_description(op_id);

    std::println("{}Decoded Instruction Detail:{}", c(kBold), c(kReset));
    if (op_id != UNKNOWN) {
        std::println("  Assembly Mnemonic: {}{}{}", c(kBoldFgBrightGreen), mnemonic, c(kReset));

        // Format Assembly Representation
        std::string assembly;
        bool const is_load = (op == simrv::pipeline::Opcode::kLoad) || (op == simrv::pipeline::Opcode::kLoadFp);
        bool const is_csr = (op_id >= CSRRW) && (op_id <= CSRRCI);
        bool const is_fp_sys = (op_id >= FCVT_W_S && op_id <= FMV_W_X) ||
                              (op_id >= FCVT_W_D && op_id <= FMV_D_X);

        if (fmt == InstFormat::R) {
            assembly = format_r_type(op_id, mnemonic, rd_val, rs1_val, rs2_val, is_fp_sys);
        } else if (fmt == InstFormat::I) {
            int32_t const imm_val = static_cast<int32_t>(inst) >> 20;
            assembly = format_i_type(op_id, mnemonic, rd_val, rs1_val, imm_val, csr_val, op, is_load, is_csr);
        } else if (fmt == InstFormat::S) {
            int32_t const imm_val = (static_cast<int32_t>(inst & 0x80000000) >> 20) | ((inst >> 7) & 0x1F) | ((inst >> 20) & 0x7E0);
            assembly = format_s_type(mnemonic, rs1_val, rs2_val, imm_val, op);
        } else if (fmt == InstFormat::B) {
            int32_t const imm_val = (static_cast<int32_t>(inst & 0x80000000) >> 19) | ((inst & 0x7E000000) >> 20) |
                                   ((inst & 0x00000F00) >> 7) | ((inst & 0x00000080) << 4);
            assembly = format_b_type(mnemonic, rs1_val, rs2_val, imm_val);
        } else if (fmt == InstFormat::U) {
            auto const imm_val = static_cast<int32_t>(inst & 0xFFFFF000);
            assembly = format_u_type(mnemonic, rd_val, imm_val);
        } else if (fmt == InstFormat::J) {
            int32_t const imm_val = (static_cast<int32_t>(inst & 0x80000000) >> 11) | (inst & 0x000FF000) |
                                   ((inst & 0x00100000) >> 9) | ((inst & 0x7FE00000) >> 20);
            assembly = format_j_type(rd_val, imm_val);
        } else if (fmt == InstFormat::R4) {
            assembly = format_r4_type(mnemonic, rd_val, rs1_val, rs2_val, rs3_val);
        }

        std::println("  Assembly Rep     : {}# {}{}", c(kBrightBlack), assembly, c(kReset));
        std::println("\nDescription (Behavior):");
        std::println("  [ISA: {}] {}", get_isa_extension_name(op_id), desc);

    } else {
        std::println("  Mnemonic         : {}UNKNOWN / RESERVED{}", c(kBoldFgRed), c(kReset));
        std::println("  This opcode is either reserved, or part of an unsupported custom extension.");
    }
    std::println("\n{}================================================={}\n", c(kBoldFgBrightBlue), c(kReset));
}

std::pair<std::string_view, std::string_view> get_operation_details(::OperationId op_id) {
    return get_description(op_id);
}

std::string_view get_isa_extension_name(::OperationId op_id) {
    if (op_id >= ::LUI && op_id <= ::CSRRCI) {
        if (op_id == ::LWU || op_id == ::LD || op_id == ::SD ||
            op_id == ::ADDIW || op_id == ::SLLIW || op_id == ::SRLIW || op_id == ::SRAIW ||
            op_id == ::ADDW || op_id == ::SUBW || op_id == ::SLLW || op_id == ::SRLW || op_id == ::SRAW) {
            return "RV64I";
        }
        return "RV32I";
    }
    if (op_id >= ::URET && op_id <= ::SFENCE_VMA) {
        return "Privileged";
    }
    if (op_id >= ::MUL && op_id <= ::REMUW) {
        if (op_id == ::MULW || op_id == ::DIVW || op_id == ::DIVUW || op_id == ::REMW || op_id == ::REMUW) {
            return "RV64M";
        }
        return "RV32M";
    }
    if (op_id >= ::LR_W && op_id <= ::AMOMAXU_W) {
        return "RV32A";
    }
    if (op_id >= ::FLW && op_id <= ::FCVT_S_LU) {
        return "RV32F";
    }
    if (op_id >= ::FLD && op_id <= ::FCVT_D_LU) {
        return "RV32D";
    }
    return "Unknown";
}

} // namespace simrv::util
