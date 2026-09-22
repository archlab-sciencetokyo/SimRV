#include <array>
#include <utility>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/pipeline/OperationInfo.hpp"

namespace simrv::execute {

namespace {
/** RVV 1.0 operations whose traps are always reported with vstart=0. */
constexpr auto requires_zero_vstart(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VREDSUM_VS:
        case isa::OperationId::VREDAND_VS:
        case isa::OperationId::VREDOR_VS:
        case isa::OperationId::VREDXOR_VS:
        case isa::OperationId::VREDMINU_VS:
        case isa::OperationId::VREDMIN_VS:
        case isa::OperationId::VREDMAXU_VS:
        case isa::OperationId::VREDMAX_VS:
        case isa::OperationId::VWREDSUM_VS:
        case isa::OperationId::VWREDSUMU_VS:
        case isa::OperationId::VFREDUSUM_VS:
        case isa::OperationId::VFREDOSUM_VS:
        case isa::OperationId::VFREDMIN_VS:
        case isa::OperationId::VFREDMAX_VS:
        case isa::OperationId::VFWREDUSUM_VS:
        case isa::OperationId::VFWREDOSUM_VS:
        case isa::OperationId::VCPOP_M:
        case isa::OperationId::VFIRST_M:
        case isa::OperationId::VMSBF_M:
        case isa::OperationId::VMSIF_M:
        case isa::OperationId::VMSOF_M:
        case isa::OperationId::VIOTA_M:
        case isa::OperationId::VCOMPRESS_VM:
            return true;
        default:
            return false;
    }
}

/** Vector operations that consume or produce scalar/vector floating-point state. */
constexpr auto is_vector_fp(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VFADD_VV:
        case isa::OperationId::VFSLIDE1UP_VF:
        case isa::OperationId::VFSLIDE1DOWN_VF:
        case isa::OperationId::VFADD_VF:
        case isa::OperationId::VFSUB_VV:
        case isa::OperationId::VFSUB_VF:
        case isa::OperationId::VFRSUB_VF:
        case isa::OperationId::VFMUL_VV:
        case isa::OperationId::VFMUL_VF:
        case isa::OperationId::VFDIV_VV:
        case isa::OperationId::VFDIV_VF:
        case isa::OperationId::VFRDIV_VF:
        case isa::OperationId::VFSQRT_V:
        case isa::OperationId::VFRSQRT7_V:
        case isa::OperationId::VFREC7_V:
        case isa::OperationId::VFCLASS_V:
        case isa::OperationId::VFCVT_XU_F_V:
        case isa::OperationId::VFCVT_X_F_V:
        case isa::OperationId::VFCVT_F_XU_V:
        case isa::OperationId::VFCVT_F_X_V:
        case isa::OperationId::VFCVT_RTZ_XU_F_V:
        case isa::OperationId::VFCVT_RTZ_X_F_V:
        case isa::OperationId::VFWCVT_XU_F_V:
        case isa::OperationId::VFWCVT_X_F_V:
        case isa::OperationId::VFWCVT_F_XU_V:
        case isa::OperationId::VFWCVT_F_X_V:
        case isa::OperationId::VFWCVT_F_F_V:
        case isa::OperationId::VFWCVT_RTZ_XU_F_V:
        case isa::OperationId::VFWCVT_RTZ_X_F_V:
        case isa::OperationId::VFNCVT_XU_F_W:
        case isa::OperationId::VFNCVT_X_F_W:
        case isa::OperationId::VFNCVT_F_XU_W:
        case isa::OperationId::VFNCVT_F_X_W:
        case isa::OperationId::VFNCVT_F_F_W:
        case isa::OperationId::VFNCVT_ROD_F_F_W:
        case isa::OperationId::VFNCVT_RTZ_XU_F_W:
        case isa::OperationId::VFNCVT_RTZ_X_F_W:
        case isa::OperationId::VFSGNJ_VV:
        case isa::OperationId::VFSGNJ_VF:
        case isa::OperationId::VFSGNJN_VV:
        case isa::OperationId::VFSGNJN_VF:
        case isa::OperationId::VFSGNJX_VV:
        case isa::OperationId::VFSGNJX_VF:
        case isa::OperationId::VFMIN_VV:
        case isa::OperationId::VFMIN_VF:
        case isa::OperationId::VFMAX_VV:
        case isa::OperationId::VFMAX_VF:
        case isa::OperationId::VMFEQ_VV:
        case isa::OperationId::VMFEQ_VF:
        case isa::OperationId::VMFNE_VV:
        case isa::OperationId::VMFNE_VF:
        case isa::OperationId::VMFLT_VV:
        case isa::OperationId::VMFLT_VF:
        case isa::OperationId::VMFLE_VV:
        case isa::OperationId::VMFLE_VF:
        case isa::OperationId::VMFGT_VF:
        case isa::OperationId::VMFGE_VF:
        case isa::OperationId::VFMADD_VV:
        case isa::OperationId::VFMADD_VF:
        case isa::OperationId::VFNMADD_VV:
        case isa::OperationId::VFNMADD_VF:
        case isa::OperationId::VFMSUB_VV:
        case isa::OperationId::VFMSUB_VF:
        case isa::OperationId::VFNMSUB_VV:
        case isa::OperationId::VFNMSUB_VF:
        case isa::OperationId::VFMACC_VV:
        case isa::OperationId::VFMACC_VF:
        case isa::OperationId::VFNMACC_VV:
        case isa::OperationId::VFNMACC_VF:
        case isa::OperationId::VFMSAC_VV:
        case isa::OperationId::VFMSAC_VF:
        case isa::OperationId::VFNMSAC_VV:
        case isa::OperationId::VFNMSAC_VF:
        case isa::OperationId::VFMV_F_S:
        case isa::OperationId::VFMV_S_F:
        case isa::OperationId::VFMERGE_VFM:
        case isa::OperationId::VFMV_V_F:
        case isa::OperationId::VFREDUSUM_VS:
        case isa::OperationId::VFREDOSUM_VS:
        case isa::OperationId::VFREDMIN_VS:
        case isa::OperationId::VFREDMAX_VS:
        case isa::OperationId::VFWREDUSUM_VS:
        case isa::OperationId::VFWREDOSUM_VS:
        case isa::OperationId::VFWADD_VV:
        case isa::OperationId::VFWADD_VF:
        case isa::OperationId::VFWSUB_VV:
        case isa::OperationId::VFWSUB_VF:
        case isa::OperationId::VFWADD_WV:
        case isa::OperationId::VFWADD_WF:
        case isa::OperationId::VFWSUB_WV:
        case isa::OperationId::VFWSUB_WF:
        case isa::OperationId::VFWMUL_VV:
        case isa::OperationId::VFWMUL_VF:
        case isa::OperationId::VFWMACC_VV:
        case isa::OperationId::VFWMACC_VF:
        case isa::OperationId::VFWNMACC_VV:
        case isa::OperationId::VFWNMACC_VF:
        case isa::OperationId::VFWMSAC_VV:
        case isa::OperationId::VFWMSAC_VF:
        case isa::OperationId::VFWNMSAC_VV:
        case isa::OperationId::VFWNMSAC_VF:
            return true;
        default:
            return false;
    }
}

/** RVV 1.0 reserves every vector FP instruction when frm contains an invalid value. */
constexpr auto is_vector_fp_arithmetic(isa::OperationId op_id) -> bool {
    return is_vector_fp(op_id);
}

