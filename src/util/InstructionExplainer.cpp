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
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;
using simrv::isa::kOperationIdCount;
using simrv::isa::Opcode;
using simrv::isa::InstFormat;
using simrv::core::Csr;
using enum simrv::core::Csr;

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



auto get_description(OperationId op_id) -> std::pair<std::string_view, std::string_view> {
    static constexpr std::pair<std::string_view, std::string_view> kDefaultDesc = {
        "UNKNOWN", "Instruction not explicitly detailed or custom extension opcode. Verify against compiler specification."
    };

    static const auto kOpDescriptions = []() -> std::array<std::pair<std::string_view, std::string_view>, kOperationIdCount> {
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
        arr[LR_D] = {"LR.D", "Load-Reserved Doubleword. Loads a doubleword from address rs1 into rd, and registers a reservation on that memory address."};
        arr[SC_D] = {"SC.D", "Store-Conditional Doubleword. Conditionally writes a doubleword from rs2 to address rs1 if a reservation on that address is active, storing 0 in rd on success, or non-zero on failure."};
        arr[AMOSWAP_D] = {"AMOSWAP.D", "Atomic Swap Doubleword. Atomically loads a doubleword from address rs1 into rd, and stores rs2 to address rs1."};
        arr[AMOADD_D] = {"AMOADD.D", "Atomic Add Doubleword. Atomically loads a doubleword from address rs1 into rd, adds rs2 to it, and stores the result back to address rs1."};
        arr[AMOXOR_D] = {"AMOXOR.D", "Atomic XOR Doubleword. Atomically loads a doubleword from address rs1 into rd, XORs rs2 with it, and stores the result back to address rs1."};
        arr[AMOAND_D] = {"AMOAND.D", "Atomic AND Doubleword. Atomically loads a doubleword from address rs1 into rd, ANDs rs2 with it, and stores the result back to address rs1."};
        arr[AMOOR_D] = {"AMOOR.D", "Atomic OR Doubleword. Atomically loads a doubleword from address rs1 into rd, ORs rs2 with it, and stores the result back to address rs1."};
        arr[AMOMIN_D] = {"AMOMIN.D", "Atomic Min Doubleword. Atomically loads a doubleword from address rs1 into rd, calculates the signed min with rs2, and stores the result back to address rs1."};
        arr[AMOMAX_D] = {"AMOMAX.D", "Atomic Max Doubleword. Atomically loads a doubleword from address rs1 into rd, calculates the signed max with rs2, and stores the result back to address rs1."};
        arr[AMOMINU_D] = {"AMOMINU.D", "Atomic Min Doubleword (Unsigned). Atomically loads a doubleword from address rs1 into rd, calculates the unsigned min with rs2, and stores the result back to address rs1."};
        arr[AMOMAXU_D] = {"AMOMAXU.D", "Atomic Max Doubleword (Unsigned). Atomically loads a doubleword from address rs1 into rd, calculates the unsigned max with rs2, and stores the result back to address rs1."};
        arr[FLW] = {"FLW", "Floating-Point Load Word. Loads a 32-bit floating-point value from memory address rs1 + immediate into floating-point register rd."};
        arr[FSW] = {"FSW", "Floating-Point Store Word. Stores a 32-bit floating-point value from floating-point register rs2 to memory address rs1 + immediate."};
        arr[FSD] = {"FSD", "Floating-Point Store Double. Stores a 64-bit floating-point value from floating-point register rs2 to memory address rs1 + immediate."};

        // B-Extension instructions (Zba, Zbb, Zbc, Zbs)
        arr[SH1ADD] = {"SH1ADD", "Shift Left by 1 and Add. Computes rs2 + (rs1 << 1)."};
        arr[SH2ADD] = {"SH2ADD", "Shift Left by 2 and Add. Computes rs2 + (rs1 << 2)."};
        arr[SH3ADD] = {"SH3ADD", "Shift Left by 3 and Add. Computes rs2 + (rs1 << 3)."};
        arr[ANDN] = {"ANDN", "AND with inverted operand. Computes rs1 & ~rs2."};
        arr[ORN] = {"ORN", "OR with inverted operand. Computes rs1 | ~rs2."};
        arr[XNOR] = {"XNOR", "Exclusive NOR. Computes ~(rs1 ^ rs2)."};
        arr[CLZ] = {"CLZ", "Count Leading Zeros. Returns the number of leading zero bits starting from the MSB."};
        arr[CTZ] = {"CTZ", "Count Trailing Zeros. Returns the number of trailing zero bits starting from the LSB."};
        arr[CPOP] = {"CPOP", "Count Population. Returns the number of set (1) bits."};
        arr[MIN] = {"MIN", "Minimum. Returns the smaller of two signed integers."};
        arr[MAX] = {"MAX", "Maximum. Returns the larger of two signed integers."};
        arr[MINU] = {"MINU", "Minimum Unsigned. Returns the smaller of two unsigned integers."};
        arr[MAXU] = {"MAXU", "Maximum Unsigned. Returns the larger of two unsigned integers."};
        arr[SEXT_B] = {"SEXT_B", "Sign-extend Byte. Sign-extends the lower 8 bits of rs1 to XLEN."};
        arr[SEXT_H] = {"SEXT_H", "Sign-extend Halfword. Sign-extends the lower 16 bits of rs1 to XLEN."};
        arr[ZEXT_H] = {"ZEXT_H", "Zero-extend Halfword. Zero-extends the lower 16 bits of rs1 to XLEN."};
        arr[ROL] = {"ROL", "Rotate Left. Rotates rs1 left by rs2 shift amount."};
        arr[ROR] = {"ROR", "Rotate Right. Rotates rs1 right by rs2 shift amount."};
        arr[RORI] = {"RORI", "Rotate Right Immediate. Rotates rs1 right by immediate shift amount."};
        arr[CLMUL] = {"CLMUL", "Polynomial Carry-less Multiply (low). Returns lower half of carry-less product."};
        arr[CLMULH] = {"CLMULH", "Polynomial Carry-less Multiply (high). Returns upper half of carry-less product."};
        arr[CLMULR] = {"CLMULR", "Polynomial Carry-less Multiply (round). Returns middle bits of carry-less product."};
        arr[BSET] = {"BSET", "Single-Bit Set. Returns rs1 with bit rs2 set to 1."};
        arr[BSETI] = {"BSETI", "Single-Bit Set Immediate. Returns rs1 with bit immediate set to 1."};
        arr[BCLR] = {"BCLR", "Single-Bit Clear. Returns rs1 with bit rs2 cleared to 0."};
        arr[BCLRI] = {"BCLRI", "Single-Bit Clear Immediate. Returns rs1 with bit immediate cleared to 0."};
        arr[BINV] = {"BINV", "Single-Bit Invert. Returns rs1 with bit rs2 inverted."};
        arr[BINVI] = {"BINVI", "Single-Bit Invert Immediate. Returns rs1 with bit immediate inverted."};
        arr[BEXT] = {"BEXT", "Single-Bit Extract. Extracts bit rs2 from rs1 and returns it in bit 0."};
        arr[BEXTI] = {"BEXTI", "Single-Bit Extract Immediate. Extracts bit immediate from rs1 and returns it in bit 0."};
        arr[ORC_B] = {"ORC_B", "Bit-wise OR-combine. Sets each byte to 0xFF if it contains any set bits, else 0x00."};
        arr[REV8] = {"REV8", "Reverse Bytes. Reverses byte order of the entire register (endianness swap)."};
        arr[PACK] = {"PACK", "Pack. Combines lower halfwords of rs1 and rs2 into rd."};

        arr[ADD_UW] = {"ADD_UW", "Add Unsigned Word. Zero-extends lower 32 bits of rs1 and adds rs2."};
        arr[SLLI_UW] = {"SLLI_UW", "Shift Left Logical Unsigned Word. Zero-extends rs1 and shifts left by immediate."};
        arr[SH1ADD_UW] = {"SH1ADD_UW", "Shift Left by 1 and Add Unsigned Word. Zero-extends rs1, shifts left by 1, and adds rs2."};
        arr[SH2ADD_UW] = {"SH2ADD_UW", "Shift Left by 2 and Add Unsigned Word. Zero-extends rs1, shifts left by 2, and adds rs2."};
        arr[SH3ADD_UW] = {"SH3ADD_UW", "Shift Left by 3 and Add Unsigned Word. Zero-extends rs1, shifts left by 3, and adds rs2."};

        arr[CLZW] = {"CLZW", "Count Leading Zeros Word. Returns number of leading zeros in the lower 32 bits."};
        arr[CTZW] = {"CTZW", "Count Trailing Zeros Word. Returns number of trailing zeros in the lower 32 bits."};
        arr[CPOPW] = {"CPOPW", "Count Population Word. Returns number of set bits in the lower 32 bits."};
        arr[ROLW] = {"ROLW", "Rotate Left Word. Rotates lower 32 bits of rs1 left by rs2."};
        arr[RORW] = {"RORW", "Rotate Right Word. Rotates lower 32 bits of rs1 right by rs2."};
        arr[RORIW] = {"RORIW", "Rotate Right Word Immediate. Rotates lower 32 bits of rs1 right by immediate."};
        arr[PACKW] = {"PACKW", "Pack Word. Packs the lower halfwords of rs1 and rs2 into lower 32 bits of rd (sign-extended)."};
        arr[VSETVLI] = {"VSETVLI", "Vector Set Configuration (immediate). Configures vtype and vl from immediate/rs1, and writes new vl to rd."};
        arr[VSETIVLI] = {"VSETIVLI", "Vector Set Configuration (immediate, uimm). Configures vtype and vl from immediate/uimm, and writes new vl to rd."};
        arr[VSETVL] = {"VSETVL", "Vector Set Configuration. Configures vtype and vl from rs2/rs1, and writes new vl to rd."};
        arr[VLE8_V] = {"VLE8.V", "Vector Load unit-strided 8-bit elements. Reads vl elements of 8-bit width from memory address rs1 into vector register rd."};
        arr[VLE16_V] = {"VLE16.V", "Vector Load unit-strided 16-bit elements. Reads vl elements of 16-bit width from memory address rs1 into vector register rd."};
        arr[VLE32_V] = {"VLE32.V", "Vector Load unit-strided 32-bit elements. Reads vl elements of 32-bit width from memory address rs1 into vector register rd."};
        arr[VSE8_V] = {"VSE8.V", "Vector Store unit-strided 8-bit elements. Writes vl elements of 8-bit width from vector register vs3 to memory address rs1."};
        arr[VSE16_V] = {"VSE16.V", "Vector Store unit-strided 16-bit elements. Writes vl elements of 16-bit width from vector register vs3 to memory address rs1."};
        arr[VSE32_V] = {"VSE32.V", "Vector Store unit-strided 32-bit elements. Writes vl elements of 32-bit width from vector register vs3 to memory address rs1."};
        arr[VADD_VV] = {"VADD.VV", "Vector-Vector Addition. Adds elements of vector register rs2 to vector register rs1, and stores result in rd."};
        arr[VADD_VX] = {"VADD.VX", "Vector-Scalar Addition. Adds scalar register rs1 to elements of vector register rs2, and stores result in rd."};
        arr[VADD_VI] = {"VADD.VI", "Vector-Immediate Addition. Adds sign-extended immediate to elements of vector register rs2, and stores result in rd."};
        arr[VSUB_VV] = {"VSUB.VV", "Vector-Vector Subtraction. Subtracts elements of vector register rs1 from vector register rs2, and stores result in rd."};
        arr[VSUB_VX] = {"VSUB.VX", "Vector-Scalar Subtraction. Subtracts scalar register rs1 from elements of vector register rs2, and stores result in rd."};
        arr[VMUL_VV] = {"VMUL.VV", "Vector-Vector Multiplication. Multiplies elements of vector register rs2 by vector register rs1, and stores result in rd."};
        arr[VMUL_VX] = {"VMUL.VX", "Vector-Scalar Multiplication. Multiplies elements of vector register rs2 by scalar register rs1, and stores result in rd."};
        arr[VDIV_VV] = {"VDIV.VV", "Vector-Vector Signed Division. Divides elements of vector register rs2 by vector register rs1, and stores result in rd."};
        arr[VDIV_VX] = {"VDIV.VX", "Vector-Scalar Signed Division. Divides elements of vector register rs2 by scalar register rs1, and stores result in rd."};
        arr[VDIVU_VV] = {"VDIVU.VV", "Vector-Vector Unsigned Division. Divides elements of vector register rs2 by vector register rs1, and stores result in rd."};
        arr[VDIVU_VX] = {"VDIVU.VX", "Vector-Scalar Unsigned Division. Divides elements of vector register rs2 by scalar register rs1, and stores result in rd."};
        arr[VAND_VV] = {"VAND.VV", "Vector-Vector Bitwise AND. Performs bitwise AND of vector register rs2 and vector register rs1, and stores result in rd."};
        arr[VAND_VX] = {"VAND.VX", "Vector-Scalar Bitwise AND. Performs bitwise AND of vector register rs2 and scalar register rs1, and stores result in rd."};
        arr[VAND_VI] = {"VAND.VI", "Vector-Immediate Bitwise AND. Performs bitwise AND of vector register rs2 and immediate, and stores result in rd."};
        arr[VOR_VV] = {"VOR.VV", "Vector-Vector Bitwise OR. Performs bitwise OR of vector register rs2 and vector register rs1, and stores result in rd."};
        arr[VOR_VX] = {"VOR.VX", "Vector-Scalar Bitwise OR. Performs bitwise OR of vector register rs2 and scalar register rs1, and stores result in rd."};
        arr[VOR_VI] = {"VOR.VI", "Vector-Immediate Bitwise OR. Performs bitwise OR of vector register rs2 and immediate, and stores result in rd."};
        arr[VXOR_VV] = {"VXOR.VV", "Vector-Vector Bitwise XOR. Performs bitwise XOR of vector register rs2 and vector register rs1, and stores result in rd."};
        arr[VXOR_VX] = {"VXOR.VX", "Vector-Scalar Bitwise XOR. Performs bitwise XOR of vector register rs2 and scalar register rs1, and stores result in rd."};
        arr[VXOR_VI] = {"VXOR.VI", "Vector-Immediate Bitwise XOR. Performs bitwise XOR of vector register rs2 and immediate, and stores result in rd."};
        arr[VSLL_VV] = {"VSLL.VV", "Vector-Vector Shift Left Logical. Shifts elements of vector register rs2 left by amounts in vector register rs1, and stores result in rd."};
        arr[VSLL_VX] = {"VSLL.VX", "Vector-Scalar Shift Left Logical. Shifts elements of vector register rs2 left by amount in scalar register rs1, and stores result in rd."};
        arr[VSLL_VI] = {"VSLL.VI", "Vector-Immediate Shift Left Logical. Shifts elements of vector register rs2 left by immediate amount, and stores result in rd."};
        arr[VSRL_VV] = {"VSRL.VV", "Vector-Vector Shift Right Logical. Shifts elements of vector register rs2 right by amounts in vector register rs1, and stores result in rd."};
        arr[VSRL_VX] = {"VSRL.VX", "Vector-Scalar Shift Right Logical. Shifts elements of vector register rs2 right by amount in scalar register rs1, and stores result in rd."};
        arr[VSRL_VI] = {"VSRL.VI", "Vector-Immediate Shift Right Logical. Shifts elements of vector register rs2 right by immediate amount, and stores result in rd."};
        arr[VSRA_VV] = {"VSRA.VV", "Vector-Vector Shift Right Arithmetic. Shifts elements of vector register rs2 right arithmetically by amounts in vector register rs1, and stores result in rd."};
        arr[VSRA_VX] = {"VSRA.VX", "Vector-Scalar Shift Right Arithmetic. Shifts elements of vector register rs2 right arithmetically by amount in scalar register rs1, and stores result in rd."};
        arr[VSRA_VI] = {"VSRA.VI", "Vector-Immediate Shift Right Arithmetic. Shifts elements of vector register rs2 right arithmetically by immediate amount, and stores result in rd."};
        arr[VMV_V_V] = {"VMV.V.V", "Vector Register Copy. Copies elements of vector register rs1 to vector register rd."};
        arr[VMV_V_X] = {"VMV.V.X", "Vector-Scalar Move/Splat. Copies scalar register rs1 to all active elements of vector register rd."};
        arr[VMV_V_I] = {"VMV.V.I", "Vector-Immediate Move/Splat. Copies immediate to all active elements of vector register rd."};
        arr[VMV_X_S] = {"VMV.X.S", "Vector Move Element 0 to Scalar. Copies element 0 of vector register rs2 to scalar register rd."};
        arr[VMV_S_X] = {"VMV.S.X", "Vector Move Scalar to Element 0. Copies scalar register rs1 to element 0 of vector register rd."};
        arr[VMSEQ_VV] = {"VMSEQ.VV", "Vector-Vector Compare Equal. Compares vector register rs2 with rs1, and writes mask result bits to vector register rd."};
        arr[VMSEQ_VX] = {"VMSEQ.VX", "Vector-Scalar Compare Equal. Compares vector register rs2 with scalar rs1, and writes mask result bits to vector register rd."};
        arr[VMSEQ_VI] = {"VMSEQ.VI", "Vector-Immediate Compare Equal. Compares vector register rs2 with immediate, and writes mask result bits to vector register rd."};
        arr[VMSNE_VV] = {"VMSNE.VV", "Vector-Vector Compare Not Equal. Compares vector register rs2 with rs1, and writes mask result bits to vector register rd."};
        arr[VMSNE_VX] = {"VMSNE.VX", "Vector-Scalar Compare Not Equal. Compares vector register rs2 with scalar rs1, and writes mask result bits to vector register rd."};
        arr[VMSNE_VI] = {"VMSNE.VI", "Vector-Immediate Compare Not Equal. Compares vector register rs2 with immediate, and writes mask result bits to vector register rd."};
        arr[VMSLT_VV] = {"VMSLT.VV", "Vector-Vector Compare Less Than (signed). Compares signed elements in rs2 and rs1, and writes mask result bits to vector register rd."};
        arr[VMSLT_VX] = {"VMSLT.VX", "Vector-Scalar Compare Less Than (signed). Compares signed elements in rs2 and scalar rs1, and writes mask result bits to vector register rd."};
        arr[VMSLTU_VV] = {"VMSLTU.VV", "Vector-Vector Compare Less Than (unsigned). Compares unsigned elements in rs2 and rs1, and writes mask result bits to vector register rd."};
        arr[VMSLTU_VX] = {"VMSLTU.VX", "Vector-Scalar Compare Less Than (unsigned). Compares unsigned elements in rs2 and scalar rs1, and writes mask result bits to vector register rd."};
        arr[VMSLE_VV] = {"VMSLE.VV", "Vector-Vector Compare Less Than or Equal (signed). Compares signed elements in rs2 and rs1, and writes mask result bits to vector register rd."};
        arr[VMSLE_VX] = {"VMSLE.VX", "Vector-Scalar Compare Less Than or Equal (signed). Compares signed elements in rs2 and scalar rs1, and writes mask result bits to vector register rd."};
        arr[VMSLE_VI] = {"VMSLE.VI", "Vector-Immediate Compare Less Than or Equal (signed). Compares signed elements in rs2 and immediate, and writes mask result bits to vector register rd."};
        arr[VMSLEU_VV] = {"VMSLEU.VV", "Vector-Vector Compare Less Than or Equal (unsigned). Compares unsigned elements in rs2 and rs1, and writes mask result bits to vector register rd."};
        arr[VMSLEU_VX] = {"VMSLEU.VX", "Vector-Scalar Compare Less Than or Equal (unsigned). Compares unsigned elements in rs2 and scalar rs1, and writes mask result bits to vector register rd."};
        arr[VMSLEU_VI] = {"VMSLEU.VI", "Vector-Immediate Compare Less Than or Equal (unsigned). Compares unsigned elements in rs2 and immediate, and writes mask result bits to vector register rd."};
        arr[VMSGT_VX] = {"VMSGT.VX", "Vector-Scalar Compare Greater Than (signed). Compares signed elements in rs2 and scalar rs1, and writes mask result bits to vector register rd."};
        arr[VMSGT_VI] = {"VMSGT.VI", "Vector-Immediate Compare Greater Than (signed). Compares signed elements in rs2 and immediate, and writes mask result bits to vector register rd."};
        arr[VMSGTU_VX] = {"VMSGTU.VX", "Vector-Scalar Compare Greater Than (unsigned). Compares unsigned elements in rs2 and scalar rs1, and writes mask result bits to vector register rd."};
        arr[VMSGTU_VI] = {"VMSGTU.VI", "Vector-Immediate Compare Greater Than (unsigned). Compares unsigned elements in rs2 and immediate, and writes mask result bits to vector register rd."};
        arr[VMERGE_VVM] = {"VMERGE.VVM", "Vector Merge. Merges vector register rs2 and rs1 under mask v0, and writes result to rd."};
        arr[VMERGE_VXM] = {"VMERGE.VXM", "Vector Merge. Merges vector register rs2 and scalar rs1 under mask v0, and writes result to rd."};
        arr[VMERGE_VIM] = {"VMERGE.VIM", "Vector Merge. Merges vector register rs2 and immediate under mask v0, and writes result to rd."};

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

auto print_r_format(uint32_t funct7_val, uint32_t rs2_val, uint32_t rs1_val, uint32_t funct3_val, uint32_t rd_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (R-Type format):");
    std::println("  31         25 24      20 19      15 1412 11       7 6           0");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::println("  |   funct7   |   rs2    |   rs1    | f3 |    rd    |   opcode    |");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::print("  |   {}{:07b}{}  |  {}{:05b}{}   |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
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

auto print_i_format(uint32_t imm_bits, int32_t imm_val, uint32_t rs1_val, uint32_t funct3_val, uint32_t rd_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (I-Type format):");
    std::println("  31                   20 19      15 1412 11       7 6           0");
    std::println("  +----------------------+----------+----+----------+-------------+");
    std::println("  |      immediate       |   rs1    | f3 |    rd    |   opcode    |");
    std::println("  +----------------------+----------+----+----------+-------------+");
    std::print("  |     {}{:012b}{}     |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
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
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Sign-extended to 64 bits: {}{} (0x{:016X}){}", c(kBrightRed), imm_val, static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)    : {}{:064b}{}", c(kBrightRed), static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Sign-extended to 32 bits: {}{} (0x{:08X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)    : {}{:032b}{}", c(kBrightRed), static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_s_format(uint32_t imm_hi, uint32_t imm_lo, int32_t imm_val, uint32_t rs1_val, uint32_t rs2_val, uint32_t funct3_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (S-Type format):");
    std::println("  31         25 24      20 19      15 1412 11       7 6           0");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::println("  |  imm[11:5] |   rs2    |   rs1    | f3 | imm[4:0] |   opcode    |");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::print("  |   {}{:07b}{}  |  {}{:05b}{}   |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
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
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Sign-extended to 64 bits: {}{} (0x{:016X}){}", c(kBrightRed), imm_val, static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)    : {}{:064b}{}", c(kBrightRed), static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Sign-extended to 32 bits: {}{} (0x{:08X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)    : {}{:032b}{}", c(kBrightRed), static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_b_format(uint32_t inst, uint32_t imm_hi, uint32_t imm_lo, int32_t imm_val, uint32_t rs1_val, uint32_t rs2_val, uint32_t funct3_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (B-Type format):");
    std::println("  31         25 24      20 19      15 1412 11       7 6           0");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::println("  |imm[12|10:5]|   rs2    |   rs1    | f3 |imm[4:1|11]|   opcode    |");
    std::println("  +------------+----------+----------+----+----------+-------------+");
    std::print("  |   {}{:07b}{}  |  {}{:05b}{}   |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
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
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Sign-extended to 64 bits: {}{} bytes (0x{:016X}){}", c(kBrightRed), imm_val, static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)    : {}{:064b}{}", c(kBrightRed), static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Sign-extended to 32 bits: {}{} bytes (0x{:08X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)    : {}{:032b}{}", c(kBrightRed), static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_u_format(uint32_t imm_bits, int32_t imm_val, uint32_t rd_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (U-Type format):");
    std::println("  31                                   12 11       7 6           0");
    std::println("  +--------------------------------------+----------+-------------+");
    std::println("  |              immediate               |    rd    |   opcode    |");
    std::println("  +--------------------------------------+----------+-------------+");
    std::print("  |         {}{:020b}{}         |  {}{:05b}{}   |   {}{:07b}{}   |\n",
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
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Combined Value           : {}{} (0x{:016X}){}", c(kBrightRed), imm_val, static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)     : {}{:064b}{}", c(kBrightRed), static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Combined Value           : {}{} (0x{:08X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)     : {}{:032b}{}", c(kBrightRed), static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_j_format(uint32_t inst, uint32_t imm_bits, int32_t imm_val, uint32_t rd_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (J-Type format):");
    std::println("  31                                   12 11       7 6           0");
    std::println("  +--------------------------------------+----------+-------------+");
    std::println("  |              immediate               |    rd    |   opcode    |");
    std::println("  +--------------------------------------+----------+-------------+");
    std::print("  |         {}{:020b}{}         |  {}{:05b}{}   |   {}{:07b}{}   |\n",
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
    if constexpr (simrv::xlen::kIsXLen64) {
        std::println("  Sign-extended to 64 bits: {}{} bytes (0x{:016X}){}", c(kBrightRed), imm_val, static_cast<uint64_t>(imm_val), c(kReset));
        std::println("  Binary (64-bit view)    : {}{:064b}{}", c(kBrightRed), static_cast<uint64_t>(imm_val), c(kReset));
    } else {
        std::println("  Sign-extended to 32 bits: {}{} bytes (0x{:08X}){}", c(kBrightRed), imm_val, static_cast<uint32_t>(imm_val), c(kReset));
        std::println("  Binary (32-bit view)    : {}{:032b}{}", c(kBrightRed), static_cast<uint32_t>(imm_val), c(kReset));
    }
}

auto print_r4_format(uint32_t funct7_val, uint32_t rs3_val, uint32_t rs2_val, uint32_t rs1_val, uint32_t funct3_val, uint32_t rd_val, Opcode op, bool use_color) -> void {
    auto c = [use_color](std::string_view code) -> std::string_view { return c_code(code, use_color); };
    std::println("Visual Bit Fields Breakdown (R4-Type format):");
    std::println("  31   27 2625    24      20 19      15 1412 11       7 6           0");
    std::println("  +-----+----+--+----------+----------+----+----------+-------------+");
    std::println("  | rs3 |fmt |..|   rs2    |   rs1    | f3 |    rd    |   opcode    |");
    std::println("  +-----+----+--+----------+----------+----+----------+-------------+");
    std::print("  |{}{:05b}{} | {:02b} |00|  {}{:05b}{}   |  {}{:05b}{}   |{}{:03b}{} |  {}{:05b}{}   |   {}{:07b}{}   |\n",
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

auto format_r_type(OperationId op_id, std::string_view mnemonic, uint32_t rd_val, uint32_t rs1_val, uint32_t rs2_val, bool is_fp_sys) -> std::string {
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
    } else if ((op_id >= LR_W && op_id <= SC_W) || (op_id >= LR_D && op_id <= SC_D)) {
        if (op_id == LR_W) {
            return std::format("lr.w {}, ({})", rd_str, rs1_str);
        } else if (op_id == LR_D) {
            return std::format("lr.d {}, ({})", rd_str, rs1_str);
        } else if (op_id == SC_W) {
            return std::format("sc.w {}, {}, ({})", rd_str, rs2_str, rs1_str);
        } else {
            return std::format("sc.d {}, {}, ({})", rd_str, rs2_str, rs1_str);
        }
    } else if ((op_id >= AMOSWAP_W && op_id <= AMOMAXU_W) || (op_id >= AMOSWAP_D && op_id <= AMOMAXU_D)) {
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

auto format_i_type(OperationId op_id, std::string_view mnemonic, uint32_t rd_val, uint32_t rs1_val, int32_t imm_val, uint32_t csr_val, Opcode op, bool is_load, bool is_csr) -> std::string {
    std::string const rd_str = ABI_NAMES[rd_val];
    std::string const rs1_str = ABI_NAMES[rs1_val];
    std::string const frd_str = FP_ABI_NAMES[rd_val];

    if (is_load) {
        if (op == Opcode::LoadFp) {
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

auto format_s_type(std::string_view mnemonic, uint32_t rs1_val, uint32_t rs2_val, int32_t imm_val, Opcode op) -> std::string {
    std::string const rs1_str = ABI_NAMES[rs1_val];
    std::string const rs2_str = ABI_NAMES[rs2_val];
    std::string const frs2_str = FP_ABI_NAMES[rs2_val];

    if (op == Opcode::StoreFp) {
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

auto get_reg_name(uint32_t idx, bool is_fp) -> std::string {
    if (is_fp) {
        if (idx < FP_ABI_NAMES.size()) {
            return std::format("f{} ({})", idx, FP_ABI_NAMES[idx]);
        }
        return std::format("f{}", idx);
    } else {
        if (idx == 0) {
            return "x0 (zero)";
        }
        if (idx < ABI_NAMES.size()) {
            return std::format("x{} ({})", idx, ABI_NAMES[idx]);
        }
        return std::format("x{}", idx);
    }
}

void explain_pipeline_hazards(OperationId op_id, InstFormat fmt, uint32_t rd_val, uint32_t rs1_val, uint32_t rs2_val, uint32_t rs3_val, Opcode op, bool use_color) {
    auto c = [use_color](std::string_view ansi_code) -> std::string_view {
        return use_color ? ansi_code : "";
    };
    constexpr std::string_view kItalic = "\033[3m";

    bool writes_rd = false;
    bool rd_is_fp = false;
    
    if (fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::U || fmt == InstFormat::J || fmt == InstFormat::R4) {
        writes_rd = true;
    }
    
    if (op_id == ECALL || op_id == EBREAK || op_id == FENCE || op_id == FENCE_I || 
        op_id == WFI || op_id == SRET || op_id == MRET || op_id == SFENCE_VMA ||
        op_id == UNKNOWN) {
        writes_rd = false;
    }
    
    if (writes_rd) {
        if (op == Opcode::LoadFp) {
            rd_is_fp = true;
        } else if (op == Opcode::OpFp || op == Opcode::MAdd || op == Opcode::MSub || op == Opcode::NMSub || op == Opcode::NMAdd) {
            if (op_id == FEQ_S || op_id == FLT_S || op_id == FLE_S ||
                op_id == FEQ_D || op_id == FLT_D || op_id == FLE_D ||
                op_id == FCLASS_S || op_id == FCLASS_D ||
                (op_id >= FCVT_W_S && op_id <= FCVT_LU_S) ||
                (op_id >= FCVT_W_D && op_id <= FCVT_LU_D) ||
                op_id == FMV_X_W || op_id == FMV_X_D) {
                rd_is_fp = false;
            } else {
                rd_is_fp = true;
            }
        }
    }

    bool reads_rs1 = false;
    bool rs1_is_fp = false;
    
    if (fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::R4) {
        reads_rs1 = true;
    }
    
    if (op_id == ECALL || op_id == EBREAK || op_id == FENCE || op_id == FENCE_I || 
        op_id == WFI || op_id == SRET || op_id == MRET ||
        op_id == CSRRWI || op_id == CSRRSI || op_id == CSRRCI ||
        op_id == UNKNOWN) {
        reads_rs1 = false;
    }
    
    if (reads_rs1) {
        if (op == Opcode::OpFp || op == Opcode::MAdd || op == Opcode::MSub || op == Opcode::NMSub || op == Opcode::NMAdd) {
            if ((op_id >= FCVT_S_W && op_id <= FCVT_S_LU) ||
                (op_id >= FCVT_D_W && op_id <= FCVT_D_LU) ||
                op_id == FMV_W_X || op_id == FMV_D_X) {
                rs1_is_fp = false;
            } else {
                rs1_is_fp = true;
            }
        }
    }

    bool reads_rs2 = false;
    bool rs2_is_fp = false;
    
    if (fmt == InstFormat::R || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::R4) {
        reads_rs2 = true;
    }
    
    if (op_id == UNKNOWN) {
        reads_rs2 = false;
    }
    
    if (reads_rs2) {
        if (op == Opcode::StoreFp) {
            rs2_is_fp = true;
        } else if (op == Opcode::OpFp || op == Opcode::MAdd || op == Opcode::MSub || op == Opcode::NMSub || op == Opcode::NMAdd) {
            if (op_id == FSQRT_S || op_id == FSQRT_D ||
                op_id == FCLASS_S || op_id == FCLASS_D ||
                (op_id >= FCVT_W_S && op_id <= FCVT_LU_S) ||
                (op_id >= FCVT_W_D && op_id <= FCVT_LU_D) ||
                (op_id >= FCVT_S_W && op_id <= FCVT_S_LU) ||
                (op_id >= FCVT_D_W && op_id <= FCVT_D_LU) ||
                op_id == FMV_X_W || op_id == FMV_X_D ||
                op_id == FMV_W_X || op_id == FMV_D_X) {
                reads_rs2 = false;
            } else {
                rs2_is_fp = true;
            }
        }
    }

    bool reads_rs3 = (fmt == InstFormat::R4);
    bool rs3_is_fp = reads_rs3;

    std::println("{}Pipeline Hazard Analysis:{}", c(kBold), c(kReset));

    bool has_registers = false;
    if (reads_rs1 || reads_rs2 || reads_rs3) {
        std::println("  Register Reads (Data Dependencies):");
        if (reads_rs1) {
            std::println("    - rs1: {} ({})", get_reg_name(rs1_val, rs1_is_fp), rs1_is_fp ? "FP" : "GPR");
        }
        if (reads_rs2) {
            std::println("    - rs2: {} ({})", get_reg_name(rs2_val, rs2_is_fp), rs2_is_fp ? "FP" : "GPR");
        }
        if (reads_rs3) {
            std::println("    - rs3: {} ({})", get_reg_name(rs3_val, rs3_is_fp), rs3_is_fp ? "FP" : "GPR");
        }
        has_registers = true;
    }

    if (writes_rd) {
        std::println("  Register Writes (Data Production):");
        std::println("    - rd : {} ({})", get_reg_name(rd_val, rd_is_fp), rd_is_fp ? "FP" : "GPR");
        has_registers = true;
    }

    if (!has_registers) {
        std::println("    (No register operands accessed by this instruction)");
    }

    bool has_read_hazard = false;
    std::string read_hazards_str;
    
    if (reads_rs1 && !(rs1_val == 0 && !rs1_is_fp)) {
        read_hazards_str += std::format("\n       * '{}'", get_reg_name(rs1_val, rs1_is_fp));
        has_read_hazard = true;
    }
    if (reads_rs2 && !(rs2_val == 0 && !rs2_is_fp)) {
        read_hazards_str += std::format("\n       * '{}'", get_reg_name(rs2_val, rs2_is_fp));
        has_read_hazard = true;
    }
    if (reads_rs3 && !(rs3_val == 0 && !rs3_is_fp)) {
        read_hazards_str += std::format("\n       * '{}'", get_reg_name(rs3_val, rs3_is_fp));
        has_read_hazard = true;
    }

    if (has_read_hazard) {
        std::println("\n  Read-After-Write (RAW) Data Hazards (Consumer Side):");
        std::print("    - If a preceding instruction writes to any of:{}", read_hazards_str);
        std::println("\n      it creates a RAW hazard requiring resolution:");
        std::println("      * {}Standard 5-stage pipeline without forwarding:{}", c(kBold), c(kReset));
        std::println("        - Stall for 2 cycles if the producer is 1 cycle ahead.");
        std::println("        - Stall for 1 cycle if the producer is 2 cycles ahead.");
        std::println("      * {}Pipeline with operand forwarding:{}", c(kBold), c(kReset));
        std::println("        - 0 stall cycles for most ALU-to-ALU dependencies (forwarded from EX/MEM or MEM/WB).");
        std::println("        - 1 stall cycle ('load-use' delay) if the producer is a LOAD instruction immediately preceding this one.");
    }

    if (writes_rd && !(rd_val == 0 && !rd_is_fp)) {
        std::println("\n  Read-After-Write (RAW) Data Hazards (Producer Side):");
        std::println("    - This instruction writes to '{}'. Any subsequent instruction", get_reg_name(rd_val, rd_is_fp));
        std::println("      reading this register within 1-2 cycles will face a RAW hazard:");
        std::println("      * {}Standard 5-stage pipeline without forwarding:{}", c(kBold), c(kReset));
        std::println("        - The subsequent instruction will stall 2 cycles if it immediately follows this one.");
        std::println("        - The subsequent instruction will stall 1 cycle if it is 2 cycles later.");
        std::println("      * {}Pipeline with operand forwarding:{}", c(kBold), c(kReset));
        if (op == Opcode::Load || op == Opcode::LoadFp) {
            std::println("        - Since this is a LOAD, it produces the data in the MEM stage. An immediately");
            std::println("          following instruction that uses this data will stall for 1 cycle.");
        } else {
            std::println("        - Forwarding path from EX/MEM or MEM/WB will eliminate stalls (0 cycles) for ALU consumers.");
        }
    }

    if ((reads_rs1 && rs1_val == 0 && !rs1_is_fp) || (reads_rs2 && rs2_val == 0 && !rs2_is_fp) || (writes_rd && rd_val == 0 && !rd_is_fp)) {
        std::println("\n  {}Note on register x0 (zero):{}", c(kItalic), c(kReset));
        std::println("    - The 'x0' GPR is hardwired to zero. Reading it always returns 0, and writing");
        std::println("      to it has no effect. Thus, accesses to 'x0' never cause pipeline data hazards.");
    }

    bool is_branch_jump = (op == Opcode::Branch) || (op_id == JALR) || (op_id == JAL);
    bool is_exception_flush = (op_id == SRET || op_id == MRET || op_id == SFENCE_VMA);

    if (is_branch_jump || is_exception_flush) {
        std::println("\n  Control Hazards (Branch/Jump Penalties):");
        if (is_branch_jump) {
            std::println("    - This instruction alters the control flow (Program Counter).");
            std::println("    - In a pipelined CPU:");
            std::println("      * {}Without Branch Prediction:{}", c(kBold), c(kReset));
            std::println("        - The processor stalls/flushes (typically 1-2 bubble cycles) when a branch is taken.");
            std::println("      * {}With Branch Prediction:{}", c(kBold), c(kReset));
            std::println("        - 0 stall cycles if the branch outcome and target are correctly predicted.");
            std::println("        - 2 to 3 penalty cycles (flushing speculative instructions) if mispredicted.");
        } else if (is_exception_flush) {
            std::println("    - This instruction returns from an exception or invalidates TLB page translations.");
            std::println("    - Resolution requires flushing speculatively fetched instructions from the pipeline");
            std::println("      to ensure correct architectural state and TLB consistency.");
        }
    }
}

void explain_datapath_diagram(isa::OperationId op_id, isa::InstFormat fmt, isa::Opcode op, bool use_color) {
    auto c = [use_color](std::string_view ansi_code) -> std::string_view {
        return use_color ? ansi_code : "";
    };

    std::println("\n{}Hardware Data-Path Routing:{}", c(kBold), c(kReset));

    // Identify instruction classes
    bool const is_load = (op == isa::Opcode::Load) || (op == isa::Opcode::LoadFp);
    bool const is_store = (op == isa::Opcode::Store) || (op == isa::Opcode::StoreFp);
    bool const is_csr = (op_id >= isa::OperationId::CSRRW && op_id <= isa::OperationId::CSRRCI);
    bool const is_branch = (op == isa::Opcode::Branch);
    bool const is_jal = (op_id == isa::OperationId::JAL);
    bool const is_jalr = (op_id == isa::OperationId::JALR);

    if (is_load) {
        std::println(
            "  [rs1 (Base Reg)] ──► [Read Data 1] ──────────────┐\n"
            "                                                  ▼\n"
            "                                             [ ALU Addr ] ──► [ Data Memory ] ──► [rd (Dest Reg)]\n"
            "                                             [   Calc   ]     [ (Read Address) ]\n"
            "                                                  ▲\n"
            "  [Immediate (I)]  ──► [Sign-Extend Unit] ────────┘"
        );
    } else if (is_store) {
        std::println(
            "  [rs1 (Base Reg)] ──► [Read Data 1] ──────────────┐\n"
            "                                                  ▼\n"
            "                                             [ ALU Addr ] ──► [ Data Memory ]\n"
            "                                             [   Calc   ]     [ (Write Address) ]\n"
            "                                                  ▲                    ▲\n"
            "  [Immediate (S)]  ──► [Sign-Extend Unit] ────────┘                    │\n"
            "                                                                       │\n"
            "  [rs2 (Src Reg)]  ──► [Read Data 2] ──────────────────────────────────┘\n"
            "                                                                  (Write Data)"
        );
    } else if (is_branch) {
        std::println(
            "  [rs1 (Src Reg 1)] ──► [Read Data 1] ─────────────┐\n"
            "                                                   ▼\n"
            "                                              [ ALU / Comp ] ──► [ Branch Control ]\n"
            "                                              [ (Evaluate) ]     [   Taken / NT   ] ──► [PC Target]\n"
            "                                                   ▲                    ▲\n"
            "  [rs2 (Src Reg 2)] ──► [Read Data 2] ─────────────┘                    │\n"
            "                                                                        │\n"
            "  [Immediate (B)]   ──► [Sign-Extend Unit] ─────────────────────────────┘\n"
            "                                                                  (Target Offset)"
        );
    } else if (is_jal) {
        std::println(
            "  [Current PC] ───────────────────► [ Adder (+4) ] ────────────────────► [rd (Dest Reg)]\n"
            "        │\n"
            "        ▼\n"
            "  [Current PC] ─────────┐\n"
            "                        ▼\n"
            "                   [ ALU Target ] ◄── [Sign-Extend Unit] ◄── [Immediate (J)]\n"
            "                   [   Adder    ]\n"
            "                        │\n"
            "                        ▼\n"
            "                 [New PC Target]"
        );
    } else if (is_jalr) {
        std::println(
            "  [Current PC] ───────────────────► [ Adder (+4) ] ────────────────────► [rd (Dest Reg)]\n"
            "\n"
            "  [rs1 (Target Reg)] ──► [Read Data 1] ────────────┐\n"
            "                                                   ▼\n"
            "                                              [ ALU Target ] ──► [New PC Target]\n"
            "                                              [   Adder    ]\n"
            "                                                   ▲\n"
            "  [Immediate (I)]    ──► [Sign-Extend Unit] ───────┘"
        );
    } else if (is_csr) {
        std::println(
            "  [rs1 / uimm] ────────► [ Read Data 1 / uimm ] ───┐\n"
            "                                                   ▼\n"
            "                                              [ CSR Logic  ] ──► [rd (Dest Reg)] (Old Value)\n"
            "                                              [ (ALU/Mux)  ]\n"
            "                                                   ▲\n"
            "  [CSR Address] ───────► [Read CSR State] ─────────┴───────────► [Write New CSR State]"
        );
    } else if (fmt == isa::InstFormat::R) {
        std::println(
            "  [rs1 (Src Reg 1)] ──► [Read Data 1] ────────────────────────────► [   ALU    ]\n"
            "                                                                    [ (Op: Math) ] ──► [rd (Dest Reg)]\n"
            "  [rs2 (Src Reg 2)] ──► [Read Data 2] ──► [Mux: RS2 Select] ──────► [          ]\n"
            "                                                 ▲\n"
            "                                           (Selects RS2)"
        );
    } else if (fmt == isa::InstFormat::I) {
        std::println(
            "  [rs1 (Src Reg 1)] ──► [Read Data 1] ────────────────────────────► [   ALU    ]\n"
            "                                                                    [ (Op: Math) ] ──► [rd (Dest Reg)]\n"
            "  [Immediate (I)]   ──► [Sign-Extend] ───► [Mux: RS2 Select] ──────► [          ]\n"
            "                                                 ▲\n"
            "                                           (Selects Imm)"
        );
    } else {
        std::println("  (Data-path layout not defined for this instruction format)");
    }
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

    // Extract core fields using Decoder
    simrv::pipeline::Decoder const dec(inst);
    auto const op = dec.opcode();
    uint32_t const rd_val = std::to_underlying(dec.rd());
    uint32_t const funct3_val = std::to_underlying(dec.funct3());
    uint32_t const rs1_val = std::to_underlying(dec.rs1());
    uint32_t const rs2_val = std::to_underlying(dec.rs2());
    uint32_t const funct7_val = dec.funct7();
    uint32_t const rs3_val = std::to_underlying(dec.rs3());
    uint32_t const csr_val = dec.csr();

    InstFormat const fmt = simrv::isa::get_instruction_format(op);
    std::println("Instruction Format: {}{}{}", c(kBold), simrv::isa::get_instruction_format_name(fmt), c(kReset));

    // Print Visual Fields
    std::println("");
    if (fmt == InstFormat::R) {
        print_r_format(funct7_val, rs2_val, rs1_val, funct3_val, rd_val, op, use_color);
    } else if (fmt == InstFormat::I) {
        uint32_t const imm_bits = (inst >> 20) & 0xFFF;
        print_i_format(imm_bits, dec.imm_i(), rs1_val, funct3_val, rd_val, op, use_color);
    } else if (fmt == InstFormat::S) {
        uint32_t const imm_hi = (inst >> 25) & 0x7F;
        uint32_t const imm_lo = (inst >> 7) & 0x1F;
        print_s_format(imm_hi, imm_lo, dec.imm_s(), rs1_val, rs2_val, funct3_val, op, use_color);
    } else if (fmt == InstFormat::B) {
        uint32_t const imm_hi = (inst >> 25) & 0x7F;
        uint32_t const imm_lo = (inst >> 7) & 0x1F;
        print_b_format(inst, imm_hi, imm_lo, dec.imm_b(), rs1_val, rs2_val, funct3_val, op, use_color);
    } else if (fmt == InstFormat::U) {
        uint32_t const imm_bits = (inst >> 12) & 0xFFFFF;
        print_u_format(imm_bits, dec.imm_u(), rd_val, op, use_color);
    } else if (fmt == InstFormat::J) {
        uint32_t const imm_bits = ((inst >> 12) & 0xFFFFF);
        print_j_format(inst, imm_bits, dec.imm_j(), rd_val, op, use_color);
    } else if (fmt == InstFormat::R4) {
        print_r4_format(funct7_val, rs3_val, rs2_val, rs1_val, funct3_val, rd_val, op, use_color);
    } else {
        std::println("Visual Bit Fields Breakdown: Format unrecognized.");
    }

    // Decode Operation ID
    std::println("--------------------------------------------------------------------------------");
    OperationId const op_id = simrv::pipeline::decoder(inst);
    auto const [mnemonic, desc] = get_description(op_id);

    std::println("{}Decoded Instruction Detail:{}", c(kBold), c(kReset));
    if (op_id != UNKNOWN) {
        std::println("  Assembly Mnemonic: {}{}{}", c(kBoldFgBrightGreen), mnemonic, c(kReset));

        // Format Assembly Representation
        std::string assembly;
        bool const is_load = (op == Opcode::Load) || (op == Opcode::LoadFp);
        bool const is_csr = (op_id >= CSRRW) && (op_id <= CSRRCI);
        bool const is_fp_sys = (op_id >= FCVT_W_S && op_id <= FMV_W_X) ||
                              (op_id >= FCVT_W_D && op_id <= FMV_D_X);

        if (fmt == InstFormat::R) {
            assembly = format_r_type(op_id, mnemonic, rd_val, rs1_val, rs2_val, is_fp_sys);
        } else if (fmt == InstFormat::I) {
            assembly = format_i_type(op_id, mnemonic, rd_val, rs1_val, dec.imm_i(), csr_val, op, is_load, is_csr);
        } else if (fmt == InstFormat::S) {
            assembly = format_s_type(mnemonic, rs1_val, rs2_val, dec.imm_s(), op);
        } else if (fmt == InstFormat::B) {
            assembly = format_b_type(mnemonic, rs1_val, rs2_val, dec.imm_b());
        } else if (fmt == InstFormat::U) {
            assembly = format_u_type(mnemonic, rd_val, dec.imm_u());
        } else if (fmt == InstFormat::J) {
            assembly = format_j_type(rd_val, dec.imm_j());
        } else if (fmt == InstFormat::R4) {
            assembly = format_r4_type(mnemonic, rd_val, rs1_val, rs2_val, rs3_val);
        }

        std::println("  Assembly Rep     : {}# {}{}", c(kBrightBlack), assembly, c(kReset));
        std::println("\nDescription (Behavior):");
        std::println("  [ISA: {}] {}", get_isa_extension_name(op_id), desc);

        std::println("\n--------------------------------------------------------------------------------");
        explain_pipeline_hazards(op_id, fmt, rd_val, rs1_val, rs2_val, rs3_val, op, use_color);
        std::println("\n--------------------------------------------------------------------------------");
        explain_datapath_diagram(op_id, fmt, op, use_color);

    } else {
        std::println("  Mnemonic         : {}UNKNOWN / RESERVED{}", c(kBoldFgRed), c(kReset));
        std::println("  This opcode is either reserved, or part of an unsupported custom extension.");
    }
    std::println("\n{}================================================={}\n", c(kBoldFgBrightBlue), c(kReset));
}

auto get_operation_details(OperationId op_id) -> std::pair<std::string_view, std::string_view> {
    return get_description(op_id);
}

auto get_isa_extension_name(OperationId op_id) -> std::string_view {
    if (op_id >= LUI && op_id <= CSRRCI) {
        if (op_id == LWU || op_id == LD || op_id == SD ||
            op_id == ADDIW || op_id == SLLIW || op_id == SRLIW || op_id == SRAIW ||
            op_id == ADDW || op_id == SUBW || op_id == SLLW || op_id == SRLW || op_id == SRAW) {
            return "RV64I";
        }
        return "RV32I";
    }
    if (op_id >= URET && op_id <= SFENCE_VMA) {
        return "Privileged";
    }
    if (op_id >= MUL && op_id <= REMUW) {
        if (op_id == MULW || op_id == DIVW || op_id == DIVUW || op_id == REMW || op_id == REMUW) {
            return "RV64M";
        }
        return "RV32M";
    }
    if (op_id >= LR_W && op_id <= AMOMAXU_W) {
        return "RV32A";
    }
    if (op_id >= LR_D && op_id <= AMOMAXU_D) {
        return "RV64A";
    }
    if (op_id >= FLW && op_id <= FCVT_S_LU) {
        return "RV32F";
    }
    if (op_id >= FLD && op_id <= FCVT_D_LU) {
        return "RV32D";
    }
    if (op_id >= SH1ADD && op_id <= PACKW) {
        if (op_id == SH1ADD || op_id == SH2ADD || op_id == SH3ADD ||
            op_id == ADD_UW || op_id == SLLI_UW ||
            op_id == SH1ADD_UW || op_id == SH2ADD_UW || op_id == SH3ADD_UW) {
            return "Zba";
        }
        if (op_id == CLMUL || op_id == CLMULH || op_id == CLMULR) {
            return "Zbc";
        }
        if (op_id == BSET || op_id == BSETI || op_id == BCLR || op_id == BCLRI ||
            op_id == BINV || op_id == BINVI || op_id == BEXT || op_id == BEXTI) {
            return "Zbs";
        }
        return "Zbb";
    }
    if (op_id >= VSETVLI && op_id <= VMERGE_VIM) {
        return "RV32V";
    }
    return "Unknown";
}

} // namespace simrv::util
