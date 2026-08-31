#include <algorithm>

#include "simrv/core/Cpu.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

void ExecuteUnit::execute_vector_config(core::CPU& cpu, isa::OperationId op_id, Instruction ir,
                                        RegId rd, RegId rs1, RegId rs2) {
    uint64_t raw_vtype = 0;
    if (op_id == isa::OperationId::VSETVLI) {
        raw_vtype = (ir >> 20) & 0x7FF;
    } else if (op_id == isa::OperationId::VSETIVLI) {
        raw_vtype = (ir >> 20) & 0x3FF;
    } else {
        raw_vtype = cpu.state().regs.read(rs2);
    }

    VtypeView const view{.raw = raw_vtype, .xlen = static_cast<uint8_t>(cpu.state().regs.xlen)};
    uint64_t const vill_mask = 1ULL << (cpu.state().regs.xlen - 1);
    bool vill = view.vill();

    uint32_t const req_sew = view.sew_bits();
    uint32_t lmul_num = 1;
    uint32_t lmul_den = 1;

    switch (view.vlmul()) {
        case Vlmul::LMUL_1:
            lmul_num = 1;
            lmul_den = 1;
            break;
        case Vlmul::LMUL_2:
            lmul_num = 2;
            lmul_den = 1;
            break;
        case Vlmul::LMUL_4:
            lmul_num = 4;
            lmul_den = 1;
            break;
        case Vlmul::LMUL_8:
            lmul_num = 8;
            lmul_den = 1;
            break;
        case Vlmul::LMUL_F8:
            lmul_num = 1;
            lmul_den = 8;
            break;
        case Vlmul::LMUL_F4:
            lmul_num = 1;
            lmul_den = 4;
            break;
        case Vlmul::LMUL_F2:
            lmul_num = 1;
            lmul_den = 2;
            break;
        default:
            vill = true;
            break;
    }

    if (!vill && (lmul_num * 64 < req_sew * lmul_den)) {
        vill = true;
    }

    uint32_t new_vl = 0;
    uint64_t final_vtype = 0;
    if (vill) {
        new_vl = 0;
        final_vtype = vill_mask;
    } else {
        final_vtype = raw_vtype & 0xFFu;
        auto const vlmax = (cpu.state().regs.vlen * lmul_num) / (req_sew * lmul_den);

        if (op_id == isa::OperationId::VSETIVLI) {
            uint32_t const uimm = (ir >> 15) & 0x1F;
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
    cpu.state().vtype = final_vtype;
    cpu.state().vstart = 0;
    cpu.state().regs.write(rd, new_vl);

    cpu.state().mstatus |= enum_mask(core::MstatusBit::Vs);
}

}  // namespace simrv::execute