/** FP operations whose widening/narrowing form touches an element twice SEW wide. */
constexpr auto uses_wide_fp_element(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VFWREDUSUM_VS:
        case isa::OperationId::VFWREDOSUM_VS:
        case isa::OperationId::VFWADD_VV:
        case isa::OperationId::VFWADD_VF:
        case isa::OperationId::VFWSUB_VV:
        case isa::OperationId::VFWSUB_VF:
        case isa::OperationId::VFWADD_WV:
        case isa::OperationId::VFWADD_WF:
        case isa::OperationId::VFWSUB_WV:
        case isa::OperationId::VFWSUB_WF:
        case isa::OperationId::VFWMUL_VV:
        case isa::OperationId::VFWMUL_VF:
        case isa::OperationId::VFWMACC_VV:
        case isa::OperationId::VFWMACC_VF:
        case isa::OperationId::VFWNMACC_VV:
        case isa::OperationId::VFWNMACC_VF:
        case isa::OperationId::VFWMSAC_VV:
        case isa::OperationId::VFWMSAC_VF:
        case isa::OperationId::VFWNMSAC_VV:
        case isa::OperationId::VFWNMSAC_VF:
        case isa::OperationId::VFWCVT_XU_F_V:
        case isa::OperationId::VFWCVT_X_F_V:
        case isa::OperationId::VFWCVT_F_XU_V:
        case isa::OperationId::VFWCVT_F_X_V:
        case isa::OperationId::VFWCVT_F_F_V:
        case isa::OperationId::VFWCVT_RTZ_XU_F_V:
        case isa::OperationId::VFWCVT_RTZ_X_F_V:
        case isa::OperationId::VFNCVT_XU_F_W:
        case isa::OperationId::VFNCVT_X_F_W:
        case isa::OperationId::VFNCVT_F_XU_W:
        case isa::OperationId::VFNCVT_F_X_W:
        case isa::OperationId::VFNCVT_F_F_W:
        case isa::OperationId::VFNCVT_ROD_F_F_W:
        case isa::OperationId::VFNCVT_RTZ_XU_F_W:
        case isa::OperationId::VFNCVT_RTZ_X_F_W:
            return true;
        default:
            return false;
    }
}

struct VectorMemoryLayout {
    bool recognized = false;
    bool valid = false;
    uint32_t field_registers = 1;
};

struct VectorOperandGroup {
    uint32_t first = 0;
    uint32_t registers = 0;
    uint32_t emul_numerator = 0;
    uint32_t emul_denominator = 1;
    uint32_t eew_numerator = 1;
    uint32_t eew_denominator = 1;
};

struct VectorComputeLayout {
    bool recognized = false;
    bool valid = false;
};

constexpr auto vector_memory_eew(isa::OperationId op_id) -> uint32_t {
    switch (op_id) {
        case isa::OperationId::VLE8_V:
        case isa::OperationId::VLE8FF_V:
        case isa::OperationId::VSE8_V:
        case isa::OperationId::VLSE8_V:
        case isa::OperationId::VSSE8_V:
        case isa::OperationId::VLUXEI8_V:
        case isa::OperationId::VLOXEI8_V:
        case isa::OperationId::VSUXEI8_V:
        case isa::OperationId::VSOXEI8_V:
            return 8;
        case isa::OperationId::VLE16_V:
        case isa::OperationId::VLE16FF_V:
        case isa::OperationId::VSE16_V:
        case isa::OperationId::VLSE16_V:
        case isa::OperationId::VSSE16_V:
        case isa::OperationId::VLUXEI16_V:
        case isa::OperationId::VLOXEI16_V:
        case isa::OperationId::VSUXEI16_V:
        case isa::OperationId::VSOXEI16_V:
            return 16;
        case isa::OperationId::VLE32_V:
        case isa::OperationId::VLE32FF_V:
        case isa::OperationId::VSE32_V:
        case isa::OperationId::VLSE32_V:
        case isa::OperationId::VSSE32_V:
        case isa::OperationId::VLUXEI32_V:
        case isa::OperationId::VLOXEI32_V:
        case isa::OperationId::VSUXEI32_V:
        case isa::OperationId::VSOXEI32_V:
            return 32;
        case isa::OperationId::VLE64_V:
        case isa::OperationId::VLE64FF_V:
        case isa::OperationId::VSE64_V:
        case isa::OperationId::VLSE64_V:
        case isa::OperationId::VSSE64_V:
        case isa::OperationId::VLUXEI64_V:
        case isa::OperationId::VLOXEI64_V:
        case isa::OperationId::VSUXEI64_V:
        case isa::OperationId::VSOXEI64_V:
            return 64;
        default:
            return 0;
    }
}

constexpr auto is_indexed_vector_memory(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VLUXEI8_V:
        case isa::OperationId::VLUXEI16_V:
        case isa::OperationId::VLUXEI32_V:
        case isa::OperationId::VLUXEI64_V:
        case isa::OperationId::VLOXEI8_V:
        case isa::OperationId::VLOXEI16_V:
        case isa::OperationId::VLOXEI32_V:
        case isa::OperationId::VLOXEI64_V:
        case isa::OperationId::VSUXEI8_V:
        case isa::OperationId::VSUXEI16_V:
        case isa::OperationId::VSUXEI32_V:
        case isa::OperationId::VSUXEI64_V:
        case isa::OperationId::VSOXEI8_V:
        case isa::OperationId::VSOXEI16_V:
        case isa::OperationId::VSOXEI32_V:
        case isa::OperationId::VSOXEI64_V:
            return true;
        default:
            return false;
    }
}

constexpr auto is_indexed_vector_load(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VLUXEI8_V:
        case isa::OperationId::VLUXEI16_V:
        case isa::OperationId::VLUXEI32_V:
        case isa::OperationId::VLUXEI64_V:
        case isa::OperationId::VLOXEI8_V:
        case isa::OperationId::VLOXEI16_V:
        case isa::OperationId::VLOXEI32_V:
        case isa::OperationId::VLOXEI64_V:
            return true;
        default:
            return false;
    }
}

constexpr auto is_vector_store(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VSE8_V:
        case isa::OperationId::VSE16_V:
        case isa::OperationId::VSE32_V:
        case isa::OperationId::VSE64_V:
        case isa::OperationId::VSM_V:
        case isa::OperationId::VSSE8_V:
        case isa::OperationId::VSSE16_V:
        case isa::OperationId::VSSE32_V:
        case isa::OperationId::VSSE64_V:
        case isa::OperationId::VSUXEI8_V:
        case isa::OperationId::VSUXEI16_V:
        case isa::OperationId::VSUXEI32_V:
        case isa::OperationId::VSUXEI64_V:
        case isa::OperationId::VSOXEI8_V:
        case isa::OperationId::VSOXEI16_V:
        case isa::OperationId::VSOXEI32_V:
        case isa::OperationId::VSOXEI64_V:
        case isa::OperationId::VS1R_V:
        case isa::OperationId::VS2R_V:
        case isa::OperationId::VS4R_V:
        case isa::OperationId::VS8R_V:
            return true;
        default:
            return false;
    }
}

constexpr auto has_scalar_destination(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VMV_X_S:
        case isa::OperationId::VFMV_F_S:
        case isa::OperationId::VFIRST_M:
        case isa::OperationId::VCPOP_M:
            return true;
        default:
            return false;
    }
}

