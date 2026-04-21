/**
 * @file IoController.cpp
 * @brief I/O Controller implementation for device firmware execution.
 */

#include "IoController.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <print>

#include "DecodeUnit.hpp"
#include "ExecuteUnit.hpp"
#include "Machine.hpp"
#include "MemorySubsystem.hpp"

namespace {

/**
 * @brief Load binary image file into destination memory buffer.
 *
 * @param file_path Path to the binary image file
 * @param dest Destination memory buffer
 */
inline void load_image_into_buffer(std::string_view file_path, Byte* dest) {
    std::ifstream in(static_cast<std::string>(file_path), std::ios::binary);
    if (!in.is_open()) {
        std::println(stdout, "__ Error: image_file {:.{}} cannot be found", file_path.data(),
                     static_cast<int>(file_path.size()));
        exit(0);
    }
    uint8_t tmp = 0;
    int i = 0;
    while (in.read(reinterpret_cast<char*>(&tmp), sizeof(tmp))) {
        dest[i++] = static_cast<Byte>(tmp);
    }
}

}  // namespace

void IoController::init(std::string_view image_path) {
    constexpr auto local_mem_size = simrv::memory::kLocalCoreMemorySize;
    cmem_storage_.assign(local_mem_size, Byte{});
    cmem = cmem_storage_.data();
    load_image_into_buffer(image_path, cmem);

    // Initialize register file
    reg.fill(0);
    reg[11] = 0x8000;  // Stack pointer initialization
}

