/**
 * @file ScalarSpecCatalog.cpp
 * @brief Scalar, floating-point, atomic, bit-manipulation, privileged, and custom instruction
 *        specification metadata, formulas, and RISC-V Manual V20260120 citations.
 */
#include <array>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "ExplainSpecInternal.hpp"
#include "simrv/explain/InstructionExplainer.hpp"
#include "simrv/isa/OperationId.hpp"

namespace simrv::explain {

using simrv::isa::kOperationIdCount;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

namespace detail {

void populate_scalar_descriptions(OpDescTable& arr) {
    arr[LUI] = {"LUI",
                "Load Upper Immediate. Places the 20-bit U-immediate into bits 31-12 of "
                "destination register rd, filling the lowest 12 bits with zeros (and "
                "sign-extending bit 31 to 64 bits on RV64)."};
    arr[AUIPC] = {"AUIPC",
                  "Add Upper Immediate to PC. Forms a 32-bit offset from the 20-bit "
                  "U-immediate (filling the lowest 12 bits with zeros), adds this offset to "
                  "the address of the AUIPC instruction, and places the result in rd."};
    arr[JAL] = {"JAL",
                "Jump and Link. Sign-extends the 20-bit J-immediate offset (in multiples of "
                "2 bytes), adds it to the instruction PC to form the jump target address, "
                "and stores the address of the following instruction (PC + 4) in rd."};
    arr[JALR] = {"JALR",
                 "Jump and Link Register. Adds the sign-extended 12-bit I-immediate to rs1, "
                 "clears the least-significant bit of the result to form the target address "
                 "((rs1 + offset) & ~1), and writes the return address (PC + 4) to rd."};
    arr[BEQ] = {"BEQ",
                "Branch if Equal. Compares registers rs1 and rs2; if equal, branches to PC + "
                "sign-extended 13-bit B-immediate offset."};
    arr[BNE] = {"BNE",
                "Branch if Not Equal. Compares registers rs1 and rs2; if not equal, branches "
                "to PC + sign-extended 13-bit B-immediate offset."};
    arr[BLT] = {"BLT",
                "Branch if Less Than (Signed). Branches to PC + offset if signed rs1 is "
                "strictly less than signed rs2."};
    arr[BGE] = {"BGE",
                "Branch if Greater Than or Equal (Signed). Branches to PC + offset if signed "
                "rs1 is greater than or equal to signed rs2."};
    arr[BLTU] = {"BLTU",
                 "Branch if Less Than (Unsigned). Branches to PC + offset if unsigned rs1 is "
                 "strictly less than unsigned rs2."};
    arr[BGEU] = {"BGEU",
                 "Branch if Greater Than or Equal (Unsigned). Branches to PC + offset if "
                 "unsigned rs1 is greater than or equal to unsigned rs2."};
    arr[LB] = {"LB",
               "Load Byte. Loads an 8-bit value from effective address (rs1 + sign-extended "
               "12-bit offset), sign-extends it to XLEN bits, and writes it to rd."};
    arr[LH] = {"LH",
               "Load Halfword. Loads a 16-bit value from effective address (rs1 + "
               "sign-extended 12-bit offset), sign-extends it to XLEN bits, and writes it to "
               "rd."};
    arr[LW] = {"LW",
               "Load Word. Loads a 32-bit value from effective address (rs1 + sign-extended "
               "12-bit offset), sign-extends it to XLEN bits, and writes it to rd."};
    arr[LD] = {"LD",
               "Load Doubleword. Loads a 64-bit value from effective address (rs1 + "
               "sign-extended 12-bit offset) into register rd."};
    arr[LBU] = {"LBU",
                "Load Byte Unsigned. Loads an 8-bit value from effective address (rs1 + "
                "offset), zero-extends it to XLEN bits, and writes it to rd."};
    arr[LHU] = {"LHU",
                "Load Halfword Unsigned. Loads a 16-bit value from effective address (rs1 + "
                "offset), zero-extends it to XLEN bits, and writes it to rd."};
    arr[LWU] = {"LWU",
                "Load Word Unsigned. Loads a 32-bit value from effective address (rs1 + "
                "offset), zero-extends it to 64 bits, and writes it to rd."};
    arr[SB] = {"SB",
               "Store Byte. Stores the least-significant 8 bits of register rs2 to memory at "
               "effective address (rs1 + sign-extended 12-bit S-immediate offset)."};
    arr[SH] = {"SH",
               "Store Halfword. Stores the least-significant 16 bits of register rs2 to "
               "memory at effective address (rs1 + sign-extended 12-bit S-immediate offset)."};
    arr[SW] = {"SW",
               "Store Word. Stores the least-significant 32 bits of register rs2 to memory "
               "at effective address (rs1 + sign-extended 12-bit S-immediate offset)."};
    arr[SD] = {"SD",
               "Store Doubleword. Stores the 64-bit value in register rs2 to memory at "
               "effective address (rs1 + sign-extended 12-bit S-immediate offset)."};
    arr[ADDI] = {"ADDI",
                 "Add Immediate. Adds the sign-extended 12-bit immediate to register rs1 and "
                 "writes the XLEN-bit result to rd (ignoring arithmetic overflow)."};
    arr[SLTI] = {"SLTI",
                 "Set Less Than Immediate (Signed). Places the value 1 in register rd if "
                 "signed register rs1 is less than the sign-extended immediate, else 0."};
    arr[SLTIU] = {"SLTIU",
                  "Set Less Than Immediate (Unsigned). Places the value 1 in register rd if "
                  "unsigned rs1 is less than the sign-extended immediate treated as unsigned, "
                  "else 0."};
    arr[XORI] = {"XORI",
                 "Bitwise XOR Immediate. Performs bitwise exclusive-OR on register rs1 and "
                 "the sign-extended 12-bit immediate and writes the result to rd."};
    arr[ORI] = {"ORI",
                "Bitwise OR Immediate. Performs bitwise inclusive-OR on register rs1 and the "
                "sign-extended 12-bit immediate and writes the result to rd."};
    arr[ANDI] = {"ANDI",
                 "Bitwise AND Immediate. Performs bitwise AND on register rs1 and the "
                 "sign-extended 12-bit immediate and writes the result to rd."};
    arr[SLLI] = {"SLLI",
                 "Shift Left Logical Immediate. Shifts register rs1 left by the immediate "
                 "shift amount shamt, filling vacated lower bits with zeros, and writes to rd."};
    arr[SRLI] = {"SRLI",
                 "Shift Right Logical Immediate. Shifts register rs1 right by the immediate "
                 "shift amount shamt, filling vacated upper bits with zeros, and writes to rd."};
    arr[SRAI] = {"SRAI",
                 "Shift Right Arithmetic Immediate. Shifts register rs1 right by shamt, "
                 "replicating the original sign bit into vacated upper bits, and writes to rd."};
    arr[ADDIW] = {"ADDIW",
                  "Add Immediate Word. Adds the sign-extended 12-bit immediate to rs1, truncates "
                  "the result to 32 bits, sign-extends to 64 bits, and writes to rd."};
    arr[SLLIW] = {"SLLIW",
                  "Shift Left Logical Immediate Word. Shifts the lower 32 bits of rs1 left by "
                  "5-bit shamt, sign-extending the 32-bit result to 64 bits in rd."};
    arr[SRLIW] = {"SRLIW",
                  "Shift Right Logical Immediate Word. Logically shifts the lower 32 bits of rs1 "
                  "right by 5-bit shamt, sign-extending the 32-bit result to 64 bits in rd."};
    arr[SRAIW] = {
        "SRAIW",
        "Shift Right Arithmetic Immediate Word. Arithmetically shifts the lower 32 bits "
        "of rs1 right by 5-bit shamt, sign-extending the 32-bit result to 64 bits in rd."};
    arr[ADD] = {"ADD",
                "Add. Adds registers rs1 and rs2 and writes the lower XLEN bits of the sum "
                "to rd (ignoring arithmetic overflow)."};
    arr[SUB] = {"SUB",
                "Subtract. Subtracts register rs2 from rs1 and writes the lower XLEN bits of "
                "the difference to rd."};
    arr[SLL] = {"SLL",
                "Shift Left Logical. Shifts rs1 left by the shift amount held in the lower "
                "log2(XLEN) bits of rs2, writing the result to rd."};
    arr[SLT] = {"SLT",
                "Set Less Than (Signed). Writes 1 to rd if signed rs1 < signed rs2, else 0."};
    arr[SLTU] = {"SLTU",
                 "Set Less Than (Unsigned). Writes 1 to rd if unsigned rs1 < unsigned rs2, "
                 "else 0."};
    arr[XOR] = {"XOR",
                "Bitwise XOR. Performs bitwise exclusive-OR of rs1 and rs2 and writes result to "
                "rd."};
    arr[SRL] = {"SRL",
                "Shift Right Logical. Logically shifts rs1 right by the shift amount in the "
                "lower log2(XLEN) bits of rs2 and writes result to rd."};
    arr[SRA] = {"SRA",
                "Shift Right Arithmetic. Arithmetically shifts rs1 right by the shift amount "
                "in the lower log2(XLEN) bits of rs2 and writes result to rd."};
    arr[OR] = {"OR",
               "Bitwise OR. Performs bitwise inclusive-OR of rs1 and rs2 and writes result "
               "to rd."};
    arr[AND] = {"AND", "Bitwise AND. Performs bitwise AND of rs1 and rs2 and writes result to rd."};
    arr[ADDW] = {"ADDW",
                 "Add Word. Adds the lower 32 bits of rs1 and rs2, sign-extending the 32-bit "
                 "result to 64 bits in rd."};
    arr[SUBW] = {"SUBW",
                 "Subtract Word. Subtracts the lower 32 bits of rs2 from rs1, sign-extending "
                 "the 32-bit result to 64 bits in rd."};
    arr[SLLW] = {"SLLW",
                 "Shift Left Logical Word. Shifts the lower 32 bits of rs1 left by rs2[4:0], "
                 "sign-extending the 32-bit result to 64 bits in rd."};
    arr[SRLW] = {"SRLW",
                 "Shift Right Logical Word. Logically shifts the lower 32 bits of rs1 right "
                 "by rs2[4:0], sign-extending the 32-bit result to 64 bits in rd."};
    arr[SRAW] = {"SRAW",
                 "Shift Right Arithmetic Word. Arithmetically shifts the lower 32 bits of "
                 "rs1 right by rs2[4:0], sign-extending the 32-bit result to 64 bits in rd."};
    arr[FENCE] = {"FENCE",
                  "Fence. Orders device I/O and memory accesses viewed by other RISC-V harts "
                  "and external devices according to predecessor and successor sets."};
    arr[FENCE_I] = {"FENCE.I",
                    "Instruction-Stream Fence. Synchronizes updates to instruction memory "
                    "with subsequent instruction fetches on the current hart."};
    arr[ECALL] = {"ECALL",
                  "Environment Call. Generates a synchronous environment-call exception to "
                  "request a service from the execution environment."};
    arr[EBREAK] = {"EBREAK",
                   "Environment Breakpoint. Generates a breakpoint exception to transfer "
                   "control to a debugging environment."};
    arr[CSRRW] = {"CSRRW",
                  "Atomic Read/Write CSR. Atomically swaps values in the CSR and integer "
                  "register: reads the old CSR value (zero-extended to XLEN) into rd and "
                  "writes rs1 to the CSR."};
    arr[CSRRS] = {"CSRRS",
                  "Atomic Read and Set Bits in CSR. Reads the CSR value into rd and sets any "
                  "writable CSR bit that is high in rs1 (if rs1 != x0)."};
    arr[CSRRC] = {"CSRRC",
                  "Atomic Read and Clear Bits in CSR. Reads the CSR value into rd and clears "
                  "any writable CSR bit that is high in rs1 (if rs1 != x0)."};
    arr[CSRRWI] = {"CSRRWI",
                   "Atomic Read/Write CSR Immediate. Reads the old CSR value into rd and "
                   "writes the zero-extended 5-bit uimm field to the CSR."};
    arr[CSRRSI] = {"CSRRSI",
                   "Atomic Read and Set Bits in CSR Immediate. Reads the CSR value into rd "
                   "and sets CSR bits specified by the 5-bit uimm mask (if uimm != 0)."};
    arr[CSRRCI] = {"CSRRCI",
                   "Atomic Read and Clear Bits in CSR Immediate. Reads the CSR value into rd "
                   "and clears CSR bits specified by the 5-bit uimm mask (if uimm != 0)."};
    arr[URET] = {"URET",
                 "User-mode Trap Return. Returns from a user-mode trap handler, restoring PC "
                 "from uepc."};
    arr[SRET] = {"SRET",
                 "Supervisor-mode Trap Return. Returns from a supervisor-mode trap handler, "
                 "restoring PC from sepc and privilege mode from sstatus.SPP."};
    arr[MRET] = {"MRET",
                 "Machine-mode Trap Return. Returns from a machine-mode trap handler, "
                 "restoring PC from mepc and privilege mode from mstatus.MPP."};
    arr[HFENCE_VVMA] = {"HFENCE.VVMA",
                        "Hypervisor Fence Virtual-Address Translation. Orders prior stores "
                        "before subsequent implicit VS-stage page-table accesses and "
                        "invalidates matching VS-stage address-translation entries."};
    arr[HFENCE_GVMA] = {"HFENCE.GVMA",
                        "Hypervisor Fence Guest-Physical Translation. Orders prior stores "
                        "before subsequent implicit G-stage page-table accesses and "
                        "invalidates matching guest-physical address-translation entries."};
    arr[WFI] = {"WFI",
                "Wait for Interrupt. Provides a hint that the current hart can be stalled "
                "until an interrupt might need servicing."};
    arr[SFENCE_VMA] = {"SFENCE.VMA",
                       "Fence Virtual Memory Address. Guarantees that previous stores to "
                       "page-table structures are ordered before subsequent implicit "
                       "references and flushes cached address-translation entries."};
    arr[MUL] = {"MUL",
                "Multiply. Performs an XLEN x XLEN multiplication of rs1 by rs2 and places "
                "the lower XLEN bits in destination register rd."};
    arr[MULH] = {"MULH",
                 "Multiply High Signed-Signed. Multiplies signed rs1 by signed rs2 and "
                 "places the upper XLEN bits of the 2*XLEN product in rd."};
    arr[MULHSU] = {"MULHSU",
                   "Multiply High Signed-Unsigned. Multiplies signed rs1 by unsigned rs2 and "
                   "places the upper XLEN bits of the 2*XLEN product in rd."};
    arr[MULHU] = {"MULHU",
                  "Multiply High Unsigned-Unsigned. Multiplies unsigned rs1 by unsigned rs2 "
                  "and places the upper XLEN bits of the 2*XLEN product in rd."};
    arr[DIV] = {"DIV",
                "Divide Signed. Performs signed integer division of XLEN bits by XLEN bits "
                "(rounding towards zero) and writes the quotient to rd."};
    arr[DIVU] = {"DIVU",
                 "Divide Unsigned. Performs unsigned integer division of XLEN bits by XLEN "
                 "bits and writes the quotient to rd."};
    arr[REM] = {"REM",
                "Remainder Signed. Computes the remainder of signed division of rs1 by rs2 "
                "with the sign of the dividend rs1, writing it to rd."};
    arr[REMU] = {"REMU",
                 "Remainder Unsigned. Computes the remainder of unsigned division of rs1 by "
                 "rs2, writing it to rd."};
    arr[MULW] = {"MULW",
                 "Multiply Word. Multiplies the lower 32 bits of rs1 and rs2, sign-extending "
                 "the lower 32 bits of the product to 64 bits in rd."};
    arr[DIVW] = {"DIVW",
                 "Divide Word Signed. Divides the lower 32-bit signed value in rs1 by the "
                 "lower 32-bit signed value in rs2, sign-extending the 32-bit quotient to 64 "
                 "bits in rd."};
    arr[DIVUW] = {"DIVUW",
                  "Divide Word Unsigned. Divides the lower 32-bit unsigned value in rs1 by "
                  "the lower 32-bit unsigned value in rs2, sign-extending the 32-bit quotient "
                  "to 64 bits in rd."};
    arr[REMW] = {"REMW",
                 "Remainder Word Signed. Computes the signed 32-bit remainder of dividing "
                 "rs1[31:0] by rs2[31:0], sign-extending the result to 64 bits in rd."};
    arr[REMUW] = {"REMUW",
                  "Remainder Word Unsigned. Computes the unsigned 32-bit remainder of "
                  "dividing rs1[31:0] by rs2[31:0], sign-extending the result to 64 bits in "
                  "rd."};
    arr[LR_W] = {"LR.W",
                 "Load-Reserved Word. Loads a 32-bit word from the address in rs1, "
                 "sign-extends it to XLEN bits in rd, and registers a reservation set "
                 "including the addressed word."};
    arr[SC_W] = {"SC.W",
                 "Store-Conditional Word. Conditionally writes the lower 32 bits of rs2 to "
                 "the address in rs1 if the reservation is still valid, writing 0 to rd on "
                 "success or a nonzero code on failure."};
    arr[AMOSWAP_W] = {"AMOSWAP.W",
                      "Atomic Swap Word. Atomically loads a 32-bit data value from address "
                      "rs1, sign-extends it into rd, and stores the lower 32 bits of rs2 back "
                      "to address rs1."};
    arr[AMOADD_W] = {"AMOADD.W",
                     "Atomic Add Word. Atomically loads a 32-bit word from address rs1 into "
                     "rd (sign-extended), adds rs2[31:0] to the loaded value, and stores the "
                     "sum back to address rs1."};
    arr[AMOXOR_W] = {"AMOXOR.W",
                     "Atomic XOR Word. Atomically loads a 32-bit word from address rs1 into "
                     "rd (sign-extended), XORs it with rs2[31:0], and stores the result back "
                     "to address rs1."};
    arr[AMOAND_W] = {"AMOAND.W",
                     "Atomic AND Word. Atomically loads a 32-bit word from address rs1 into "
                     "rd (sign-extended), ANDs it with rs2[31:0], and stores the result back "
                     "to address rs1."};
    arr[AMOOR_W] = {"AMOOR.W",
                    "Atomic OR Word. Atomically loads a 32-bit word from address rs1 into rd "
                    "(sign-extended), ORs it with rs2[31:0], and stores the result back to "
                    "address rs1."};
    arr[AMOMIN_W] = {"AMOMIN.W",
                     "Atomic Minimum Word (Signed). Atomically loads a 32-bit word from "
                     "address rs1 into rd (sign-extended) and stores the signed minimum with "
                     "rs2[31:0] back to address rs1."};
    arr[AMOMAX_W] = {"AMOMAX.W",
                     "Atomic Maximum Word (Signed). Atomically loads a 32-bit word from "
                     "address rs1 into rd (sign-extended) and stores the signed maximum with "
                     "rs2[31:0] back to address rs1."};
    arr[AMOMINU_W] = {"AMOMINU.W",
                      "Atomic Minimum Word (Unsigned). Atomically loads a 32-bit word from "
                      "address rs1 into rd (sign-extended) and stores the unsigned minimum "
                      "with rs2[31:0] back to address rs1."};
    arr[AMOMAXU_W] = {"AMOMAXU.W",
                      "Atomic Maximum Word (Unsigned). Atomically loads a 32-bit word from "
                      "address rs1 into rd (sign-extended) and stores the unsigned maximum "
                      "with rs2[31:0] back to address rs1."};
    arr[LR_D] = {"LR.D",
                 "Load-Reserved Doubleword. Loads a 64-bit doubleword from address rs1 into "
                 "rd and registers a reservation set including the addressed doubleword."};
    arr[SC_D] = {"SC.D",
                 "Store-Conditional Doubleword. Conditionally writes the 64-bit value in rs2 "
                 "to address rs1 if the reservation is valid, writing 0 to rd on success or "
                 "nonzero on failure."};
    arr[AMOSWAP_D] = {"AMOSWAP.D",
                      "Atomic Swap Doubleword. Atomically loads a 64-bit doubleword from "
                      "address rs1 into rd and stores rs2 back to address rs1."};
    arr[AMOADD_D] = {"AMOADD.D",
                     "Atomic Add Doubleword. Atomically loads a 64-bit doubleword from "
                     "address rs1 into rd, adds rs2, and stores the sum back to address rs1."};
    arr[AMOXOR_D] = {"AMOXOR.D",
                     "Atomic XOR Doubleword. Atomically loads a 64-bit doubleword from "
                     "address rs1 into rd, XORs with rs2, and stores the result back to "
                     "address rs1."};
    arr[AMOAND_D] = {"AMOAND.D",
                     "Atomic AND Doubleword. Atomically loads a 64-bit doubleword from "
                     "address rs1 into rd, ANDs with rs2, and stores the result back to "
                     "address rs1."};
    arr[AMOOR_D] = {"AMOOR.D",
                    "Atomic OR Doubleword. Atomically loads a 64-bit doubleword from address "
                    "rs1 into rd, ORs with rs2, and stores the result back to address rs1."};
    arr[AMOMIN_D] = {"AMOMIN.D",
                     "Atomic Minimum Doubleword (Signed). Atomically loads a 64-bit "
                     "doubleword from address rs1 into rd and stores the signed minimum with "
                     "rs2 back to address rs1."};
    arr[AMOMAX_D] = {"AMOMAX.D",
                     "Atomic Maximum Doubleword (Signed). Atomically loads a 64-bit "
                     "doubleword from address rs1 into rd and stores the signed maximum with "
                     "rs2 back to address rs1."};
    arr[AMOMINU_D] = {"AMOMINU.D",
                      "Atomic Minimum Doubleword (Unsigned). Atomically loads a 64-bit "
                      "doubleword from address rs1 into rd and stores the unsigned minimum "
                      "with rs2 back to address rs1."};
    arr[AMOMAXU_D] = {"AMOMAXU.D",
                      "Atomic Maximum Doubleword (Unsigned). Atomically loads a 64-bit "
                      "doubleword from address rs1 into rd and stores the unsigned maximum "
                      "with rs2 back to address rs1."};
    arr[FLW] = {"FLW",
                "Floating-Point Load Word. Loads a single-precision floating-point value "
                "from memory address (rs1 + sign-extended 12-bit offset) into floating-point "
                "register rd (NaN-boxed when FLEN > 32)."};
    arr[FSW] = {"FSW",
                "Floating-Point Store Word. Stores a single-precision value from "
                "floating-point register rs2 to memory at address (rs1 + sign-extended "
                "12-bit offset)."};
    arr[FSD] = {"FSD",
                "Floating-Point Store Doubleword. Stores a double-precision value from "
                "floating-point register rs2 to memory at address (rs1 + sign-extended "
                "12-bit offset)."};
    arr[FLD] = {"FLD",
                "Floating-Point Load Doubleword. Loads a double-precision floating-point "
                "value from memory address (rs1 + sign-extended 12-bit offset) into "
                "floating-point register rd."};
    arr[FMADD_S] = {"FMADD.S",
                    "Floating-Point Fused Multiply-Add Single. Computes (rs1 * rs2) + rs3 "
                    "with a single rounding step and writes the single-precision result to "
                    "rd."};
    arr[FMSUB_S] = {"FMSUB.S",
                    "Floating-Point Fused Multiply-Subtract Single. Computes (rs1 * rs2) - "
                    "rs3 with a single rounding step and writes the single-precision result "
                    "to rd."};
    arr[FNMSUB_S] = {"FNMSUB.S",
                     "Floating-Point Negated Multiply-Subtract Single. Computes -(rs1 * rs2) "
                     "+ rs3 with a single rounding step and writes the single-precision "
                     "result to rd."};
    arr[FNMADD_S] = {"FNMADD.S",
                     "Floating-Point Negated Multiply-Add Single. Computes -(rs1 * rs2) - "
                     "rs3 with a single rounding step and writes the single-precision result "
                     "to rd."};
    arr[FADD_S] = {"FADD.S",
                   "Floating-Point Add Single. Adds IEEE-754 single-precision values in rs1 "
                   "and rs2 and writes the rounded result to rd."};
    arr[FSUB_S] = {"FSUB.S",
                   "Floating-Point Subtract Single. Subtracts single-precision rs2 from rs1 "
                   "and writes the rounded result to rd."};
    arr[FMUL_S] = {"FMUL.S",
                   "Floating-Point Multiply Single. Multiplies single-precision values in rs1 "
                   "and rs2 and writes the rounded result to rd."};
    arr[FDIV_S] = {"FDIV.S",
                   "Floating-Point Divide Single. Divides single-precision rs1 by rs2 and "
                   "writes the rounded result to rd."};
    arr[FSQRT_S] = {"FSQRT.S",
                    "Floating-Point Square Root Single. Computes the square root of "
                    "single-precision rs1 and writes the rounded result to rd."};
    arr[FSGNJ_S] = {"FSGNJ.S",
                    "Floating-Point Sign-Inject Single. Produces a result with the magnitude "
                    "of rs1 and the sign bit of rs2 in rd."};
    arr[FSGNJN_S] = {"FSGNJN.S",
                     "Floating-Point Sign-Inject Negate Single. Produces a result with the "
                     "magnitude of rs1 and the negated sign bit of rs2 in rd."};
    arr[FSGNJX_S] = {"FSGNJX.S",
                     "Floating-Point Sign-Inject XOR Single. Produces a result with the "
                     "magnitude of rs1 and a sign bit equal to the XOR of the sign bits of "
                     "rs1 and rs2."};
    arr[FMIN_S] = {"FMIN.S",
                   "Floating-Point Minimum Single. Writes the smaller of single-precision "
                   "operands rs1 and rs2 to rd."};
    arr[FMAX_S] = {"FMAX.S",
                   "Floating-Point Maximum Single. Writes the larger of single-precision "
                   "operands rs1 and rs2 to rd."};
    arr[FCVT_W_S] = {"FCVT.W.S",
                     "Floating-Point Convert Single to Word. Converts a single-precision "
                     "value in floating-point register rs1 to a signed 32-bit integer (sign-"
                     "extended to XLEN) in integer register rd."};
    arr[FCVT_WU_S] = {"FCVT.WU.S",
                      "Floating-Point Convert Single to Unsigned Word. Converts a single-"
                      "precision value in rs1 to an unsigned 32-bit integer (sign-extended to "
                      "XLEN) in integer register rd."};
    arr[FMV_X_W] = {"FMV.X.W",
                    "Floating-Point Move Word to Integer. Moves the lower 32 bits of "
                    "floating-point register rs1 (sign-extended to XLEN) to integer register "
                    "rd without bit conversion."};
    arr[FEQ_S] = {"FEQ.S",
                  "Floating-Point Compare Equal Single. Performs a quiet comparison: writes 1 "
                  "to integer register rd if rs1 equals rs2, else 0."};
    arr[FLT_S] = {"FLT.S",
                  "Floating-Point Compare Less Than Single. Performs a signaling comparison: "
                  "writes 1 to integer register rd if rs1 < rs2, else 0."};
    arr[FLE_S] = {"FLE.S",
                  "Floating-Point Compare Less Than or Equal Single. Performs a signaling "
                  "comparison: writes 1 to integer register rd if rs1 <= rs2, else 0."};
    arr[FCLASS_S] = {"FCLASS.S",
                     "Floating-Point Classify Single. Examines the value in floating-point "
                     "register rs1 and writes a 10-bit mask to integer register rd indicating "
                     "the IEEE-754 class of the floating-point number."};
    arr[FCVT_S_W] = {"FCVT.S.W",
                     "Floating-Point Convert Word to Single. Converts a signed 32-bit integer "
                     "in integer register rs1 into a single-precision floating-point number "
                     "in rd."};
    arr[FCVT_S_WU] = {"FCVT.S.WU",
                      "Floating-Point Convert Unsigned Word to Single. Converts an unsigned "
                      "32-bit integer in integer register rs1 into a single-precision "
                      "floating-point number in rd."};
    arr[FMV_W_X] = {"FMV.W.X",
                    "Floating-Point Move Integer to Word. Moves the lower 32 bits from "
                    "integer register rs1 into floating-point register rd (NaN-boxed when "
                    "FLEN > 32)."};
    arr[FCVT_L_S] = {"FCVT.L.S",
                     "Floating-Point Convert Single to Long. Converts a single-precision "
                     "value in rs1 to a signed 64-bit integer in integer register rd."};
    arr[FCVT_LU_S] = {"FCVT.LU.S",
                      "Floating-Point Convert Single to Unsigned Long. Converts a single-"
                      "precision value in rs1 to an unsigned 64-bit integer in integer "
                      "register rd."};
    arr[FCVT_S_L] = {"FCVT.S.L",
                     "Floating-Point Convert Long to Single. Converts a signed 64-bit integer "
                     "in integer register rs1 to a single-precision value in rd."};
    arr[FCVT_S_LU] = {"FCVT.S.LU",
                      "Floating-Point Convert Unsigned Long to Single. Converts an unsigned "
                      "64-bit integer in integer register rs1 to a single-precision value in "
                      "rd."};
    arr[FMADD_D] = {"FMADD.D",
                    "Floating-Point Fused Multiply-Add Double. Computes (rs1 * rs2) + rs3 "
                    "with a single rounding step and writes the double-precision result to "
                    "rd."};
    arr[FMSUB_D] = {"FMSUB.D",
                    "Floating-Point Fused Multiply-Subtract Double. Computes (rs1 * rs2) - "
                    "rs3 with a single rounding step and writes the double-precision result "
                    "to rd."};
    arr[FNMSUB_D] = {"FNMSUB.D",
                     "Floating-Point Negated Multiply-Subtract Double. Computes -(rs1 * rs2) "
                     "+ rs3 with a single rounding step and writes the double-precision "
                     "result to rd."};
    arr[FNMADD_D] = {"FNMADD.D",
                     "Floating-Point Negated Multiply-Add Double. Computes -(rs1 * rs2) - "
                     "rs3 with a single rounding step and writes the double-precision result "
                     "to rd."};
    arr[FADD_D] = {"FADD.D",
                   "Floating-Point Add Double. Adds IEEE-754 double-precision values in rs1 "
                   "and rs2 and writes the rounded result to rd."};
    arr[FSUB_D] = {"FSUB.D",
                   "Floating-Point Subtract Double. Subtracts double-precision rs2 from rs1 "
                   "and writes the rounded result to rd."};
    arr[FMUL_D] = {"FMUL.D",
                   "Floating-Point Multiply Double. Multiplies double-precision values in rs1 "
                   "and rs2 and writes the rounded result to rd."};
    arr[FDIV_D] = {"FDIV.D",
                   "Floating-Point Divide Double. Divides double-precision rs1 by rs2 and "
                   "writes the rounded result to rd."};
    arr[FSQRT_D] = {"FSQRT.D",
                    "Floating-Point Square Root Double. Computes the square root of "
                    "double-precision rs1 and writes the rounded result to rd."};
    arr[FSGNJ_D] = {"FSGNJ.D",
                    "Floating-Point Sign-Inject Double. Produces a double-precision result "
                    "with the magnitude of rs1 and the sign bit of rs2 in rd."};
    arr[FSGNJN_D] = {"FSGNJN.D",
                     "Floating-Point Sign-Inject Negate Double. Produces a double-precision "
                     "result with the magnitude of rs1 and the negated sign bit of rs2 in rd."};
    arr[FSGNJX_D] = {"FSGNJX.D",
                     "Floating-Point Sign-Inject XOR Double. Produces a double-precision "
                     "result with the magnitude of rs1 and the XOR of sign bits of rs1 and "
                     "rs2."};
    arr[FMIN_D] = {"FMIN.D",
                   "Floating-Point Minimum Double. Writes the smaller of double-precision "
                   "operands rs1 and rs2 to rd."};
    arr[FMAX_D] = {"FMAX.D",
                   "Floating-Point Maximum Double. Writes the larger of double-precision "
                   "operands rs1 and rs2 to rd."};
    arr[FCVT_S_D] = {"FCVT.S.D",
                     "Floating-Point Convert Double to Single. Rounds a double-precision "
                     "value in rs1 to single-precision in rd."};
    arr[FCVT_D_S] = {"FCVT.D.S",
                     "Floating-Point Convert Single to Double. Converts a single-precision "
                     "value in rs1 to double-precision in rd."};
    arr[FEQ_D] = {"FEQ.D",
                  "Floating-Point Compare Equal Double. Performs a quiet comparison: writes 1 "
                  "to integer register rd if rs1 equals rs2, else 0."};
    arr[FLT_D] = {"FLT.D",
                  "Floating-Point Compare Less Than Double. Performs a signaling comparison: "
                  "writes 1 to integer register rd if rs1 < rs2, else 0."};
    arr[FLE_D] = {"FLE.D",
                  "Floating-Point Compare Less Than or Equal Double. Performs a signaling "
                  "comparison: writes 1 to integer register rd if rs1 <= rs2, else 0."};
    arr[FCLASS_D] = {"FCLASS.D",
                     "Floating-Point Classify Double. Examines double-precision value in rs1 "
                     "and writes a 10-bit classification mask to integer register rd."};
    arr[FCVT_W_D] = {"FCVT.W.D",
                     "Floating-Point Convert Double to Word. Converts double-precision value "
                     "in rs1 to a signed 32-bit integer (sign-extended to XLEN) in rd."};
    arr[FCVT_WU_D] = {"FCVT.WU.D",
                      "Floating-Point Convert Double to Unsigned Word. Converts double-"
                      "precision value in rs1 to an unsigned 32-bit integer (sign-extended to "
                      "XLEN) in rd."};
    arr[FCVT_D_W] = {"FCVT.D.W",
                     "Floating-Point Convert Word to Double. Converts a signed 32-bit integer "
                     "in GPR rs1 to a double-precision value in rd."};
    arr[FCVT_D_WU] = {"FCVT.D.WU",
                      "Floating-Point Convert Unsigned Word to Double. Converts an unsigned "
                      "32-bit integer in GPR rs1 to a double-precision value in rd."};
    arr[FMV_X_D] = {"FMV.X.D",
                    "Floating-Point Move Double to Integer. Moves the 64-bit IEEE-754 "
                    "encoding in floating-point register rs1 directly to integer register rd."};
    arr[FMV_D_X] = {"FMV.D.X",
                    "Floating-Point Move Integer to Double. Moves the 64-bit value in integer "
                    "register rs1 directly to double-precision floating-point register rd."};
    arr[FCVT_L_D] = {"FCVT.L.D",
                     "Floating-Point Convert Double to Long. Converts double-precision value "
                     "in rs1 to a signed 64-bit integer in GPR rd."};
    arr[FCVT_LU_D] = {"FCVT.LU.D",
                      "Floating-Point Convert Double to Unsigned Long. Converts double-"
                      "precision value in rs1 to an unsigned 64-bit integer in GPR rd."};
    arr[FCVT_D_L] = {"FCVT.D.L",
                     "Floating-Point Convert Long to Double. Converts a signed 64-bit integer "
                     "in GPR rs1 to a double-precision value in rd."};
    arr[FCVT_D_LU] = {"FCVT.D.LU",
                      "Floating-Point Convert Unsigned Long to Double. Converts an unsigned "
                      "64-bit integer in GPR rs1 to a double-precision value in rd."};

    // B-Extension instructions (Zba, Zbb, Zbc, Zbs)
    arr[SH1ADD] = {"SH1ADD",
                   "Shift Left by 1 and Add. Shifts rs1 left by 1 bit and adds it to rs2, "
                   "writing the result to rd."};
    arr[SH2ADD] = {"SH2ADD",
                   "Shift Left by 2 and Add. Shifts rs1 left by 2 bits and adds it to rs2, "
                   "writing the result to rd."};
    arr[SH3ADD] = {"SH3ADD",
                   "Shift Left by 3 and Add. Shifts rs1 left by 3 bits and adds it to rs2, "
                   "writing the result to rd."};
    arr[ANDN] = {"ANDN",
                 "Bitwise AND-NOT. Performs bitwise AND of rs1 and the bitwise inversion of "
                 "rs2, writing the result to rd."};
    arr[ORN] = {"ORN",
                "Bitwise OR-NOT. Performs bitwise OR of rs1 and the bitwise inversion of rs2, "
                "writing the result to rd."};
    arr[XNOR] = {"XNOR",
                 "Bitwise Exclusive-NOR. Performs bitwise XNOR of rs1 and rs2, writing the "
                 "result to rd."};
    arr[CLZ] = {"CLZ",
                "Count Leading Zeros. Counts the number of 0 bits before the first 1 bit, "
                "starting from the most-significant bit of rs1."};
    arr[CTZ] = {"CTZ",
                "Count Trailing Zeros. Counts the number of 0 bits before the first 1 bit, "
                "starting from the least-significant bit of rs1."};
    arr[CPOP] = {"CPOP",
                 "Count Population. Counts the number of 1 bits (set bits) in register rs1."};
    arr[MIN] = {"MIN", "Minimum Signed. Writes the smaller of signed integers rs1 and rs2 to rd."};
    arr[MAX] = {"MAX", "Maximum Signed. Writes the larger of signed integers rs1 and rs2 to rd."};
    arr[MINU] = {"MINU",
                 "Minimum Unsigned. Writes the smaller of unsigned integers rs1 and rs2 to "
                 "rd."};
    arr[MAXU] = {"MAXU",
                 "Maximum Unsigned. Writes the larger of unsigned integers rs1 and rs2 to rd."};
    arr[SEXT_B] = {"SEXT_B",
                   "Sign-Extend Byte. Sign-extends the least-significant 8 bits of rs1 to "
                   "XLEN bits in rd."};
    arr[SEXT_H] = {"SEXT_H",
                   "Sign-Extend Halfword. Sign-extends the least-significant 16 bits of rs1 "
                   "to XLEN bits in rd."};
    arr[ZEXT_H] = {"ZEXT_H",
                   "Zero-Extend Halfword. Zero-extends the least-significant 16 bits of rs1 "
                   "to XLEN bits in rd."};
    arr[ROL] = {"ROL",
                "Rotate Left. Rotates rs1 left by the shift amount in the lower log2(XLEN) "
                "bits of rs2."};
    arr[ROR] = {"ROR",
                "Rotate Right. Rotates rs1 right by the shift amount in the lower log2(XLEN) "
                "bits of rs2."};
    arr[RORI] = {"RORI",
                 "Rotate Right Immediate. Rotates rs1 right by the immediate shift amount."};
    arr[CLMUL] = {"CLMUL",
                  "Carry-less Multiply (Low). Computes the carry-less product of rs1 and rs2 "
                  "and writes the lower XLEN bits to rd."};
    arr[CLMULH] = {"CLMULH",
                   "Carry-less Multiply (High). Computes the carry-less product of rs1 and "
                   "rs2 and writes the upper XLEN bits to rd."};
    arr[CLMULR] = {"CLMULR",
                   "Carry-less Multiply (Reversed). Computes the carry-less product of rs1 "
                   "and rs2 and writes bits 2*XLEN-2 down to XLEN-1 to rd."};
    arr[BSET] = {"BSET",
                 "Single-Bit Set (Register). Returns rs1 with a single bit set at the index "
                 "specified by rs2."};
    arr[BSETI] = {"BSETI",
                  "Single-Bit Set (Immediate). Returns rs1 with a single bit set at the "
                  "index specified by shamt."};
    arr[BCLR] = {"BCLR",
                 "Single-Bit Clear (Register). Returns rs1 with a single bit cleared at the "
                 "index specified by rs2."};
    arr[BCLRI] = {"BCLRI",
                  "Single-Bit Clear (Immediate). Returns rs1 with a single bit cleared at "
                  "the index specified by shamt."};
    arr[BINV] = {"BINV",
                 "Single-Bit Invert (Register). Returns rs1 with a single bit inverted at the "
                 "index specified by rs2."};
    arr[BINVI] = {"BINVI",
                  "Single-Bit Invert (Immediate). Returns rs1 with a single bit inverted at "
                  "the index specified by shamt."};
    arr[BEXT] = {"BEXT",
                 "Single-Bit Extract (Register). Extracts a single bit from rs1 at the index "
                 "specified by rs2 and places it in rd[0]."};
    arr[BEXTI] = {"BEXTI",
                  "Single-Bit Extract (Immediate). Extracts a single bit from rs1 at the "
                  "index specified by shamt and places it in rd[0]."};
    arr[ORC_B] = {"ORC_B",
                  "Bitwise OR-Combine (Byte Granule). Within each byte of rs1, sets all bits "
                  "of the result byte to 1 if any bit in that source byte is 1, else 0."};
    arr[REV8] = {"REV8",
                 "Byte-Reverse Register. Reverses the order of bytes in rs1 (endianness "
                 "conversion)."};
    arr[PACK] = {"PACK", "Pack Low Halves. Packs the lower XLEN/2 bits of rs1 and rs2 into rd."};

    arr[ADD_UW] = {"ADD_UW",
                   "Add Unsigned Word. Zero-extends the lower 32 bits of rs1 to 64 bits and "
                   "adds it to rs2."};
    arr[SLLI_UW] = {"SLLI_UW",
                    "Shift Left Unsigned Word Immediate. Zero-extends the lower 32 bits of "
                    "rs1 to 64 bits and shifts left by shamt."};
    arr[SH1ADD_UW] = {"SH1ADD_UW",
                      "Shift Left by 1 and Add Unsigned Word. Zero-extends rs1[31:0], shifts "
                      "left by 1, and adds to rs2."};
    arr[SH2ADD_UW] = {"SH2ADD_UW",
                      "Shift Left by 2 and Add Unsigned Word. Zero-extends rs1[31:0], shifts "
                      "left by 2, and adds to rs2."};
    arr[SH3ADD_UW] = {"SH3ADD_UW",
                      "Shift Left by 3 and Add Unsigned Word. Zero-extends rs1[31:0], shifts "
                      "left by 3, and adds to rs2."};

    arr[CLZW] = {"CLZW",
                 "Count Leading Zeros Word. Counts leading zero bits in the lower 32 bits of "
                 "rs1."};
    arr[CTZW] = {"CTZW",
                 "Count Trailing Zeros Word. Counts trailing zero bits in the lower 32 bits "
                 "of rs1."};
    arr[CPOPW] = {"CPOPW", "Count Population Word. Counts set bits in the lower 32 bits of rs1."};
    arr[ROLW] = {"ROLW",
                 "Rotate Left Word. Rotates the lower 32 bits of rs1 left by rs2[4:0] and "
                 "sign-extends to 64 bits."};
    arr[RORW] = {"RORW",
                 "Rotate Right Word. Rotates the lower 32 bits of rs1 right by rs2[4:0] and "
                 "sign-extends to 64 bits."};
    arr[RORIW] = {"RORIW",
                  "Rotate Right Word Immediate. Rotates the lower 32 bits of rs1 right by "
                  "shamt[4:0] and sign-extends to 64 bits."};
    arr[PACKW] = {"PACKW",
                  "Pack Word. Packs the lower 16 bits of rs1 and rs2 into a 32-bit word and "
                  "sign-extends to 64 bits in rd."};

    // Custom Function Unit
    arr[CFU] = {"CFU",
                "Custom Function Unit instruction (CUSTOM_0 opcode space). Dispatches an "
                "application-specific coprocessor/accelerator operation parameterized by "
                "funct3 (function select) and funct7 (custom sub-operation). Consumes scalar "
                "integer registers rs1 and rs2, returning the hardware computation result in "
                "rd with custom execution latency and pipeline stall cycles."};

    // Zicond
    arr[CZERO_EQZ] = {"CZERO.EQZ",
                      "Conditional Zero if Equal to Zero. Moves 0 to rd if the condition "
                      "register rs2 equals 0; otherwise moves rs1 to rd."};
    arr[CZERO_NEZ] = {"CZERO.NEZ",
                      "Conditional Zero if Nonzero. Moves 0 to rd if the condition register "
                      "rs2 is nonzero; otherwise moves rs1 to rd."};

    // Zicbom / Zicboz / Zicbop
    arr[CBO_ZERO] = {"CBO.ZERO",
                     "Cache-Block Zero. Writes zeros to the entire cache block containing "
                     "the effective address in rs1."};
    arr[CBO_CLEAN] = {"CBO.CLEAN",
                      "Cache-Block Clean. Performs a writeback of dirty data in the cache "
                      "block containing the effective address in rs1."};
    arr[CBO_FLUSH] = {"CBO.FLUSH",
                      "Cache-Block Flush. Writes back dirty data and invalidates the cache "
                      "block containing the effective address in rs1."};
    arr[CBO_INVAL] = {"CBO.INVAL",
                      "Cache-Block Invalidate. Deallocates the cache block containing the "
                      "effective address in rs1."};
    arr[PREFETCH_I] = {"PREFETCH.I",
                       "Cache-Block Prefetch for Instruction Fetch. Hints to hardware that "
                       "the cache block at effective address (rs1 + offset) will be fetched."};
    arr[PREFETCH_R] = {"PREFETCH.R",
                       "Cache-Block Prefetch for Data Read. Hints to hardware that the cache "
                       "block at effective address (rs1 + offset) will be read."};
    arr[PREFETCH_W] = {"PREFETCH.W",
                       "Cache-Block Prefetch for Data Write. Hints to hardware that the cache "
                       "block at effective address (rs1 + offset) will be written."};

    // Svinval
    arr[SINVAL_VMA] = {"SINVAL.VMA",
                       "Supervisor Invalidate Virtual Memory Address. Invalidates cached "
                       "address-translation entries matching virtual address rs1 and ASID "
                       "rs2."};
    arr[SFENCE_W_INVAL] = {"SFENCE.W.INVAL",
                           "Supervisor Fence Write Invalidate. Orders prior stores to page "
                           "tables before subsequent SINVAL.VMA instructions."};
    arr[SFENCE_INVAL_IR] = {"SFENCE.INVAL.IR",
                            "Supervisor Fence Invalidate Instruction/Read. Guarantees that "
                            "prior SINVAL.VMA instructions complete before subsequent "
                            "implicit page-table accesses."};

    // Zfhmin (minimal half-precision floating-point)
    arr[FLH] = {"FLH", "Load Half-Precision. Loads a 16-bit floating-point encoding into f[rd]."};
    arr[FSH] = {"FSH", "Store Half-Precision. Stores the low 16 bits of f[rs2] to memory."};
    arr[FMV_X_H] = {"FMV.X.H",
                    "Move Half-Precision to Integer. Copies the 16-bit encoding to x[rd]."};
    arr[FMV_H_X] = {"FMV.H.X", "Move Integer to Half-Precision. Copies x[rs1][15:0] to f[rd]."};
    arr[FCVT_S_H] = {"FCVT.S.H", "Convert Half to Single. Converts f[rs1] to single precision."};
    arr[FCVT_H_S] = {"FCVT.H.S", "Convert Single to Half. Rounds f[rs1] to half precision."};
    arr[FCVT_D_H] = {"FCVT.D.H", "Convert Half to Double. Converts f[rs1] to double precision."};
    arr[FCVT_H_D] = {"FCVT.H.D", "Convert Double to Half. Rounds f[rs1] to half precision."};

    // Zfa (additional floating-point instructions)
    arr[FLI_S] = {"FLI.S",
                  "Load Immediate Single. Loads the single-precision constant selected by rs1."};
    arr[FLI_D] = {"FLI.D",
                  "Load Immediate Double. Loads the double-precision constant selected by rs1."};
    arr[FMINM_S] = {
        "FMINM.S",
        "Minimum Number Single. Returns the minimumNumber of two single-precision operands."};
    arr[FMAXM_S] = {
        "FMAXM.S",
        "Maximum Number Single. Returns the maximumNumber of two single-precision operands."};
    arr[FMINM_D] = {
        "FMINM.D",
        "Minimum Number Double. Returns the minimumNumber of two double-precision operands."};
    arr[FMAXM_D] = {
        "FMAXM.D",
        "Maximum Number Double. Returns the maximumNumber of two double-precision operands."};
    arr[FLEQ_S] = {"FLEQ.S",
                   "Quiet Less Than or Equal Single. Compares single-precision operands without "
                   "signaling on quiet NaNs."};
    arr[FLTQ_S] = {"FLTQ.S",
                   "Quiet Less Than Single. Compares single-precision operands without signaling "
                   "on quiet NaNs."};
    arr[FLEQ_D] = {"FLEQ.D",
                   "Quiet Less Than or Equal Double. Compares double-precision operands without "
                   "signaling on quiet NaNs."};
    arr[FLTQ_D] = {"FLTQ.D",
                   "Quiet Less Than Double. Compares double-precision operands without signaling "
                   "on quiet NaNs."};
    arr[FROUND_S] = {"FROUND.S",
                     "Round to Integer Single. Rounds a single-precision value to an integral "
                     "value using the encoded rounding mode."};
    arr[FROUNDNX_S] = {"FROUNDNX.S",
                       "Round to Integer Exact Single. Rounds a single-precision value and sets "
                       "inexact when required."};
    arr[FROUND_D] = {"FROUND.D",
                     "Round to Integer Double. Rounds a double-precision value to an integral "
                     "value using the encoded rounding mode."};
    arr[FROUNDNX_D] = {"FROUNDNX.D",
                       "Round to Integer Exact Double. Rounds a double-precision value and sets "
                       "inexact when required."};
    arr[FCVTMOD_W_D] = {"FCVTMOD.W.D",
                        "Convert Double to Word Modulo. Rounds toward zero, keeps the low 32 bits, "
                        "then sign-extends to XLEN."};

    // RVA23 scalar may-be and wait-on-reservation-set instructions.
    arr[MOP] = {"MOP",
                "May-Be-Operation. Unless redefined by another extension, writes zero to x[rd] and "
                "has no source-to-destination dependency."};
    arr[WRS_NTO] = {"WRS.NTO",
                    "Wait on Reservation Set, No Time-Out. May stall while the reservation set is "
                    "valid and no pending interrupt is observed."};
    arr[WRS_STO] = {"WRS.STO",
                    "Wait on Reservation Set, Short Time-Out. Like WRS.NTO, with a bounded, "
                    "implementation-defined short stall timeout."};

    // Hypervisor virtual-machine explicit memory accesses.
    arr[HLV_B] = {"HLV.B",
                  "Hypervisor Load Byte. Loads a signed byte from guest-physical memory using the "
                  "effective VS/VU privilege."};
    arr[HLV_BU] = {"HLV.BU",
                   "Hypervisor Load Byte Unsigned. Loads an unsigned byte from guest-physical "
                   "memory using the effective VS/VU privilege."};
    arr[HLV_H] = {
        "HLV.H",
        "Hypervisor Load Halfword. Loads and sign-extends a halfword from guest-physical memory."};
    arr[HLV_HU] = {"HLV.HU",
                   "Hypervisor Load Halfword Unsigned. Loads a zero-extended halfword from "
                   "guest-physical memory."};
    arr[HLV_W] = {
        "HLV.W", "Hypervisor Load Word. Loads and sign-extends a word from guest-physical memory."};
    arr[HLV_WU] = {
        "HLV.WU",
        "Hypervisor Load Word Unsigned. Loads a zero-extended word from guest-physical memory."};
    arr[HLV_D] = {"HLV.D",
                  "Hypervisor Load Doubleword. Loads a doubleword from guest-physical memory."};
    arr[HLVX_HU] = {"HLVX.HU",
                    "Hypervisor Load Execute Halfword Unsigned. Loads using execute permission and "
                    "reports load faults."};
    arr[HLVX_WU] = {"HLVX.WU",
                    "Hypervisor Load Execute Word Unsigned. Loads using execute permission and "
                    "reports load faults."};
    arr[HSV_B] = {"HSV.B",
                  "Hypervisor Store Byte. Stores one byte to guest-physical memory using the "
                  "effective VS/VU privilege."};
    arr[HSV_H] = {"HSV.H",
                  "Hypervisor Store Halfword. Stores a halfword to guest-physical memory."};
    arr[HSV_W] = {"HSV.W", "Hypervisor Store Word. Stores a word to guest-physical memory."};
    arr[HSV_D] = {"HSV.D",
                  "Hypervisor Store Doubleword. Stores a doubleword to guest-physical memory."};
}

}  // namespace detail

namespace {

auto get_description(OperationId op_id) -> std::pair<std::string_view, std::string_view> {
    static constexpr std::pair<std::string_view, std::string_view> kDefaultDesc = {
        "UNKNOWN",
        "Instruction not explicitly detailed or custom extension opcode. Verify against compiler "
        "specification."};

    static const auto kOpDescriptions = []() -> detail::OpDescTable {
        detail::OpDescTable arr;
        arr.fill(kDefaultDesc);
        detail::populate_scalar_descriptions(arr);
        detail::populate_vector_descriptions(arr);
        return arr;
    }();

    auto const idx = static_cast<size_t>(op_id);
    if (idx < kOpDescriptions.size()) {
        return kOpDescriptions[idx];
    }
    return kDefaultDesc;
}

auto get_spec_formula(OperationId op_id) -> std::string_view {
    switch (op_id) {
        case LUI:
            return "x[rd] = sext(imm[31:12] << 12)";
        case AUIPC:
            return "x[rd] = pc + sext(imm[31:12] << 12)";
        case JAL:
            return "x[rd] = pc + 4; pc = pc + sext(imm[20:1])";
        case JALR:
            return "t = pc + 4; pc = (x[rs1] + sext(imm[11:0])) & ~1; x[rd] = t";
        case BEQ:
            return "if (x[rs1] == x[rs2]) pc += sext(imm[12:1])";
        case BNE:
            return "if (x[rs1] != x[rs2]) pc += sext(imm[12:1])";
        case BLT:
            return "if (x[rs1] <_s x[rs2]) pc += sext(imm[12:1])";
        case BGE:
            return "if (x[rs1] >=_s x[rs2]) pc += sext(imm[12:1])";
        case BLTU:
            return "if (x[rs1] <_u x[rs2]) pc += sext(imm[12:1])";
        case BGEU:
            return "if (x[rs1] >=_u x[rs2]) pc += sext(imm[12:1])";
        case LB:
            return "x[rd] = sext(M[x[rs1] + sext(imm[11:0])][7:0])";
        case LH:
            return "x[rd] = sext(M[x[rs1] + sext(imm[11:0])][15:0])";
        case LW:
            return "x[rd] = sext(M[x[rs1] + sext(imm[11:0])][31:0])";
        case LD:
            return "x[rd] = M[x[rs1] + sext(imm[11:0])][63:0]";
        case LBU:
            return "x[rd] = zext(M[x[rs1] + sext(imm[11:0])][7:0])";
        case LHU:
            return "x[rd] = zext(M[x[rs1] + sext(imm[11:0])][15:0])";
        case LWU:
            return "x[rd] = zext(M[x[rs1] + sext(imm[11:0])][31:0])";
        case SB:
            return "M[x[rs1] + sext(imm[11:0])][7:0] = x[rs2][7:0]";
        case SH:
            return "M[x[rs1] + sext(imm[11:0])][15:0] = x[rs2][15:0]";
        case SW:
            return "M[x[rs1] + sext(imm[11:0])][31:0] = x[rs2][31:0]";
        case SD:
            return "M[x[rs1] + sext(imm[11:0])][63:0] = x[rs2][63:0]";
        case ADDI:
            return "x[rd] = x[rs1] + sext(imm[11:0])";
        case SLTI:
            return "x[rd] = (x[rs1] <_s sext(imm[11:0])) ? 1 : 0";
        case SLTIU:
            return "x[rd] = (x[rs1] <_u sext(imm[11:0])) ? 1 : 0";
        case XORI:
            return "x[rd] = x[rs1] ^ sext(imm[11:0])";
        case ORI:
            return "x[rd] = x[rs1] | sext(imm[11:0])";
        case ANDI:
            return "x[rd] = x[rs1] & sext(imm[11:0])";
        case SLLI:
            return "x[rd] = x[rs1] << shamt";
        case SRLI:
            return "x[rd] = x[rs1] >>_u shamt";
        case SRAI:
            return "x[rd] = x[rs1] >>_s shamt";
        case ADDIW:
            return "x[rd] = sext((x[rs1] + sext(imm[11:0]))[31:0])";
        case SLLIW:
            return "x[rd] = sext((x[rs1][31:0] << shamt[4:0])[31:0])";
        case SRLIW:
            return "x[rd] = sext(x[rs1][31:0] >>_u shamt[4:0])";
        case SRAIW:
            return "x[rd] = sext(x[rs1][31:0] >>_s shamt[4:0])";
        case ADD:
            return "x[rd] = x[rs1] + x[rs2]";
        case SUB:
            return "x[rd] = x[rs1] - x[rs2]";
        case SLL:
            return "x[rd] = x[rs1] << x[rs2][log2(XLEN)-1:0]";
        case SLT:
            return "x[rd] = (x[rs1] <_s x[rs2]) ? 1 : 0";
        case SLTU:
            return "x[rd] = (x[rs1] <_u x[rs2]) ? 1 : 0";
        case XOR:
            return "x[rd] = x[rs1] ^ x[rs2]";
        case SRL:
            return "x[rd] = x[rs1] >>_u x[rs2][log2(XLEN)-1:0]";
        case SRA:
            return "x[rd] = x[rs1] >>_s x[rs2][log2(XLEN)-1:0]";
        case OR:
            return "x[rd] = x[rs1] | x[rs2]";
        case AND:
            return "x[rd] = x[rs1] & x[rs2]";
        case ADDW:
            return "x[rd] = sext((x[rs1] + x[rs2])[31:0])";
        case SUBW:
            return "x[rd] = sext((x[rs1] - x[rs2])[31:0])";
        case SLLW:
            return "x[rd] = sext((x[rs1][31:0] << x[rs2][4:0])[31:0])";
        case SRLW:
            return "x[rd] = sext(x[rs1][31:0] >>_u x[rs2][4:0])";
        case SRAW:
            return "x[rd] = sext(x[rs1][31:0] >>_s x[rs2][4:0])";
        case FENCE:
            return "order(pred_ops, succ_ops)";
        case FENCE_I:
            return "order(prior_explicit_memory_accesses, "
                   "subsequent_instruction_fetches_on_this_hart)";
        case ECALL:
            return "RaiseException(EnvironmentCall)";
        case EBREAK:
            return "RaiseException(Breakpoint)";
        case CSRRW:
            return "if (rd != x0) x[rd] = CSRs[csr]; CSRs[csr] = x[rs1]";
        case CSRRS:
            return "t = CSRs[csr]; x[rd] = t; if (rs1 != x0) CSRs[csr] = t | x[rs1]";
        case CSRRC:
            return "t = CSRs[csr]; x[rd] = t; if (rs1 != x0) CSRs[csr] = t & ~x[rs1]";
        case CSRRWI:
            return "if (rd != x0) x[rd] = CSRs[csr]; CSRs[csr] = zext(uimm[4:0])";
        case CSRRSI:
            return "t = CSRs[csr]; x[rd] = t; if (uimm != 0) CSRs[csr] = t | zext(uimm[4:0])";
        case CSRRCI:
            return "t = CSRs[csr]; x[rd] = t; if (uimm != 0) CSRs[csr] = t & ~zext(uimm[4:0])";
        case MRET:
            return "PC = mepc; priv = mstatus.MPP; mstatus.MIE = mstatus.MPIE";
        case SRET:
            return "PC = sepc; priv = sstatus.SPP; sstatus.SIE = sstatus.SPIE";
        case HFENCE_VVMA:
            return "order(prior_stores, later_vs_stage_page_table_accesses); invalidate(vaddr, "
                   "asid)";
        case HFENCE_GVMA:
            return "order(prior_stores, later_g_stage_page_table_accesses); invalidate(gaddr, "
                   "vmid)";
        case URET:
            return "PC = uepc; priv = User";
        case WFI:
            return "while (!interrupt_pending) wait()";
        case SFENCE_VMA:
            return "order(prior_stores, subsequent_implicit_page_table_accesses); invalidate(vaddr "
                   "= x[rs1], asid = x[rs2])";
        case MUL:
            return "x[rd] = (x[rs1] * x[rs2])[XLEN-1:0]";
        case MULH:
            return "x[rd] = (x[rs1] *_s x[rs2])[2*XLEN-1:XLEN]";
        case MULHSU:
            return "x[rd] = (x[rs1] *_su x[rs2])[2*XLEN-1:XLEN]";
        case MULHU:
            return "x[rd] = (x[rs1] *_u x[rs2])[2*XLEN-1:XLEN]";
        case DIV:
            return "x[rd] = x[rs1] /_s x[rs2]";
        case DIVU:
            return "x[rd] = x[rs1] /_u x[rs2]";
        case REM:
            return "x[rd] = x[rs1] %_s x[rs2]";
        case REMU:
            return "x[rd] = x[rs1] %_u x[rs2]";
        case MULW:
            return "x[rd] = sext((x[rs1][31:0] * x[rs2][31:0])[31:0])";
        case DIVW:
            return "x[rd] = sext(x[rs1][31:0] /_s x[rs2][31:0])";
        case DIVUW:
            return "x[rd] = sext(x[rs1][31:0] /_u x[rs2][31:0])";
        case REMW:
            return "x[rd] = sext(x[rs1][31:0] %_s x[rs2][31:0])";
        case REMUW:
            return "x[rd] = sext(x[rs1][31:0] %_u x[rs2][31:0])";
        case CZERO_EQZ:
            return "x[rd] = (x[rs2] == 0) ? 0 : x[rs1]";
        case CZERO_NEZ:
            return "x[rd] = (x[rs2] != 0) ? 0 : x[rs1]";
        case SH1ADD:
            return "x[rd] = x[rs2] + (x[rs1] << 1)";
        case SH2ADD:
            return "x[rd] = x[rs2] + (x[rs1] << 2)";
        case SH3ADD:
            return "x[rd] = x[rs2] + (x[rs1] << 3)";
        case ANDN:
            return "x[rd] = x[rs1] & ~x[rs2]";
        case ORN:
            return "x[rd] = x[rs1] | ~x[rs2]";
        case XNOR:
            return "x[rd] = ~(x[rs1] ^ x[rs2])";
        default:
            if (op_id >= LR_W && op_id <= AMOMAXU_D) {
                return "x[rd] = atomic_rmw(M[x[rs1]], x[rs2])";
            }
            if (op_id >= FLW && op_id <= FCVT_D_LU) {
                return "f[rd] / x[rd] = fp_op(rs1, rs2, rm)";
            }
            if (isa::is_vector_op(op_id)) {
                return "for i in 0..vl-1: vd[i] = vec_op(vs2[i], vs1/rs1/imm)";
            }
            if (op_id == CFU) {
                return "x[rd] = cfu_op(funct7, funct3, x[rs1], x[rs2])";
            }
            return "architectural_op(operands)";
    }
}

auto get_spec_section(std::string_view ext, OperationId op_id) -> SpecReference {
    std::string_view doc = kUnprivSpecDoc;
    std::string_view sec = "Vol. I: Ch. 1 (RV32I Base Integer Instruction Set)";

    if (ext == "Privileged" || ext == "Svinval" || ext == "H") {
        doc = kPrivSpecDoc;
        if (op_id == SFENCE_VMA) {
            sec = "Vol. II: Ch. 11 (Supervisor-Level ISA: Supervisor Memory-Management Fence)";
        } else if (op_id >= SINVAL_VMA && op_id <= SFENCE_INVAL_IR) {
            sec = "Vol. II: Ch. 15 (Svinval Fine-Grained Address-Translation Cache Invalidation)";
        } else if (ext == "H") {
            sec = "Vol. II: Ch. 14 (H Extension for Hypervisor Support)";
        } else {
            sec = "Vol. II: Ch. 2 (Machine-Level ISA and Trap-Return Instructions)";
        }
    } else if (ext == "RV32I") {
        if (op_id == LUI || op_id == AUIPC || (op_id >= ADDI && op_id <= SRAI) ||
            (op_id >= ADD && op_id <= AND)) {
            sec = "Vol. I: Ch. 1 (RV32I Integer Computational Instructions)";
        } else if (op_id == JAL || op_id == JALR || (op_id >= BEQ && op_id <= BGEU)) {
            sec = "Vol. I: Ch. 1 (RV32I Control-Transfer Instructions)";
        } else if (op_id >= LB && op_id <= SW) {
            sec = "Vol. I: Ch. 1 (RV32I Load and Store Instructions)";
        } else if (op_id == FENCE) {
            sec = "Vol. I: Ch. 1 (RV32I Memory-Ordering Instructions)";
        }
    } else if (ext == "RV64I") {
        sec = "Vol. I: Ch. 3 (RV64I Base Integer Instruction Set)";
    } else if (ext == "Zifencei") {
        sec = "Vol. I: Ch. 4 (Zifencei Instruction-Fetch Fence)";
    } else if (ext == "Zicsr") {
        sec = "Vol. I: Ch. 5 (Zicsr Control and Status Register Instructions)";
    } else if (ext == "RV32M" || ext == "RV64M" || ext == "M") {
        sec = "Vol. I: Ch. 11 (M Integer Multiplication and Division)";
    } else if (ext == "RV32A" || ext == "RV64A" || ext == "A") {
        sec = "Vol. I: Ch. 12 (A Atomic Instructions)";
    } else if (ext == "RV32F" || ext == "F") {
        sec = "Vol. I: Ch. 20 (F Single-Precision Floating-Point)";
    } else if (ext == "RV32D" || ext == "D") {
        sec = "Vol. I: Ch. 21 (D Double-Precision Floating-Point)";
    } else if (ext == "Zfhmin") {
        sec = "Vol. I: Ch. 23 (Zfhmin Half-Precision Floating-Point)";
    } else if (ext == "Zfa") {
        sec = "Vol. I: Ch. 25 (Zfa Additional Floating-Point Instructions)";
    } else if (ext == "Zimop") {
        sec = "Vol. I: Ch. 9 (Zimop May-Be-Operations)";
    } else if (ext == "Zawrs") {
        sec = "Vol. I: Ch. 13 (Zawrs Wait-on-Reservation-Set Instructions)";
    } else if (ext == "Zba") {
        sec = "Vol. I: Ch. 29, Sec. 29.1.2 (Zba Address-Generation Instructions)";
    } else if (ext == "Zbb" || ext == "Zbc" || ext == "Zbs") {
        sec = "Vol. I: Ch. 29 (B Standard Extension for Bit Manipulation)";
    } else if (ext == "Zbkb") {
        sec = "Vol. I: Ch. 29, Sec. 29.1.6 (Zbkb Bit-Manipulation for Cryptography)";
    } else if (ext == "Zicond") {
        sec = "Vol. I: Ch. 10 (Zicond Integer Conditional Operations)";
    } else if (ext == "Zicbom/Zicboz/Zicbop") {
        sec = "Vol. I: Ch. 19 (CMO Cache-Block Management Operations)";
    } else if (ext == "Vector") {
        sec = "Vol. I: Ch. 30 (V Standard Extension for Vector Operations)";
    } else if (ext == "Zvbb" || ext == "Zvbc") {
        sec = "Vol. I: Ch. 32 (Vector Cryptography and Bit-Manipulation Extensions)";
    } else if (ext == "Custom") {
        sec = "Vol. I: Extending RISC-V, custom-0 opcode space";
    }

    return SpecReference{
        .document = doc,
        .section = sec,
        .extension = ext,
        .formula = get_spec_formula(op_id),
    };
}

}  // namespace

auto get_isa_extension_name(OperationId op_id) -> std::string_view {
    if (op_id >= CSRRW && op_id <= CSRRCI) {
        return "Zicsr";
    }
    if (op_id == FENCE_I) {
        return "Zifencei";
    }
    if (op_id == ECALL || op_id == EBREAK) {
        return "Privileged";
    }
    if (op_id >= LUI && op_id <= CSRRCI) {
        switch (op_id) {
            case LWU:
            case LD:
            case SD:
            case ADDIW:
            case SLLIW:
            case SRLIW:
            case SRAIW:
            case ADDW:
            case SUBW:
            case SLLW:
            case SRLW:
            case SRAW:
                return "RV64I";
            default:
                return "RV32I";
        }
    }
    if (op_id >= URET && op_id <= SFENCE_VMA) {
        return "Privileged";
    }
    if (op_id >= SINVAL_VMA && op_id <= SFENCE_INVAL_IR) {
        return "Svinval";
    }
    if (op_id == HFENCE_VVMA || op_id == HFENCE_GVMA || op_id == HINVAL_VVMA ||
        op_id == HINVAL_GVMA || (op_id >= HLV_B && op_id <= HSV_D)) {
        return "H";
    }
    if (op_id >= FLH && op_id <= FCVT_H_D) {
        return "Zfhmin";
    }
    if (op_id >= FLI_S && op_id <= FCVTMOD_W_D) {
        return "Zfa";
    }
    if (op_id == MOP) {
        return "Zimop";
    }
    if (op_id == WRS_NTO || op_id == WRS_STO) {
        return "Zawrs";
    }
    if (op_id == CZERO_EQZ || op_id == CZERO_NEZ) {
        return "Zicond";
    }
    if (op_id >= CBO_INVAL && op_id <= PREFETCH_W) {
        return "Zicbom/Zicboz/Zicbop";
    }
    if (op_id >= MUL && op_id <= REMUW) {
        switch (op_id) {
            case MULW:
            case DIVW:
            case DIVUW:
            case REMW:
            case REMUW:
                return "RV64M";
            default:
                return "RV32M";
        }
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
        switch (op_id) {
            case SH1ADD:
            case SH2ADD:
            case SH3ADD:
            case ADD_UW:
            case SLLI_UW:
            case SH1ADD_UW:
            case SH2ADD_UW:
            case SH3ADD_UW:
                return "Zba";
            case CLMUL:
            case CLMULH:
            case CLMULR:
                return "Zbc";
            case BSET:
            case BSETI:
            case BCLR:
            case BCLRI:
            case BINV:
            case BINVI:
            case BEXT:
            case BEXTI:
                return "Zbs";
            case PACK:
            case PACKW:
                return "Zbkb";
            default:
                return "Zbb";
        }
    }
    if (isa::is_vector_op(op_id)) {
        if (op_id == VCLMUL_VV || op_id == VCLMUL_VX || op_id == VCLMULH_VV ||
            op_id == VCLMULH_VX) {
            return "Zvbc";
        }
        if ((op_id >= VANDN_VV && op_id <= VREV8_V) || (op_id >= VWSLL_VV && op_id <= VWSLL_VI)) {
            return "Zvbb";
        }
        return "Vector";
    }
    if (op_id == CFU) {
        return "Custom";
    }
    return "Unknown";
}

auto get_operation_details(OperationId op_id) -> std::pair<std::string_view, std::string_view> {
    return get_description(op_id);
}

auto get_operation_spec(OperationId op_id) -> OperationSpecDetails {
    auto const [mnemonic, desc] = get_description(op_id);
    auto const ext = get_isa_extension_name(op_id);
    return OperationSpecDetails{
        .mnemonic = mnemonic,
        .description = desc,
        .reference = get_spec_section(ext, op_id),
    };
}

auto get_cfu_details(uint32_t funct7, uint32_t funct3) -> CfuDetails {
    std::string desc = std::format(
        "Dispatches custom function {} (sub-operation 0x{:02X}) to the hardware Custom Function "
        "Unit (CFU). Operands rs1 and rs2 are passed to accelerator logic; computed result writes "
        "to rd.",
        funct3, funct7);
    return CfuDetails{
        .funct3 = funct3,
        .funct7 = funct7,
        .name = "CFU",
        .description = std::move(desc),
        .spec_section = "Vol. I: Extending RISC-V, custom-0 opcode space",
    };
}

}  // namespace simrv::explain