constexpr auto has_mask_destination(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VMSEQ_VV:
        case isa::OperationId::VMSEQ_VX:
        case isa::OperationId::VMSEQ_VI:
        case isa::OperationId::VMSNE_VV:
        case isa::OperationId::VMSNE_VX:
        case isa::OperationId::VMSNE_VI:
        case isa::OperationId::VMSLT_VV:
        case isa::OperationId::VMSLT_VX:
        case isa::OperationId::VMSLTU_VV:
        case isa::OperationId::VMSLTU_VX:
        case isa::OperationId::VMSLE_VV:
        case isa::OperationId::VMSLE_VX:
        case isa::OperationId::VMSLE_VI:
        case isa::OperationId::VMSLEU_VV:
        case isa::OperationId::VMSLEU_VX:
        case isa::OperationId::VMSLEU_VI:
        case isa::OperationId::VMSGT_VX:
        case isa::OperationId::VMSGT_VI:
        case isa::OperationId::VMSGTU_VX:
        case isa::OperationId::VMSGTU_VI:
        case isa::OperationId::VMFEQ_VV:
        case isa::OperationId::VMFEQ_VF:
        case isa::OperationId::VMFNE_VV:
        case isa::OperationId::VMFNE_VF:
        case isa::OperationId::VMFLT_VV:
        case isa::OperationId::VMFLT_VF:
        case isa::OperationId::VMFLE_VV:
        case isa::OperationId::VMFLE_VF:
        case isa::OperationId::VMFGT_VF:
        case isa::OperationId::VMFGE_VF:
        case isa::OperationId::VMADC_VV:
        case isa::OperationId::VMADC_VX:
        case isa::OperationId::VMADC_VI:
        case isa::OperationId::VMADC_VVM:
        case isa::OperationId::VMADC_VXM:
        case isa::OperationId::VMADC_VIM:
        case isa::OperationId::VMSBC_VV:
        case isa::OperationId::VMSBC_VX:
        case isa::OperationId::VMSBC_VVM:
        case isa::OperationId::VMSBC_VXM:
        case isa::OperationId::VMSBF_M:
        case isa::OperationId::VMSIF_M:
        case isa::OperationId::VMSOF_M:
        case isa::OperationId::VMAND_MM:
        case isa::OperationId::VMNAND_MM:
        case isa::OperationId::VMANDN_MM:
        case isa::OperationId::VMOR_MM:
        case isa::OperationId::VMNOR_MM:
        case isa::OperationId::VMORN_MM:
        case isa::OperationId::VMXOR_MM:
        case isa::OperationId::VMXNOR_MM:
            return true;
        default:
            return false;
    }
}

constexpr auto is_vector_reduction(isa::OperationId op_id) -> bool {
    switch (op_id) {
        case isa::OperationId::VREDSUM_VS:
        case isa::OperationId::VREDAND_VS:
        case isa::OperationId::VREDOR_VS:
        case isa::OperationId::VREDXOR_VS:
        case isa::OperationId::VREDMINU_VS:
        case isa::OperationId::VREDMIN_VS:
        case isa::OperationId::VREDMAXU_VS:
        case isa::OperationId::VREDMAX_VS:
        case isa::OperationId::VWREDSUM_VS:
        case isa::OperationId::VWREDSUMU_VS:
        case isa::OperationId::VFREDUSUM_VS:
        case isa::OperationId::VFREDOSUM_VS:
        case isa::OperationId::VFREDMIN_VS:
        case isa::OperationId::VFREDMAX_VS:
        case isa::OperationId::VFWREDUSUM_VS:
        case isa::OperationId::VFWREDOSUM_VS:
            return true;
        default:
            return false;
    }
}

constexpr auto has_vector_destination(isa::OperationId op_id,
                                      const VectorMemoryLayout& memory_layout) -> bool {
    if (memory_layout.recognized) return !is_vector_store(op_id);
    const auto value = std::to_underlying(op_id);
    return value >= std::to_underlying(isa::OperationId::VADD_VV) &&
           value <= std::to_underlying(isa::OperationId::VWSLL_VI) &&
           !has_scalar_destination(op_id);
}

constexpr auto register_ranges_overlap(uint32_t first_a, uint32_t count_a, uint32_t first_b,
                                       uint32_t count_b) -> bool {
    return first_a < first_b + count_b && first_b < first_a + count_a;
}

constexpr auto whole_vector_memory_registers(isa::OperationId op_id) -> uint32_t {
    switch (op_id) {
        case isa::OperationId::VL1RE8_V:
        case isa::OperationId::VL1RE16_V:
        case isa::OperationId::VL1RE32_V:
        case isa::OperationId::VL1RE64_V:
        case isa::OperationId::VS1R_V:
            return 1;
        case isa::OperationId::VL2RE8_V:
        case isa::OperationId::VL2RE16_V:
        case isa::OperationId::VL2RE32_V:
        case isa::OperationId::VL2RE64_V:
        case isa::OperationId::VS2R_V:
            return 2;
        case isa::OperationId::VL4RE8_V:
        case isa::OperationId::VL4RE16_V:
        case isa::OperationId::VL4RE32_V:
        case isa::OperationId::VL4RE64_V:
        case isa::OperationId::VS4R_V:
            return 4;
        case isa::OperationId::VL8RE8_V:
        case isa::OperationId::VL8RE16_V:
        case isa::OperationId::VL8RE32_V:
        case isa::OperationId::VL8RE64_V:
        case isa::OperationId::VS8R_V:
            return 8;
        default:
            return 0;
    }
}

constexpr auto whole_vector_memory_eew(isa::OperationId op_id) -> uint32_t {
    switch (op_id) {
        case isa::OperationId::VL1RE16_V:
        case isa::OperationId::VL2RE16_V:
        case isa::OperationId::VL4RE16_V:
        case isa::OperationId::VL8RE16_V:
            return 16;
        case isa::OperationId::VL1RE32_V:
        case isa::OperationId::VL2RE32_V:
        case isa::OperationId::VL4RE32_V:
        case isa::OperationId::VL8RE32_V:
            return 32;
        case isa::OperationId::VL1RE64_V:
        case isa::OperationId::VL2RE64_V:
        case isa::OperationId::VL4RE64_V:
        case isa::OperationId::VL8RE64_V:
            return 64;
        default:
            // Whole-register stores and the remaining loads have EEW=8.
            return 8;
    }
}

constexpr auto lmul_ratio(Vlmul lmul) -> std::pair<uint32_t, uint32_t> {
    switch (lmul) {
        case Vlmul::LMUL_F8:
            return {1, 8};
        case Vlmul::LMUL_F4:
            return {1, 4};
        case Vlmul::LMUL_F2:
            return {1, 2};
        case Vlmul::LMUL_1:
            return {1, 1};
        case Vlmul::LMUL_2:
            return {2, 1};
        case Vlmul::LMUL_4:
            return {4, 1};
        case Vlmul::LMUL_8:
            return {8, 1};
        default:
            return {0, 1};
    }
}

constexpr auto valid_group(RegId base, uint32_t numerator, uint32_t denominator, uint32_t fields,
                           uint32_t& field_registers) -> bool {
    if (numerator == 0 || numerator > denominator * 8 || numerator * 8 < denominator) return false;
    field_registers = numerator > denominator ? numerator / denominator : 1;
    const uint32_t first = std::to_underlying(base);
    return first % field_registers == 0 && numerator * fields <= denominator * 8 &&
           first + field_registers * fields <= 32;
}

constexpr auto make_operand_group(RegId base, uint32_t lmul_numerator, uint32_t lmul_denominator,
                                  uint32_t width_numerator, uint32_t width_denominator,
                                  VectorOperandGroup& group) -> bool {
    uint32_t registers = 1;
    const uint32_t emul_numerator = lmul_numerator * width_numerator;
    const uint32_t emul_denominator = lmul_denominator * width_denominator;
    if (!valid_group(base, emul_numerator, emul_denominator, 1, registers)) return false;
    group = {.first = std::to_underlying(base),
             .registers = registers,
             .emul_numerator = emul_numerator,
             .emul_denominator = emul_denominator,
             .eew_numerator = width_numerator,
             .eew_denominator = width_denominator};
    return true;
}

constexpr auto same_eew(const VectorOperandGroup& lhs, const VectorOperandGroup& rhs) -> bool {
    return lhs.eew_numerator * rhs.eew_denominator == rhs.eew_numerator * lhs.eew_denominator;
}

