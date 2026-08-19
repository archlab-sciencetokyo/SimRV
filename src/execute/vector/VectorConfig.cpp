#include <algorithm>

#include "simrv/core/Cpu.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

void ExecuteUnit::execute_vector_config(core::CPU& cpu, isa::OperationId op_id, Instruction ir,
                                        RegId rd, RegId rs1, RegId rs2) {
    uint64_t vtype = 0;
    if (op_id == isa::OperationId::VSETVLI) {
        vtype = (ir >> 20) & 0x7FF;
    } else if (op_id == isa::OperationId::VSETIVLI) {
        vtype = (ir >> 20) & 0x3FF;
    } else {
        vtype = cpu.state().regs.read(rs2);
    }

    uint64_t vill_mask = 1ULL << (cpu.state().regs.xlen - 1);
    bool vill = (vtype & vill_mask) != 0;

    // Check reserved bits in vtype [XLEN-2:8]
    uint64_t reserved_mask = ~(0xFFULL | vill_mask);
    if (cpu.state().regs.xlen == 32) {
        reserved_mask &= 0xFFFFFFFFULL;
    }
    if ((vtype & reserved_mask) != 0) {
        vill = true;
    }

    uint32_t sew_field = (vtype >> 3) & 0x7;
    uint32_t lmul_field = vtype & 0x7u;

    if (sew_field > 3 || lmul_field == 4) {
        vill = true;
    }

    uint32_t req_sew = 8 << sew_field;
    uint32_t lmul_num = 1;
    uint32_t lmul_den = 1;

    switch (lmul_field) {
        case 0:
            lmul_num = 1;
            lmul_den = 1;
            break;
        case 1:
            lmul_num = 2;
            lmul_den = 1;
            break;
        case 2:
            lmul_num = 4;
            lmul_den = 1;
            break;
        case 3:
            lmul_num = 8;
            lmul_den = 1;
            break;
        case 5:
            lmul_num = 1;
            lmul_den = 8;
            break;
        case 6:
            lmul_num = 1;
            lmul_den = 4;
            break;
        case 7:
            lmul_num = 1;
            lmul_den = 2;
            break;
        default:
            break;
    }

    if (!vill && (lmul_num * 64 < req_sew * lmul_den)) {
        vill = true;
    }

    uint32_t new_vl = 0;
    if (vill) {
        new_vl = 0;
        vtype = vill_mask;
    } else {
        vtype = vtype & 0xFFu;
        auto vlmax = (cpu.state().regs.vlen * lmul_num) / (req_sew * lmul_den);

        if (op_id == isa::OperationId::VSETIVLI) {
            uint32_t uimm = (ir >> 15) & 0x1F;
            new_vl = std::min(uimm, vlmax);
        } else {
            if (rs1 == RegId::Zero) {
                if (rd == RegId::Zero) {
                    new_vl = std::min(static_cast<uint32_t>(cpu.state().vl), vlmax);
                } else {
                    new_vl = vlmax;
                }
            } else {
                new_vl = std::min(static_cast<uint32_t>(cpu.state().regs.read(rs1)), vlmax);
            }
        }
    }

    cpu.state().vl = new_vl;
    cpu.state().vtype = vtype;
    cpu.state().vstart = 0;
    cpu.state().regs.write(rd, new_vl);

    cpu.state().mstatus |= enum_mask(core::MstatusBit::Vs);
}

}  // namespace simrv::execute