auto IoController::exec() -> bool {
    DecodeUnit decode_unit;
    ExecuteUnit execute_unit;
    bool ret = true;
    PipelineContext ctx;

    // ===== FETCH & DECODE STAGE =====
    ctx.cpc = pc;
    std::memcpy(&ctx.ir, &cmem[pc & simrv::memory::kDramMask], sizeof(ctx.ir));
    if ((ctx.ir & 3) != 3) {
        std::println(stderr,
                     "[IoController] ERROR: Compressed instruction (RV32C) not supported at "
                     "PC=0x{:08x} (instruction=0x{:04x})",
                     pc, static_cast<uint16_t>(ctx.ir & 0xFFFFu));
        exit(1);
    }

    // Decode instruction fields from 32-bit word
    ctx.opcode = (ctx.ir >> 0) & 0x7F;
    ctx.rd = (ctx.ir >> 7) & 0x1F;
    ctx.rs1 = (ctx.ir >> 15) & 0x1F;
    ctx.rs2 = (ctx.ir >> 20) & 0x1F;
    ctx.funct3 = (ctx.ir >> 12) & 0x7;
    ctx.funct5 = (ctx.ir >> 27) & 0x1F;
    ctx.funct7 = (ctx.ir >> 25);
    ctx.funct12 = (ctx.ir >> 20);
    ctx.imm = decode_unit.decodeImmediate(ctx.ir);

    // ===== OPERAND FETCH STAGE =====
    ctx.rrs1 = reg[ctx.rs1];
    ctx.rrs2 = reg[ctx.rs2];

    // ===== EXECUTE STAGE =====
    switch (ctx.opcode) {
        case static_cast<Instruction>(Opcode::Lui): {
            ctx.tkn = 0;
            ctx.wb_data = ctx.imm << 12;
            break;
        }
        case static_cast<Instruction>(Opcode::Auipc): {
            ctx.tkn = 0;
            ctx.wb_data = pc + (ctx.imm << 12);
            break;
        }
        case static_cast<Instruction>(Opcode::Jal): {
            ctx.tkn = 1;
            ctx.wb_data = pc + 4;
            ctx.jmp_pc = pc + ctx.imm;
            break;
        }
        case static_cast<Instruction>(Opcode::Jalr): {
            ctx.tkn = 1;
            ctx.wb_data = pc + 4;
            ctx.jmp_pc = ctx.rrs1 + ctx.imm;
            break;
        }
        case static_cast<Instruction>(Opcode::Op): {
            ctx.tkn = 0;
            ctx.wb_data = execute_unit.aluInt(ctx.rrs1, ctx.rrs2, ctx.funct3, ctx.funct7);
            break;
        }
        case static_cast<Instruction>(Opcode::Load):
        case static_cast<Instruction>(Opcode::Store): {
            ctx.tkn = 0;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            break;
        }
        case static_cast<Instruction>(Opcode::MiscMem): {
            ctx.tkn = 0;
            break;
        }
        case static_cast<Instruction>(Opcode::Branch): {
            ctx.tkn = execute_unit.branchTaken(ctx.rrs1, ctx.rrs2, ctx.funct3);
            ctx.jmp_pc = pc + ctx.imm;
            break;
        }
        case static_cast<Instruction>(Opcode::OpImm): {
            ctx.tkn = 0;
            ctx.funct7 &= (ctx.funct3 == static_cast<Instruction>(Funct3::Add)) ? 0 : 0x20;
            ctx.wb_data = execute_unit.aluInt(ctx.rrs1, ctx.imm, ctx.funct3, ctx.funct7);
            break;
        }
        case static_cast<Instruction>(Opcode::System): {
            // System instructions: ECALL, EBREAK, CSR operations
            // IoController does not support privileged system operations
            if (ctx.funct12 == 0x0) {  // ECALL
                std::println(
                    stderr,
                    "[IoController] WARNING: ECALL (system call) not supported at PC=0x{:08x}",
                    ctx.cpc);
                ctx.tkn = 0;
            } else if (ctx.funct12 == 0x1) {  // EBREAK
                std::println(
                    stderr,
                    "[IoController] WARNING: EBREAK (debugger break) not supported at PC=0x{:08x}",
                    ctx.cpc);
                ctx.tkn = 0;
            } else if ((ctx.funct3 >= 1) && (ctx.funct3 <= 3)) {  // CSR operations
                std::println(stderr,
                             "[IoController] WARNING: CSR operations not supported at PC=0x{:08x} "
                             "(funct3={}, csr=0x{:03x})",
                             ctx.cpc, ctx.funct3, ctx.funct12);
                ctx.tkn = 0;
            } else {
                std::println(stderr,
                             "[IoController] WARNING: Unknown System instruction at PC=0x{:08x} "
                             "(funct3={}, funct12=0x{:03x})",
                             ctx.cpc, ctx.funct3, ctx.funct12);
                ctx.tkn = 0;
            }
            break;
        }
        default: {
            // Unsupported opcode
            std::println(stderr,
                         "[IoController] WARNING: Unsupported instruction opcode=0x{:02x} at "
                         "PC=0x{:08x} (ir=0x{:08x})",
                         ctx.opcode, ctx.cpc, ctx.ir);
            ctx.tkn = 0;
            break;
        }
    }

    // ===== MEMORY STAGE =====
    const int access_size = 1 << (ctx.funct3 & 0x3);

    if (ctx.opcode == static_cast<Instruction>(Opcode::Load)) {
        if ((ctx.mem_addr >> 28) == 0x8) {
            // Main memory access
            ctx.mem_rdata = simrv::memory_detail::ram_read_fast(
                ctx.mem_addr & simrv::memory::kDramMask, ctx.funct3, mmem);
        } else if ((ctx.mem_addr >> 28) == 0x9) {
            // Disk access
            ctx.mem_rdata = 0;
            for (int i = 0; i < access_size; ++i) {
                ctx.mem_rdata |=
                    static_cast<Word>(std::to_integer<uint8_t>(disk[(ctx.mem_addr + i)]))
                    << (8 * i);
            }
        } else if (ctx.mem_addr == 0x40009000) {
            ctx.mem_rdata = Mode;
        } else if (ctx.mem_addr == 0x40009004) {
            ctx.mem_rdata = Qnum;
        } else if (ctx.mem_addr == 0x40009008) {
            ctx.mem_rdata = Qsel;
        } else if ((ctx.mem_addr >> 12) == 0x4000a) {
            // Console queue read
            const int idx = static_cast<int>(ctx.mem_addr / 0x24u);
            const Address offset = ctx.mem_addr % 0x24u;
            if (offset == 0x0u) {
                ctx.mem_rdata = cons_queue[idx].Ready;
            } else if (offset == 0x4u) {
                ctx.mem_rdata = cons_queue[idx].Notify;
            } else if (offset == 0x8u) {
                ctx.mem_rdata = cons_queue[idx].DescLow;
            } else if (offset == 0xCu) {
                ctx.mem_rdata = cons_queue[idx].DescHigh;
            } else if (offset == 0x10u) {
                ctx.mem_rdata = cons_queue[idx].AvailLow;
            } else if (offset == 0x14u) {
                ctx.mem_rdata = cons_queue[idx].AvailHigh;
            } else if (offset == 0x18u) {
                ctx.mem_rdata = cons_queue[idx].UsedLow;
            } else if (offset == 0x1Cu) {
                ctx.mem_rdata = cons_queue[idx].UsedHigh;
            } else if (offset == 0x20u) {
                ctx.mem_rdata = cons_queue[idx].last_avail_idx;
            }
        } else if ((ctx.mem_addr >> 12) == 0x4000b) {
            // Disk queue read
            const int idx = static_cast<int>(ctx.mem_addr / 0x24u);
            const Address offset = ctx.mem_addr % 0x24u;
            if (offset == 0x0u) {
                ctx.mem_rdata = disk_queue[idx].Ready;
            } else if (offset == 0x4u) {
                ctx.mem_rdata = disk_queue[idx].Notify;
            } else if (offset == 0x8u) {
                ctx.mem_rdata = disk_queue[idx].DescLow;
            } else if (offset == 0xCu) {
                ctx.mem_rdata = disk_queue[idx].DescHigh;
            } else if (offset == 0x10u) {
                ctx.mem_rdata = disk_queue[idx].AvailLow;
            } else if (offset == 0x14u) {
                ctx.mem_rdata = disk_queue[idx].AvailHigh;
            } else if (offset == 0x18u) {
                ctx.mem_rdata = disk_queue[idx].UsedLow;
            } else if (offset == 0x1Cu) {
                ctx.mem_rdata = disk_queue[idx].UsedHigh;
            } else if (offset == 0x20u) {
                ctx.mem_rdata = disk_queue[idx].last_avail_idx;
            }
        } else if ((ctx.mem_addr >> 12) == 0x4000c) {
            ctx.mem_rdata = static_cast<Word>(std::to_integer<uint8_t>(cons_fifo));
        } else {
            // Local memory access (default)
            ctx.mem_rdata = simrv::memory_detail::ram_read_fast(
                ctx.mem_addr & simrv::memory::kDramMask, ctx.funct3, cmem);
        }
    }

    if (ctx.opcode == static_cast<Instruction>(Opcode::Store)) {
        if (ctx.mem_addr == 0x40008000) {
            // Console control register
            if ((ctx.rrs2 >> 16) == 1) {
                std::print("{}", static_cast<char>(ctx.rrs2 & 0xff));
                std::fflush(stdout);
            }
            if ((ctx.rrs2 >> 16) == 2) {
                ret = false;  // Power-off command
            }
        } else if ((ctx.mem_addr >> 28) == 0x8) {
            // Write to main memory
            for (int i = 0; i < access_size; ++i) {
                mmem[(ctx.mem_addr + i) & simrv::memory::kDramMask] =
                    static_cast<Byte>(static_cast<uint8_t>((ctx.rrs2 >> (8 * i)) & 0xFF));
            }
        } else if ((ctx.mem_addr >> 28) == 0x9) {
            // Write to disk
            auto* dsk_tmp = reinterpret_cast<Word*>(disk);
            dsk_tmp[(ctx.mem_addr) / 4] = ctx.rrs2;
        } else if ((ctx.mem_addr >> 12) == 0x4000a) {
            // Console queue write
            const int idx = static_cast<int>(ctx.mem_addr / 0x24u);
            const Address offset = ctx.mem_addr % 0x24u;
            if (offset == 0x0u) {
                cons_queue[idx].Ready = ctx.rrs2;
            } else if (offset == 0x4u) {
                cons_queue[idx].Notify = ctx.rrs2;
            } else if (offset == 0x8u) {
                cons_queue[idx].DescLow = ctx.rrs2;
            } else if (offset == 0xCu) {
                cons_queue[idx].DescHigh = ctx.rrs2;
            } else if (offset == 0x10u) {
                cons_queue[idx].AvailLow = ctx.rrs2;
            } else if (offset == 0x14u) {
                cons_queue[idx].AvailHigh = ctx.rrs2;
            } else if (offset == 0x18u) {
                cons_queue[idx].UsedLow = ctx.rrs2;
            } else if (offset == 0x1Cu) {
                cons_queue[idx].UsedHigh = ctx.rrs2;
            } else if (offset == 0x20u) {
                cons_queue[idx].last_avail_idx = ctx.rrs2;
            }
        } else if ((ctx.mem_addr >> 12) == 0x4000b) {
            // Disk queue write
            const int idx = static_cast<int>(ctx.mem_addr / 0x24u);
            const Address offset = ctx.mem_addr % 0x24u;
            if (offset == 0x0u) {
                disk_queue[idx].Ready = ctx.rrs2;
            } else if (offset == 0x4u) {
                disk_queue[idx].Notify = ctx.rrs2;
            } else if (offset == 0x8u) {
                disk_queue[idx].DescLow = ctx.rrs2;
            } else if (offset == 0xCu) {
                disk_queue[idx].DescHigh = ctx.rrs2;
            } else if (offset == 0x10u) {
                disk_queue[idx].AvailLow = ctx.rrs2;
            } else if (offset == 0x14u) {
                disk_queue[idx].AvailHigh = ctx.rrs2;
            } else if (offset == 0x18u) {
                disk_queue[idx].UsedLow = ctx.rrs2;
            } else if (offset == 0x1Cu) {
                disk_queue[idx].UsedHigh = ctx.rrs2;
            } else if (offset == 0x20u) {
                disk_queue[idx].last_avail_idx = ctx.rrs2;
            }
        } else {
            // Write to local memory (default)
            for (int i = 0; i < access_size; ++i) {
                cmem[(ctx.mem_addr + i) & simrv::memory::kDramMask] =
                    static_cast<Byte>(static_cast<uint8_t>((ctx.rrs2 >> (8 * i)) & 0xFF));
            }
        }
    }

    // ===== WRITEBACK STAGE =====
    Word wb_data = 0;
    bool wb_enable = false;

    if (ctx.opcode == static_cast<Instruction>(Opcode::Load)) {
        wb_data = ctx.mem_rdata;
        wb_enable = true;
    } else if ((ctx.opcode == static_cast<Instruction>(Opcode::Lui)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::Auipc)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::Jal)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::Jalr)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::Op)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::OpImm))) {
        wb_data = ctx.wb_data;
        wb_enable = true;
    }

    // ===== COMMIT STAGE =====
    if (wb_enable && (ctx.rd != 0)) {
        reg[ctx.rd] = wb_data;
    }

    // Update PC: jump if branch taken, otherwise sequential increment
    pc = (ctx.tkn != 0) ? ctx.jmp_pc : pc + 4;

    if (owner != nullptr) {
        owner->e_uc_cnt++;
    }

    return ret;
}