constexpr auto narrower_eew(const VectorOperandGroup& lhs, const VectorOperandGroup& rhs) -> bool {
    return lhs.eew_numerator * rhs.eew_denominator < rhs.eew_numerator * lhs.eew_denominator;
}

constexpr auto emul_at_least_one(const VectorOperandGroup& group) -> bool {
    return group.emul_numerator >= group.emul_denominator;
}

constexpr auto legal_destination_overlap(const VectorOperandGroup& destination,
                                         const VectorOperandGroup& source) -> bool {
    if (!register_ranges_overlap(destination.first, destination.registers, source.first,
                                 source.registers)) {
        return true;
    }
    if (same_eew(destination, source)) return true;
    if (narrower_eew(destination, source)) return destination.first == source.first;
    return emul_at_least_one(source) &&
           destination.first + destination.registers == source.first + source.registers;
}

constexpr auto vector_compute_layout(isa::OperationId op_id, RegId destination, RegId source1,
                                     RegId source2, uint32_t sew, Vlmul lmul)
    -> VectorComputeLayout {
    enum class Layout : uint8_t {
        None,
        WidenVv,
        WidenVx,
        WidenWv,
        WidenWx,
        WidenAccVv,
        WidenAccVx,
        WidenConvert,
        NarrowWv,
        NarrowWx,
        NarrowConvert,
        Extend2,
        Extend4,
        Extend8,
    };

    Layout layout = Layout::None;
    switch (op_id) {
        case isa::OperationId::VWADD_VV:
        case isa::OperationId::VWADDU_VV:
        case isa::OperationId::VWSUB_VV:
        case isa::OperationId::VWSUBU_VV:
        case isa::OperationId::VWMUL_VV:
        case isa::OperationId::VWMULU_VV:
        case isa::OperationId::VWMULSU_VV:
        case isa::OperationId::VFWADD_VV:
        case isa::OperationId::VFWSUB_VV:
        case isa::OperationId::VFWMUL_VV:
            layout = Layout::WidenVv;
            break;
        case isa::OperationId::VWADD_VX:
        case isa::OperationId::VWADDU_VX:
        case isa::OperationId::VWSUB_VX:
        case isa::OperationId::VWSUBU_VX:
        case isa::OperationId::VWMUL_VX:
        case isa::OperationId::VWMULU_VX:
        case isa::OperationId::VWMULSU_VX:
        case isa::OperationId::VFWADD_VF:
        case isa::OperationId::VFWSUB_VF:
        case isa::OperationId::VFWMUL_VF:
            layout = Layout::WidenVx;
            break;
        case isa::OperationId::VWADD_WV:
        case isa::OperationId::VWADDU_WV:
        case isa::OperationId::VWSUB_WV:
        case isa::OperationId::VWSUBU_WV:
        case isa::OperationId::VFWADD_WV:
        case isa::OperationId::VFWSUB_WV:
            layout = Layout::WidenWv;
            break;
        case isa::OperationId::VWADD_WX:
        case isa::OperationId::VWADDU_WX:
        case isa::OperationId::VWSUB_WX:
        case isa::OperationId::VWSUBU_WX:
        case isa::OperationId::VFWADD_WF:
        case isa::OperationId::VFWSUB_WF:
            layout = Layout::WidenWx;
            break;
        case isa::OperationId::VWMACCU_VV:
        case isa::OperationId::VWMACC_VV:
        case isa::OperationId::VWMACCSU_VV:
        case isa::OperationId::VFWMACC_VV:
        case isa::OperationId::VFWNMACC_VV:
        case isa::OperationId::VFWMSAC_VV:
        case isa::OperationId::VFWNMSAC_VV:
            layout = Layout::WidenAccVv;
            break;
        case isa::OperationId::VWMACCU_VX:
        case isa::OperationId::VWMACC_VX:
        case isa::OperationId::VWMACCUS_VX:
        case isa::OperationId::VWMACCSU_VX:
        case isa::OperationId::VFWMACC_VF:
        case isa::OperationId::VFWNMACC_VF:
        case isa::OperationId::VFWMSAC_VF:
        case isa::OperationId::VFWNMSAC_VF:
            layout = Layout::WidenAccVx;
            break;
        case isa::OperationId::VFWCVT_XU_F_V:
        case isa::OperationId::VFWCVT_X_F_V:
        case isa::OperationId::VFWCVT_F_XU_V:
        case isa::OperationId::VFWCVT_F_X_V:
        case isa::OperationId::VFWCVT_F_F_V:
        case isa::OperationId::VFWCVT_RTZ_XU_F_V:
        case isa::OperationId::VFWCVT_RTZ_X_F_V:
            layout = Layout::WidenConvert;
            break;
        case isa::OperationId::VNCLIP_WV:
        case isa::OperationId::VNCLIPU_WV:
        case isa::OperationId::VNSRL_WV:
        case isa::OperationId::VNSRA_WV:
            layout = Layout::NarrowWv;
            break;
        case isa::OperationId::VNCLIP_WX:
        case isa::OperationId::VNCLIP_WI:
        case isa::OperationId::VNCLIPU_WX:
        case isa::OperationId::VNCLIPU_WI:
        case isa::OperationId::VNSRL_WX:
        case isa::OperationId::VNSRL_WI:
        case isa::OperationId::VNSRA_WX:
        case isa::OperationId::VNSRA_WI:
            layout = Layout::NarrowWx;
            break;
        case isa::OperationId::VFNCVT_XU_F_W:
        case isa::OperationId::VFNCVT_X_F_W:
        case isa::OperationId::VFNCVT_F_XU_W:
        case isa::OperationId::VFNCVT_F_X_W:
        case isa::OperationId::VFNCVT_F_F_W:
        case isa::OperationId::VFNCVT_ROD_F_F_W:
        case isa::OperationId::VFNCVT_RTZ_XU_F_W:
        case isa::OperationId::VFNCVT_RTZ_X_F_W:
            layout = Layout::NarrowConvert;
            break;
        case isa::OperationId::VSEXT_VF2:
        case isa::OperationId::VZEXT_VF2:
            layout = Layout::Extend2;
            break;
        case isa::OperationId::VSEXT_VF4:
        case isa::OperationId::VZEXT_VF4:
            layout = Layout::Extend4;
            break;
        case isa::OperationId::VSEXT_VF8:
        case isa::OperationId::VZEXT_VF8:
            layout = Layout::Extend8;
            break;
        default:
            return {};
    }

    const auto [lmul_numerator, lmul_denominator] = lmul_ratio(lmul);
    if (lmul_numerator == 0) return {.recognized = true};
    const bool uses_double_width = layout <= Layout::NarrowConvert;
    if ((uses_double_width && sew > 32) || (layout == Layout::Extend2 && sew < 16) ||
        (layout == Layout::Extend4 && sew < 32) || (layout == Layout::Extend8 && sew < 64)) {
        return {.recognized = true};
    }

    std::array<VectorOperandGroup, 4> groups{};
    uint32_t group_count = 0;
    const auto add_group = [&](RegId reg, uint32_t width_numerator,
                               uint32_t width_denominator) -> bool {
        return make_operand_group(reg, lmul_numerator, lmul_denominator, width_numerator,
                                  width_denominator, groups[group_count++]);
    };

    bool valid = false;
    switch (layout) {
        case Layout::WidenVv:
            valid = add_group(destination, 2, 1) && add_group(source2, 1, 1) &&
                    add_group(source1, 1, 1);
            break;
        case Layout::WidenVx:
            valid = add_group(destination, 2, 1) && add_group(source2, 1, 1);
            break;
        case Layout::WidenWv:
            valid = add_group(destination, 2, 1) && add_group(source2, 2, 1) &&
                    add_group(source1, 1, 1);
            break;
        case Layout::WidenWx:
            valid = add_group(destination, 2, 1) && add_group(source2, 2, 1);
            break;
        case Layout::WidenAccVv:
            valid = add_group(destination, 2, 1) && add_group(destination, 2, 1) &&
                    add_group(source2, 1, 1) && add_group(source1, 1, 1);
            break;
        case Layout::WidenAccVx:
            valid = add_group(destination, 2, 1) && add_group(destination, 2, 1) &&
                    add_group(source2, 1, 1);
            break;
        case Layout::WidenConvert:
            valid = add_group(destination, 2, 1) && add_group(source2, 1, 1);
            break;
        case Layout::NarrowWv:
            valid = add_group(destination, 1, 1) && add_group(source2, 2, 1) &&
                    add_group(source1, 1, 1);
            break;
        case Layout::NarrowWx:
        case Layout::NarrowConvert:
            valid = add_group(destination, 1, 1) && add_group(source2, 2, 1);
            break;
        case Layout::Extend2:
            valid = add_group(destination, 1, 1) && add_group(source2, 1, 2);
            break;
        case Layout::Extend4:
            valid = add_group(destination, 1, 1) && add_group(source2, 1, 4);
            break;
        case Layout::Extend8:
            valid = add_group(destination, 1, 1) && add_group(source2, 1, 8);
            break;
        case Layout::None:
            break;
    }
    if (!valid) return {.recognized = true};

    for (uint32_t i = 1; i < group_count; ++i) {
        if (!legal_destination_overlap(groups[0], groups[i])) return {.recognized = true};
        for (uint32_t j = i + 1; j < group_count; ++j) {
            if (!same_eew(groups[i], groups[j]) &&
                register_ranges_overlap(groups[i].first, groups[i].registers, groups[j].first,
                                        groups[j].registers)) {
                return {.recognized = true};
            }
        }
    }
    return {.recognized = true, .valid = true};
}

constexpr auto vector_permute_layout(isa::OperationId op_id, RegId destination, RegId source1,
                                     RegId source2, uint32_t sew, Vlmul lmul, uint32_t vlen,
                                     CSRValue vstart) -> VectorComputeLayout {
    enum class Layout : uint8_t {
        None,
        SlideUp,
        GatherOne,
        GatherTwo,
        GatherEi16,
        Compress,
        Whole1,
        Whole2,
        Whole4,
        Whole8,
    };
    Layout layout = Layout::None;
    switch (op_id) {
        case isa::OperationId::VSLIDEUP_VX:
        case isa::OperationId::VSLIDEUP_VI:
        case isa::OperationId::VSLIDE1UP_VX:
        case isa::OperationId::VFSLIDE1UP_VF:
            layout = Layout::SlideUp;
            break;
        case isa::OperationId::VRGATHER_VX:
        case isa::OperationId::VRGATHER_VI:
            layout = Layout::GatherOne;
            break;
        case isa::OperationId::VRGATHER_VV:
            layout = Layout::GatherTwo;
            break;
        case isa::OperationId::VRGATHEREI16_VV:
            layout = Layout::GatherEi16;
            break;
        case isa::OperationId::VCOMPRESS_VM:
            layout = Layout::Compress;
            break;
        case isa::OperationId::VMV1R_V:
            layout = Layout::Whole1;
            break;
        case isa::OperationId::VMV2R_V:
            layout = Layout::Whole2;
            break;
        case isa::OperationId::VMV4R_V:
            layout = Layout::Whole4;
            break;
        case isa::OperationId::VMV8R_V:
            layout = Layout::Whole8;
            break;
        default:
            return {};
    }

    if (layout >= Layout::Whole1) {
        const uint32_t registers =
            1U << (std::to_underlying(layout) - std::to_underlying(Layout::Whole1));
        const uint32_t destination_index = std::to_underlying(destination);
        const uint32_t source_index = std::to_underlying(source2);
        const uint64_t evl = static_cast<uint64_t>(registers) * vlen / sew;
        return {.recognized = true,
                .valid = destination_index % registers == 0 && source_index % registers == 0 &&
                         destination_index + registers <= 32 && source_index + registers <= 32 &&
                         vstart < evl};
    }

    const auto [lmul_numerator, lmul_denominator] = lmul_ratio(lmul);
    if (lmul_numerator == 0) return {.recognized = true};
    std::array<VectorOperandGroup, 3> groups{};
    if (!make_operand_group(destination, lmul_numerator, lmul_denominator, 1, 1, groups[0]) ||
        !make_operand_group(source2, lmul_numerator, lmul_denominator, 1, 1, groups[1])) {
        return {.recognized = true};
    }
    if (register_ranges_overlap(groups[0].first, groups[0].registers, groups[1].first,
                                groups[1].registers)) {
        return {.recognized = true};
    }

    if (layout == Layout::GatherTwo || layout == Layout::GatherEi16) {
        const uint32_t width_numerator = layout == Layout::GatherEi16 ? 16 : 1;
        const uint32_t width_denominator = layout == Layout::GatherEi16 ? sew : 1;
        if (!make_operand_group(source1, lmul_numerator, lmul_denominator, width_numerator,
                                width_denominator, groups[2]) ||
            register_ranges_overlap(groups[0].first, groups[0].registers, groups[2].first,
                                    groups[2].registers)) {
            return {.recognized = true};
        }
        if (!same_eew(groups[1], groups[2]) &&
            register_ranges_overlap(groups[1].first, groups[1].registers, groups[2].first,
                                    groups[2].registers)) {
            return {.recognized = true};
        }
    } else if (layout == Layout::Compress) {
        const uint32_t mask_register = std::to_underlying(source1);
        if (register_ranges_overlap(groups[0].first, groups[0].registers, mask_register, 1) ||
            register_ranges_overlap(groups[1].first, groups[1].registers, mask_register, 1)) {
            return {.recognized = true};
        }
    }
    return {.recognized = true, .valid = true};
}

constexpr auto ordinary_vector_groups_valid(isa::OperationId op_id, RegId destination,
                                            RegId source1, RegId source2, Vlmul lmul,
                                            bool special_layout) -> bool {
    if (special_layout || is_vector_reduction(op_id) || has_mask_destination(op_id) ||
        has_scalar_destination(op_id) || op_id == isa::OperationId::VIOTA_M ||
        op_id == isa::OperationId::VID_V || op_id == isa::OperationId::VMV_S_X ||
        op_id == isa::OperationId::VFMV_S_F) {
        return true;
    }
    const auto value = std::to_underlying(op_id);
    if (value < std::to_underlying(isa::OperationId::VADD_VV) ||
        value > std::to_underlying(isa::OperationId::VWSLL_VI)) {
        return true;
    }
    const auto operands = pipeline::operation::info(op_id).operands;
    if (operands.rd != pipeline::operation::RegBank::Vector) return true;
    const auto [numerator, denominator] = lmul_ratio(lmul);
    uint32_t registers = 1;
    if (!valid_group(destination, numerator, denominator, 1, registers)) return false;
    if (operands.rs1 == pipeline::operation::RegBank::Vector &&
        !valid_group(source1, numerator, denominator, 1, registers)) {
        return false;
    }
    return operands.rs2 != pipeline::operation::RegBank::Vector ||
           valid_group(source2, numerator, denominator, 1, registers);
}

constexpr auto reduction_groups_valid(isa::OperationId op_id, RegId scalar_source,
                                      RegId vector_source, uint32_t sew, Vlmul lmul) -> bool {
    if (!is_vector_reduction(op_id)) return true;
    const auto [numerator, denominator] = lmul_ratio(lmul);
    uint32_t vector_registers = 1;
    if (!valid_group(vector_source, numerator, denominator, 1, vector_registers)) return false;
    const bool widening =
        op_id == isa::OperationId::VWREDSUM_VS || op_id == isa::OperationId::VWREDSUMU_VS ||
        op_id == isa::OperationId::VFWREDUSUM_VS || op_id == isa::OperationId::VFWREDOSUM_VS;
    if (!widening) return true;
    if (sew > 32) return false;
    return !register_ranges_overlap(std::to_underlying(scalar_source), 1,
                                    std::to_underlying(vector_source), vector_registers);
}

constexpr auto mask_operand_groups_valid(isa::OperationId op_id, RegId destination, RegId source1,
                                         RegId source2, bool vm, Vlmul lmul) -> bool {
    const bool mask_prefix = op_id == isa::OperationId::VMSBF_M ||
                             op_id == isa::OperationId::VMSIF_M ||
                             op_id == isa::OperationId::VMSOF_M;
    if (mask_prefix) return destination != source2 && (vm || destination != RegId::Zero);

    if (op_id == isa::OperationId::VIOTA_M) {
        const auto [numerator, denominator] = lmul_ratio(lmul);
        uint32_t registers = 1;
        return valid_group(destination, numerator, denominator, 1, registers) &&
               !register_ranges_overlap(std::to_underlying(destination), registers,
                                        std::to_underlying(source2), 1) &&
               (vm || destination != RegId::Zero);
    }

    if (!has_mask_destination(op_id)) return true;
    switch (op_id) {
        case isa::OperationId::VMAND_MM:
        case isa::OperationId::VMNAND_MM:
        case isa::OperationId::VMANDN_MM:
        case isa::OperationId::VMOR_MM:
        case isa::OperationId::VMNOR_MM:
        case isa::OperationId::VMORN_MM:
        case isa::OperationId::VMXOR_MM:
        case isa::OperationId::VMXNOR_MM:
            return true;  // All three operands are single-register masks.
        default:
            break;
    }

    const auto [numerator, denominator] = lmul_ratio(lmul);
    uint32_t registers = 1;
    if (!valid_group(source2, numerator, denominator, 1, registers)) return false;
    const auto destination_index = std::to_underlying(destination);
    const auto source2_index = std::to_underlying(source2);
    if (register_ranges_overlap(destination_index, 1, source2_index, registers) &&
        destination_index != source2_index) {
        return false;
    }
    if (pipeline::operation::info(op_id).operands.rs1 == pipeline::operation::RegBank::Vector) {
        if (!valid_group(source1, numerator, denominator, 1, registers)) return false;
        const auto source1_index = std::to_underlying(source1);
        if (register_ranges_overlap(destination_index, 1, source1_index, registers) &&
            destination_index != source1_index) {
            return false;
        }
    }
    return true;
}

constexpr auto vector_memory_layout(isa::OperationId op_id, RegId data_reg, RegId index_reg,
                                    uint32_t sew, uint32_t nfields, Vlmul lmul, uint32_t vlen,
                                    CSRValue vstart) -> VectorMemoryLayout {
    if (op_id == isa::OperationId::VLM_V || op_id == isa::OperationId::VSM_V) {
        return {.recognized = true, .valid = nfields == 1, .field_registers = 1};
    }

    if (const uint32_t whole_registers = whole_vector_memory_registers(op_id);
        whole_registers != 0) {
        const uint32_t first = std::to_underlying(data_reg);
        const uint64_t evl =
            static_cast<uint64_t>(whole_registers) * vlen / whole_vector_memory_eew(op_id);
        return {
            .recognized = true,
            .valid = first % whole_registers == 0 && first + whole_registers <= 32 && vstart < evl,
            .field_registers = whole_registers};
    }

    const uint32_t eew = vector_memory_eew(op_id);
    if (eew == 0) return {};
    const auto [lmul_num, lmul_den] = lmul_ratio(lmul);
    if (lmul_num == 0) return {.recognized = true};

    uint32_t data_num = lmul_num * eew;
    uint32_t data_den = lmul_den * sew;
    uint32_t index_num = data_num;
    uint32_t index_den = data_den;
    if (is_indexed_vector_memory(op_id)) {
        data_num = lmul_num;
        data_den = lmul_den;
        index_num = lmul_num * eew;
        index_den = lmul_den * sew;
    }

    uint32_t field_registers = 1;
    if (!valid_group(data_reg, data_num, data_den, nfields, field_registers)) {
        return {.recognized = true};
    }
    if (is_indexed_vector_memory(op_id)) {
        uint32_t index_registers = 1;
        if (!valid_group(index_reg, index_num, index_den, 1, index_registers)) {
            return {.recognized = true};
        }
        if (nfields > 1 && is_indexed_vector_load(op_id) &&
            register_ranges_overlap(std::to_underlying(data_reg), field_registers * nfields,
                                    std::to_underlying(index_reg), index_registers)) {
            return {.recognized = true};
        }
    }
    return {.recognized = true, .valid = true, .field_registers = field_registers};
}
}  // namespace

void ExecuteUnit::execute_vector(core::CPU& cpu, memory::MemorySubsystem& mem,
                                 isa::OperationId op_id, Instruction ir) {
    if (cpu.state().regs.xlen == 32 && isa::requires_rv64(op_id)) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }
    if (cpu.state().vstart != 0 && requires_zero_vstart(op_id)) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }

    const auto rd = static_cast<RegId>((ir >> 7) & 0x1F);
    const auto rs1 = static_cast<RegId>((ir >> 15) & 0x1F);
    const auto rs2 = static_cast<RegId>((ir >> 20) & 0x1F);
    const bool vm = ((ir >> 25) & 1) != 0;
    if ((op_id == isa::OperationId::VMV_V_V || op_id == isa::OperationId::VMV_V_X ||
         op_id == isa::OperationId::VMV_V_I || op_id == isa::OperationId::VFMV_V_F) &&
        (rs2 != RegId::Zero || !vm)) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }

    const bool is_configuration = op_id == isa::OperationId::VSETVLI ||
                                  op_id == isa::OperationId::VSETIVLI ||
                                  op_id == isa::OperationId::VSETVL;
    const bool whole_register_memory = whole_vector_memory_registers(op_id) != 0;
    const VtypeView vtype{.raw = cpu.state().vtype,
                          .xlen = static_cast<uint8_t>(cpu.state().regs.xlen)};
    if (!is_configuration && !whole_register_memory && vtype.vill()) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }

    const bool whole_register_move =
        op_id == isa::OperationId::VMV1R_V || op_id == isa::OperationId::VMV2R_V ||
        op_id == isa::OperationId::VMV4R_V || op_id == isa::OperationId::VMV8R_V;
    if (!is_configuration && !whole_register_memory && !whole_register_move) {
        const auto [lmul_numerator, lmul_denominator] = lmul_ratio(vtype.vlmul());
        const uint32_t sew_for_vlmax = 8U << ((cpu.state().vtype >> 3U) & 0x7U);
        const uint64_t vlmax = static_cast<uint64_t>(cpu.state().regs.vlen) * lmul_numerator /
                               (sew_for_vlmax * lmul_denominator);
        if (lmul_numerator == 0 || cpu.state().vstart >= vlmax) {
            cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
            cpu.active_context().pending_tval = ir;
            return;
        }
    }

    // Decode immediate fields
    auto simm5 = static_cast<int32_t>((ir >> 15) & 0x1F);
    if ((simm5 & 0x10) != 0) {
        simm5 |= ~0x1F;
    }

    uint32_t vl = cpu.state().vl;
    uint32_t sew = 8 << ((cpu.state().vtype >> 3) & 0x7);
    const uint32_t nfields = ((ir >> 29U) & 7U) + 1U;
    const auto memory_layout = vector_memory_layout(op_id, rd, rs2, sew, nfields, vtype.vlmul(),
                                                    cpu.state().regs.vlen, cpu.state().vstart);
    if (memory_layout.recognized && !memory_layout.valid) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }
    if (!vm && rd == RegId::Zero && has_vector_destination(op_id, memory_layout) &&
        !has_mask_destination(op_id) && !is_vector_reduction(op_id)) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }
    const auto compute_layout = vector_compute_layout(op_id, rd, rs1, rs2, sew, vtype.vlmul());
    if (compute_layout.recognized && !compute_layout.valid) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }
    const auto permute_layout = vector_permute_layout(op_id, rd, rs1, rs2, sew, vtype.vlmul(),
                                                      cpu.state().regs.vlen, cpu.state().vstart);
    if (permute_layout.recognized && !permute_layout.valid) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }
    if (!memory_layout.recognized &&
        !ordinary_vector_groups_valid(op_id, rd, rs1, rs2, vtype.vlmul(),
                                      compute_layout.recognized || permute_layout.recognized)) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }
    if (!reduction_groups_valid(op_id, rs1, rs2, sew, vtype.vlmul())) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }
    if (!mask_operand_groups_valid(op_id, rd, rs1, rs2, vm, vtype.vlmul())) {
        cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
        cpu.active_context().pending_tval = ir;
        return;
    }

    if (is_vector_fp(op_id)) {
        const bool has_f = isa::misa_has_extension(cpu.state().misa, isa::IsaExtension::F);
        const bool has_d = isa::misa_has_extension(cpu.state().misa, isa::IsaExtension::D);
        const bool scalar_fp_enabled =
            (sew == 32 && has_f && (!uses_wide_fp_element(op_id) || has_d)) ||
            (sew == 64 && has_f && has_d);
        const bool fs_enabled =
            (cpu.state().mstatus & enum_mask(core::MstatusBit::Fs)) != static_cast<CSRValue>(0);
        const Word frm = (cpu.state().fcsr >> 5U) & 0x7U;
        if (!scalar_fp_enabled || !fs_enabled || (is_vector_fp_arithmetic(op_id) && frm >= 5U)) {
            // SEW=16 vector FP belongs to Zvfh, which SimRV does not advertise.
            cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
            cpu.active_context().pending_tval = ir;
            return;
        }
    }

    switch (op_id) {
        // Configuration
        case isa::OperationId::VSETVLI:
        case isa::OperationId::VSETIVLI:
        case isa::OperationId::VSETVL:
            execute_vector_config(cpu, op_id, ir, rd, rs1, rs2);
            break;

        // Memory
        case isa::OperationId::VLE8_V:
        case isa::OperationId::VLE16_V:
        case isa::OperationId::VLE32_V:
        case isa::OperationId::VLE64_V:
        case isa::OperationId::VLE8FF_V:
        case isa::OperationId::VLE16FF_V:
        case isa::OperationId::VLE32FF_V:
        case isa::OperationId::VLE64FF_V:
        case isa::OperationId::VLM_V:
        case isa::OperationId::VSE8_V:
        case isa::OperationId::VSE16_V:
        case isa::OperationId::VSE32_V:
        case isa::OperationId::VSE64_V:
        case isa::OperationId::VSM_V:
        case isa::OperationId::VLSE8_V:
        case isa::OperationId::VLSE16_V:
        case isa::OperationId::VLSE32_V:
        case isa::OperationId::VLSE64_V:
        case isa::OperationId::VSSE8_V:
        case isa::OperationId::VSSE16_V:
        case isa::OperationId::VSSE32_V:
        case isa::OperationId::VSSE64_V:
        case isa::OperationId::VLUXEI8_V:
        case isa::OperationId::VLUXEI16_V:
        case isa::OperationId::VLUXEI32_V:
        case isa::OperationId::VLUXEI64_V:
        case isa::OperationId::VLOXEI8_V:
        case isa::OperationId::VLOXEI16_V:
        case isa::OperationId::VLOXEI32_V:
        case isa::OperationId::VLOXEI64_V:
        case isa::OperationId::VSUXEI8_V:
        case isa::OperationId::VSUXEI16_V:
        case isa::OperationId::VSUXEI32_V:
        case isa::OperationId::VSUXEI64_V:
        case isa::OperationId::VSOXEI8_V:
        case isa::OperationId::VSOXEI16_V:
        case isa::OperationId::VSOXEI32_V:
        case isa::OperationId::VSOXEI64_V:
        case isa::OperationId::VL1RE8_V:
        case isa::OperationId::VL1RE16_V:
        case isa::OperationId::VL1RE32_V:
        case isa::OperationId::VL1RE64_V:
        case isa::OperationId::VL2RE8_V:
        case isa::OperationId::VL2RE16_V:
        case isa::OperationId::VL2RE32_V:
        case isa::OperationId::VL2RE64_V:
        case isa::OperationId::VL4RE8_V:
        case isa::OperationId::VL4RE16_V:
        case isa::OperationId::VL4RE32_V:
        case isa::OperationId::VL4RE64_V:
        case isa::OperationId::VL8RE8_V:
        case isa::OperationId::VL8RE16_V:
        case isa::OperationId::VL8RE32_V:
        case isa::OperationId::VL8RE64_V:
        case isa::OperationId::VS1R_V:
        case isa::OperationId::VS2R_V:
        case isa::OperationId::VS4R_V:
        case isa::OperationId::VS8R_V:
            execute_vector_memory(cpu, mem, op_id, rd, rs1, rs2, vm, vl, sew, nfields,
                                  memory_layout.field_registers);
            break;

        // Floating-point
        case isa::OperationId::VFADD_VV:
        case isa::OperationId::VFADD_VF:
        case isa::OperationId::VFSUB_VV:
        case isa::OperationId::VFSUB_VF:
        case isa::OperationId::VFRSUB_VF:
        case isa::OperationId::VFMUL_VV:
        case isa::OperationId::VFMUL_VF:
        case isa::OperationId::VFDIV_VV:
        case isa::OperationId::VFDIV_VF:
        case isa::OperationId::VFRDIV_VF:
        case isa::OperationId::VFSQRT_V:
        case isa::OperationId::VFRSQRT7_V:
        case isa::OperationId::VFREC7_V:
        case isa::OperationId::VFCLASS_V:
        case isa::OperationId::VFCVT_XU_F_V:
        case isa::OperationId::VFCVT_X_F_V:
        case isa::OperationId::VFCVT_F_XU_V:
        case isa::OperationId::VFCVT_F_X_V:
        case isa::OperationId::VFCVT_RTZ_XU_F_V:
        case isa::OperationId::VFCVT_RTZ_X_F_V:
        case isa::OperationId::VFWCVT_XU_F_V:
        case isa::OperationId::VFWCVT_X_F_V:
        case isa::OperationId::VFWCVT_F_XU_V:
        case isa::OperationId::VFWCVT_F_X_V:
        case isa::OperationId::VFWCVT_F_F_V:
        case isa::OperationId::VFWCVT_RTZ_XU_F_V:
        case isa::OperationId::VFWCVT_RTZ_X_F_V:
        case isa::OperationId::VFNCVT_XU_F_W:
        case isa::OperationId::VFNCVT_X_F_W:
        case isa::OperationId::VFNCVT_F_XU_W:
        case isa::OperationId::VFNCVT_F_X_W:
        case isa::OperationId::VFNCVT_F_F_W:
        case isa::OperationId::VFNCVT_ROD_F_F_W:
        case isa::OperationId::VFNCVT_RTZ_XU_F_W:
        case isa::OperationId::VFNCVT_RTZ_X_F_W:
        case isa::OperationId::VFSGNJ_VV:
        case isa::OperationId::VFSGNJ_VF:
        case isa::OperationId::VFSGNJN_VV:
        case isa::OperationId::VFSGNJN_VF:
        case isa::OperationId::VFSGNJX_VV:
        case isa::OperationId::VFSGNJX_VF:
        case isa::OperationId::VFMIN_VV:
        case isa::OperationId::VFMIN_VF:
        case isa::OperationId::VFMAX_VV:
        case isa::OperationId::VFMAX_VF:
        case isa::OperationId::VMFEQ_VV:
        case isa::OperationId::VMFEQ_VF:
        case isa::OperationId::VMFNE_VV:
        case isa::OperationId::VMFNE_VF:
        case isa::OperationId::VMFLT_VV:
        case isa::OperationId::VMFLT_VF:
        case isa::OperationId::VMFLE_VV:
        case isa::OperationId::VMFLE_VF:
        case isa::OperationId::VMFGT_VF:
        case isa::OperationId::VMFGE_VF:
        case isa::OperationId::VFMADD_VV:
        case isa::OperationId::VFMADD_VF:
        case isa::OperationId::VFNMADD_VV:
        case isa::OperationId::VFNMADD_VF:
        case isa::OperationId::VFMSUB_VV:
        case isa::OperationId::VFMSUB_VF:
        case isa::OperationId::VFNMSUB_VV:
        case isa::OperationId::VFNMSUB_VF:
        case isa::OperationId::VFMACC_VV:
        case isa::OperationId::VFMACC_VF:
        case isa::OperationId::VFNMACC_VV:
        case isa::OperationId::VFNMACC_VF:
        case isa::OperationId::VFMSAC_VV:
        case isa::OperationId::VFMSAC_VF:
        case isa::OperationId::VFNMSAC_VV:
        case isa::OperationId::VFNMSAC_VF:
        case isa::OperationId::VFREDUSUM_VS:
        case isa::OperationId::VFREDOSUM_VS:
        case isa::OperationId::VFREDMIN_VS:
        case isa::OperationId::VFREDMAX_VS:
        case isa::OperationId::VFWREDUSUM_VS:
        case isa::OperationId::VFWREDOSUM_VS:
        case isa::OperationId::VFWADD_VV:
        case isa::OperationId::VFWADD_VF:
        case isa::OperationId::VFWSUB_VV:
        case isa::OperationId::VFWSUB_VF:
        case isa::OperationId::VFWADD_WV:
        case isa::OperationId::VFWADD_WF:
        case isa::OperationId::VFWSUB_WV:
        case isa::OperationId::VFWSUB_WF:
        case isa::OperationId::VFWMUL_VV:
        case isa::OperationId::VFWMUL_VF:
        case isa::OperationId::VFWMACC_VV:
        case isa::OperationId::VFWMACC_VF:
        case isa::OperationId::VFWNMACC_VV:
        case isa::OperationId::VFWNMACC_VF:
        case isa::OperationId::VFWMSAC_VV:
        case isa::OperationId::VFWMSAC_VF:
        case isa::OperationId::VFWNMSAC_VV:
        case isa::OperationId::VFWNMSAC_VF:
            execute_vector_float(cpu, op_id, rd, rs1, rs2, vm, vl, sew);
            break;

        // Fixed-point
        case isa::OperationId::VSADD_VV:
        case isa::OperationId::VSADD_VX:
        case isa::OperationId::VSADD_VI:
        case isa::OperationId::VSADDU_VV:
        case isa::OperationId::VSADDU_VX:
        case isa::OperationId::VSADDU_VI:
        case isa::OperationId::VAADDU_VV:
        case isa::OperationId::VAADDU_VX:
        case isa::OperationId::VAADD_VV:
        case isa::OperationId::VAADD_VX:
        case isa::OperationId::VASUBU_VV:
        case isa::OperationId::VASUBU_VX:
        case isa::OperationId::VASUB_VV:
        case isa::OperationId::VASUB_VX:
        case isa::OperationId::VSSUB_VV:
        case isa::OperationId::VSSUB_VX:
        case isa::OperationId::VSSUBU_VV:
        case isa::OperationId::VSSUBU_VX:
        case isa::OperationId::VSMUL_VV:
        case isa::OperationId::VSMUL_VX:
        case isa::OperationId::VSSRA_VV:
        case isa::OperationId::VSSRA_VX:
        case isa::OperationId::VSSRA_VI:
        case isa::OperationId::VSSRL_VV:
        case isa::OperationId::VSSRL_VX:
        case isa::OperationId::VSSRL_VI:
        case isa::OperationId::VNCLIP_WV:
        case isa::OperationId::VNCLIP_WX:
        case isa::OperationId::VNCLIP_WI:
        case isa::OperationId::VNCLIPU_WV:
        case isa::OperationId::VNCLIPU_WX:
        case isa::OperationId::VNCLIPU_WI:
            execute_vector_fixed_point(cpu, op_id, rd, rs1, rs2, vm, vl, sew,
                                       cpu.state().regs.read(rs1), simm5);
            break;

        // Permutations
        case isa::OperationId::VMV_X_S:
        case isa::OperationId::VMV_S_X:
        case isa::OperationId::VFMV_F_S:
        case isa::OperationId::VFMV_S_F:
        case isa::OperationId::VFMERGE_VFM:
        case isa::OperationId::VFMV_V_F:
        case isa::OperationId::VMERGE_VVM:
        case isa::OperationId::VMERGE_VXM:
        case isa::OperationId::VMERGE_VIM:
        case isa::OperationId::VID_V:
        case isa::OperationId::VMV_V_V:
        case isa::OperationId::VMV_V_X:
        case isa::OperationId::VMV_V_I:
        case isa::OperationId::VSLIDE1UP_VX:
        case isa::OperationId::VSLIDE1DOWN_VX:
        case isa::OperationId::VFSLIDE1UP_VF:
        case isa::OperationId::VFSLIDE1DOWN_VF:
        case isa::OperationId::VSLIDEDOWN_VX:
        case isa::OperationId::VSLIDEDOWN_VI:
        case isa::OperationId::VSLIDEUP_VX:
        case isa::OperationId::VSLIDEUP_VI:
        case isa::OperationId::VRGATHER_VV:
        case isa::OperationId::VRGATHER_VX:
        case isa::OperationId::VRGATHER_VI:
        case isa::OperationId::VRGATHEREI16_VV:
        case isa::OperationId::VMV1R_V:
        case isa::OperationId::VMV2R_V:
        case isa::OperationId::VMV4R_V:
        case isa::OperationId::VMV8R_V:
        case isa::OperationId::VCOMPRESS_VM:
            execute_vector_permute(cpu, op_id, rd, rs1, rs2, vm, vl, sew,
                                   cpu.state().regs.read(rs1), simm5);
            break;

        default:
            if (!execute_vector_integer(cpu, op_id, rd, rs1, rs2, vm, vl, sew,
                                        cpu.state().regs.read(rs1), simm5)) {
                cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
                cpu.active_context().pending_tval = ir;
            }
            break;
    }

    // Vector 1.0 requires every successfully completed vector instruction, including vset*vl*, to
    // reset vstart. Faulting vector memory operations leave the faulting element index for resume.
    if (!cpu.active_context().pending_exception.has_value()) {
        cpu.state().mstatus |= enum_mask(core::MstatusBit::Vs);
        if (is_vector_fp(op_id)) {
            // Vector FP may update scalar FP registers or fflags. Setting FS Dirty
            // conservatively is permitted even when a particular result is exact.
            cpu.state().mstatus |= enum_mask(core::MstatusBit::Fs);
        }
        cpu.state().vstart = 0;
    }
}

void ExecuteUnit::execute_vector(core::CPU& cpu, core::Machine& machine, isa::OperationId op_id,
                                 Instruction ir) {
    execute_vector(cpu, machine.memory(), op_id, ir);
}

}  // namespace simrv::execute
