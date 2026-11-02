/**
 * @file VectorSpecCatalog.cpp
 * @brief RVV 1.0, Zvbb, and Zvbc vector instruction specification descriptions.
 *
 * Citations reference The RISC-V Instruction Set Manual, Volume I (Version 20260120), Chapter 30.
 */
#include "ExplainSpecInternal.hpp"
#include "simrv/isa/OperationId.hpp"

namespace simrv::explain::detail {

using enum simrv::isa::OperationId;

void populate_vector_descriptions(OpDescTable& arr) {
    arr[VSETVLI] = {"VSETVLI",
                    "Set Vector Length & Type Immediate. Updates vtype from zimm[10:0] and "
                    "vl from AVL in rs1, writing the new vl into rd."};
    arr[VSETIVLI] = {"VSETIVLI",
                     "Set Vector Length & Type (Immediate AVL). Updates vtype from zimm[9:0] "
                     "and vl from 5-bit uimm AVL, writing the new vl into rd."};
    arr[VSETVL] = {"VSETVL",
                   "Set Vector Length & Type Register. Updates vtype from rs2 and vl from "
                   "AVL in rs1, writing the new vl into rd."};
    arr[VLE8_V] = {"VLE8.V",
                   "Vector Unit-Stride Load (8-bit elements). Loads vl 8-bit elements from "
                   "memory starting at address rs1 into vector register group vd."};
    arr[VLE16_V] = {"VLE16.V",
                    "Vector Unit-Stride Load (16-bit elements). Loads vl 16-bit elements from "
                    "memory starting at address rs1 into vector register group vd."};
    arr[VLE32_V] = {"VLE32.V",
                    "Vector Unit-Stride Load (32-bit elements). Loads vl 32-bit elements from "
                    "memory starting at address rs1 into vector register group vd."};
    arr[VLE64_V] = {"VLE64.V",
                    "Vector Unit-Stride Load (64-bit elements). Loads vl 64-bit elements from "
                    "memory starting at address rs1 into vector register group vd."};
    arr[VSE8_V] = {"VSE8.V",
                   "Vector Unit-Stride Store (8-bit elements). Stores vl 8-bit elements from "
                   "vector register group vs3 to memory starting at address rs1."};
    arr[VSE16_V] = {"VSE16.V",
                    "Vector Unit-Stride Store (16-bit elements). Stores vl 16-bit elements "
                    "from vector register group vs3 to memory starting at address rs1."};
    arr[VSE32_V] = {"VSE32.V",
                    "Vector Unit-Stride Store (32-bit elements). Stores vl 32-bit elements "
                    "from vector register group vs3 to memory starting at address rs1."};
    arr[VSE64_V] = {"VSE64.V",
                    "Vector Unit-Stride Store (64-bit elements). Stores vl 64-bit elements "
                    "from vector register group vs3 to memory starting at address rs1."};
    arr[VADD_VV] = {"VADD.VV",
                    "Vector-Vector Integer Add. Adds active elements of vector vs2 and vs1 "
                    "and writes the SEW-bit sums to vd."};
    arr[VADD_VX] = {"VADD.VX",
                    "Vector-Scalar Integer Add. Adds scalar register rs1 to active elements "
                    "of vector vs2 and writes the SEW-bit sums to vd."};
    arr[VADD_VI] = {"VADD.VI",
                    "Vector-Immediate Integer Add. Adds sign-extended 5-bit simm to active "
                    "elements of vector vs2 and writes the SEW-bit sums to vd."};
    arr[VSUB_VV] = {"VSUB.VV",
                    "Vector-Vector Integer Subtract. Subtracts elements of vs1 from vs2 "
                    "(vs2[i] - vs1[i]) and writes results to vd."};
    arr[VSUB_VX] = {"VSUB.VX",
                    "Vector-Scalar Integer Subtract. Subtracts scalar rs1 from elements of "
                    "vs2 (vs2[i] - x[rs1]) and writes results to vd."};
    arr[VMUL_VV] = {"VMUL.VV",
                    "Vector-Vector Multiply. Multiplies active elements of vs2 and vs1 and "
                    "writes the lower SEW bits of each product to vd."};
    arr[VMUL_VX] = {"VMUL.VX",
                    "Vector-Scalar Multiply. Multiplies active elements of vs2 by scalar rs1 "
                    "and writes the lower SEW bits of each product to vd."};
    arr[VDIV_VV] = {"VDIV.VV",
                    "Vector-Vector Signed Divide. Divides signed elements of vs2 by signed "
                    "elements of vs1 and writes quotients to vd."};
    arr[VDIV_VX] = {"VDIV.VX",
                    "Vector-Scalar Signed Divide. Divides signed elements of vs2 by signed "
                    "scalar rs1 and writes quotients to vd."};
    arr[VDIVU_VV] = {"VDIVU.VV",
                     "Vector-Vector Unsigned Divide. Divides unsigned elements of vs2 by "
                     "unsigned elements of vs1 and writes quotients to vd."};
    arr[VDIVU_VX] = {"VDIVU.VX",
                     "Vector-Scalar Unsigned Divide. Divides unsigned elements of vs2 by "
                     "unsigned scalar rs1 and writes quotients to vd."};
    arr[VAND_VV] = {"VAND.VV",
                    "Vector-Vector Bitwise AND. Performs element-wise bitwise AND of vs2 and "
                    "vs1 and writes results to vd."};
    arr[VAND_VX] = {"VAND.VX",
                    "Vector-Scalar Bitwise AND. Performs element-wise bitwise AND of vs2 and "
                    "scalar rs1 and writes results to vd."};
    arr[VAND_VI] = {"VAND.VI",
                    "Vector-Immediate Bitwise AND. Performs element-wise bitwise AND of vs2 "
                    "and sign-extended simm and writes results to vd."};
    arr[VOR_VV] = {"VOR.VV",
                   "Vector-Vector Bitwise OR. Performs element-wise bitwise OR of vs2 and vs1 "
                   "and writes results to vd."};
    arr[VOR_VX] = {"VOR.VX",
                   "Vector-Scalar Bitwise OR. Performs element-wise bitwise OR of vs2 and "
                   "scalar rs1 and writes results to vd."};
    arr[VOR_VI] = {"VOR.VI",
                   "Vector-Immediate Bitwise OR. Performs element-wise bitwise OR of vs2 and "
                   "sign-extended simm and writes results to vd."};
    arr[VXOR_VV] = {"VXOR.VV",
                    "Vector-Vector Bitwise XOR. Performs element-wise bitwise XOR of vs2 and "
                    "vs1 and writes results to vd."};
    arr[VXOR_VX] = {"VXOR.VX",
                    "Vector-Scalar Bitwise XOR. Performs element-wise bitwise XOR of vs2 and "
                    "scalar rs1 and writes results to vd."};
    arr[VXOR_VI] = {"VXOR.VI",
                    "Vector-Immediate Bitwise XOR. Performs element-wise bitwise XOR of vs2 "
                    "and sign-extended simm and writes results to vd."};
    arr[VSLL_VV] = {"VSLL.VV",
                    "Vector-Vector Shift Left Logical. Shifts elements of vs2 left by shift "
                    "amounts in vs1 and writes results to vd."};
    arr[VSLL_VX] = {"VSLL.VX",
                    "Vector-Scalar Shift Left Logical. Shifts elements of vs2 left by shift "
                    "amount in scalar rs1 and writes results to vd."};
    arr[VSLL_VI] = {"VSLL.VI",
                    "Vector-Immediate Shift Left Logical. Shifts elements of vs2 left by "
                    "uimm shift amount and writes results to vd."};
    arr[VSRL_VV] = {"VSRL.VV",
                    "Vector-Vector Shift Right Logical. Logically shifts elements of vs2 "
                    "right by shift amounts in vs1 and writes results to vd."};
    arr[VSRL_VX] = {"VSRL.VX",
                    "Vector-Scalar Shift Right Logical. Logically shifts elements of vs2 "
                    "right by shift amount in scalar rs1 and writes results to vd."};
    arr[VSRL_VI] = {"VSRL.VI",
                    "Vector-Immediate Shift Right Logical. Logically shifts elements of vs2 "
                    "right by uimm shift amount and writes results to vd."};
    arr[VSRA_VV] = {"VSRA.VV",
                    "Vector-Vector Shift Right Arithmetic. Arithmetically shifts elements of "
                    "vs2 right by shift amounts in vs1 and writes results to vd."};
    arr[VSRA_VX] = {"VSRA.VX",
                    "Vector-Scalar Shift Right Arithmetic. Arithmetically shifts elements of "
                    "vs2 right by shift amount in scalar rs1 and writes results to vd."};
    arr[VSRA_VI] = {"VSRA.VI",
                    "Vector-Immediate Shift Right Arithmetic. Arithmetically shifts elements "
                    "of vs2 right by uimm shift amount and writes results to vd."};
    arr[VMV_V_V] = {"VMV.V.V",
                    "Vector Register Copy. Copies active elements of vector register group "
                    "vs1 into vd."};
    arr[VMV_V_X] = {"VMV.V.X",
                    "Vector-Scalar Move/Splat. Splats the SEW-bit value from integer register "
                    "rs1 into all active elements of vd."};
    arr[VMV_V_I] = {"VMV.V.I",
                    "Vector-Immediate Move/Splat. Splats sign-extended 5-bit simm into all "
                    "active elements of vd."};
    arr[VMV_X_S] = {"VMV.X.S",
                    "Vector Extract Element 0 to Scalar. Sign-extends element 0 of vector "
                    "register vs2 (width SEW) into integer register rd."};
    arr[VMV_S_X] = {"VMV.S.X",
                    "Vector Move Scalar to Element 0. Copies the lower SEW bits of integer "
                    "register rs1 to element 0 of vector register vd (if vl > 0)."};
    arr[VMSEQ_VV] = {"VMSEQ.VV",
                     "Vector Mask Set if Equal (Vector-Vector). Writes 1 to mask bit vd[i] if "
                     "vs2[i] == vs1[i], else 0."};
    arr[VMSEQ_VX] = {"VMSEQ.VX",
                     "Vector Mask Set if Equal (Vector-Scalar). Writes 1 to mask bit vd[i] if "
                     "vs2[i] == x[rs1], else 0."};
    arr[VMSEQ_VI] = {"VMSEQ.VI",
                     "Vector Mask Set if Equal (Vector-Immediate). Writes 1 to mask bit vd[i] "
                     "if vs2[i] == sext(simm), else 0."};
    arr[VMSNE_VV] = {"VMSNE.VV",
                     "Vector Mask Set if Not Equal (Vector-Vector). Writes 1 to mask bit "
                     "vd[i] if vs2[i] != vs1[i], else 0."};
    arr[VMSNE_VX] = {"VMSNE.VX",
                     "Vector Mask Set if Not Equal (Vector-Scalar). Writes 1 to mask bit "
                     "vd[i] if vs2[i] != x[rs1], else 0."};
    arr[VMSNE_VI] = {"VMSNE.VI",
                     "Vector Mask Set if Not Equal (Vector-Immediate). Writes 1 to mask bit "
                     "vd[i] if vs2[i] != sext(simm), else 0."};
    arr[VMSLT_VV] = {"VMSLT.VV",
                     "Vector Mask Set if Less Than Signed (Vector-Vector). Writes 1 to mask "
                     "bit vd[i] if signed vs2[i] < signed vs1[i], else 0."};
    arr[VMSLT_VX] = {"VMSLT.VX",
                     "Vector Mask Set if Less Than Signed (Vector-Scalar). Writes 1 to mask "
                     "bit vd[i] if signed vs2[i] < signed x[rs1], else 0."};
    arr[VMSLTU_VV] = {"VMSLTU.VV",
                      "Vector Mask Set if Less Than Unsigned (Vector-Vector). Writes 1 to "
                      "mask bit vd[i] if unsigned vs2[i] < unsigned vs1[i], else 0."};
    arr[VMSLTU_VX] = {"VMSLTU.VX",
                      "Vector Mask Set if Less Than Unsigned (Vector-Scalar). Writes 1 to "
                      "mask bit vd[i] if unsigned vs2[i] < unsigned x[rs1], else 0."};
    arr[VMSLE_VV] = {"VMSLE.VV",
                     "Vector Mask Set if Less Than or Equal Signed (Vector-Vector). Writes 1 "
                     "to mask bit vd[i] if signed vs2[i] <= signed vs1[i], else 0."};
    arr[VMSLE_VX] = {"VMSLE.VX",
                     "Vector Mask Set if Less Than or Equal Signed (Vector-Scalar). Writes 1 "
                     "to mask bit vd[i] if signed vs2[i] <= signed x[rs1], else 0."};
    arr[VMSLE_VI] = {"VMSLE.VI",
                     "Vector Mask Set if Less Than or Equal Signed (Vector-Immediate). Writes "
                     "1 to mask bit vd[i] if signed vs2[i] <= sext(simm), else 0."};
    arr[VMSLEU_VV] = {"VMSLEU.VV",
                      "Vector Mask Set if Less Than or Equal Unsigned (Vector-Vector). Writes "
                      "1 to mask bit vd[i] if unsigned vs2[i] <= unsigned vs1[i], else 0."};
    arr[VMSLEU_VX] = {"VMSLEU.VX",
                      "Vector Mask Set if Less Than or Equal Unsigned (Vector-Scalar). Writes "
                      "1 to mask bit vd[i] if unsigned vs2[i] <= unsigned x[rs1], else 0."};
    arr[VMSLEU_VI] = {"VMSLEU.VI",
                      "Vector Mask Set if Less Than or Equal Unsigned (Vector-Immediate). "
                      "Writes 1 to mask bit vd[i] if unsigned vs2[i] <= sext(simm), else 0."};
    arr[VMSGT_VX] = {"VMSGT.VX",
                     "Vector Mask Set if Greater Than Signed (Vector-Scalar). Writes 1 to "
                     "mask bit vd[i] if signed vs2[i] > signed x[rs1], else 0."};
    arr[VMSGT_VI] = {"VMSGT.VI",
                     "Vector Mask Set if Greater Than Signed (Vector-Immediate). Writes 1 to "
                     "mask bit vd[i] if signed vs2[i] > sext(simm), else 0."};
    arr[VMSGTU_VX] = {"VMSGTU.VX",
                      "Vector Mask Set if Greater Than Unsigned (Vector-Scalar). Writes 1 to "
                      "mask bit vd[i] if unsigned vs2[i] > unsigned x[rs1], else 0."};
    arr[VMSGTU_VI] = {"VMSGTU.VI",
                      "Vector Mask Set if Greater Than Unsigned (Vector-Immediate). Writes 1 "
                      "to mask bit vd[i] if unsigned vs2[i] > sext(simm), else 0."};
    arr[VMERGE_VVM] = {"VMERGE.VVM",
                       "Vector Merge (Vector-Vector-Mask). Selects vs1[i] where v0.mask[i] is "
                       "1 and vs2[i] where v0.mask[i] is 0, writing to vd[i]."};
    arr[VMERGE_VXM] = {"VMERGE.VXM",
                       "Vector Merge (Vector-Scalar-Mask). Selects x[rs1] where v0.mask[i] is "
                       "1 and vs2[i] where v0.mask[i] is 0, writing to vd[i]."};
    arr[VMERGE_VIM] = {"VMERGE.VIM",
                       "Vector Merge (Vector-Immediate-Mask). Selects sext(simm) where "
                       "v0.mask[i] is 1 and vs2[i] where v0.mask[i] is 0, writing to vd[i]."};
    arr[VMACC_VV] = {"VMACC.VV",
                     "Vector Integer Multiply-Accumulate. Computes vd[i] = +(vs1[i] * vs2[i]) "
                     "+ vd[i]."};
    arr[VMACC_VX] = {"VMACC.VX",
                     "Vector-Scalar Integer Multiply-Accumulate. Computes vd[i] = +(x[rs1] * "
                     "vs2[i]) + vd[i]."};
    arr[VMADD_VV] = {"VMADD.VV",
                     "Vector Integer Multiply-Add. Computes vd[i] = +(vs1[i] * vd[i]) + "
                     "vs2[i]."};
    arr[VMADD_VX] = {"VMADD.VX",
                     "Vector-Scalar Integer Multiply-Add. Computes vd[i] = +(x[rs1] * vd[i]) "
                     "+ vs2[i]."};
    arr[VNMSAC_VV] = {"VNMSAC.VV",
                      "Vector Integer Negated Multiply-Accumulate. Computes vd[i] = -(vs1[i] "
                      "* vs2[i]) + vd[i]."};
    arr[VNMSAC_VX] = {"VNMSAC.VX",
                      "Vector-Scalar Integer Negated Multiply-Accumulate. Computes vd[i] = "
                      "-(x[rs1] * vs2[i]) + vd[i]."};
    arr[VNMSUB_VV] = {"VNMSUB.VV",
                      "Vector Integer Negated Multiply-Add. Computes vd[i] = -(vs1[i] * "
                      "vd[i]) + vs2[i]."};
    arr[VNMSUB_VX] = {"VNMSUB.VX",
                      "Vector-Scalar Integer Negated Multiply-Add. Computes vd[i] = -(x[rs1] "
                      "* vd[i]) + vs2[i]."};
    arr[VWMACCU_VV] = {"VWMACCU.VV",
                       "Vector Widening Unsigned Multiply-Accumulate. Multiplies unsigned "
                       "vs1 and vs2 and adds the 2*SEW product to 2*SEW vd."};
    arr[VWMACCU_VX] = {"VWMACCU.VX",
                       "Vector-Scalar Widening Unsigned Multiply-Accumulate. Multiplies "
                       "unsigned scalar rs1 and vs2 and adds the 2*SEW product to 2*SEW vd."};
    arr[VWMACC_VV] = {"VWMACC.VV",
                      "Vector Widening Signed Multiply-Accumulate. Multiplies signed vs1 and "
                      "vs2 and adds the 2*SEW product to 2*SEW vd."};
    arr[VWMACC_VX] = {"VWMACC.VX",
                      "Vector-Scalar Widening Signed Multiply-Accumulate. Multiplies signed "
                      "scalar rs1 and vs2 and adds the 2*SEW product to 2*SEW vd."};
    arr[VWMACCUS_VX] = {"VWMACCUS.VX",
                        "Vector-Scalar Widening Unsigned-Signed Multiply-Accumulate. "
                        "Multiplies unsigned scalar rs1 and signed vs2 and adds to 2*SEW vd."};
    arr[VWMACCSU_VV] = {"VWMACCSU.VV",
                        "Vector Widening Signed-Unsigned Multiply-Accumulate. Multiplies "
                        "signed vs1 and unsigned vs2 and adds to 2*SEW vd."};
    arr[VWMACCSU_VX] = {"VWMACCSU.VX",
                        "Vector-Scalar Widening Signed-Unsigned Multiply-Accumulate. "
                        "Multiplies signed scalar rs1 and unsigned vs2 and adds to 2*SEW vd."};
    arr[VMIN_VV] = {"VMIN.VV",
                    "Vector-Vector Signed Minimum. Writes the element-wise signed minimum of "
                    "vs2 and vs1 to vd."};
    arr[VMIN_VX] = {"VMIN.VX",
                    "Vector-Scalar Signed Minimum. Writes the element-wise signed minimum of "
                    "vs2 and scalar rs1 to vd."};
    arr[VMINU_VV] = {"VMINU.VV",
                     "Vector-Vector Unsigned Minimum. Writes the element-wise unsigned "
                     "minimum of vs2 and vs1 to vd."};
    arr[VMINU_VX] = {"VMINU.VX",
                     "Vector-Scalar Unsigned Minimum. Writes the element-wise unsigned "
                     "minimum of vs2 and scalar rs1 to vd."};
    arr[VMAX_VV] = {"VMAX.VV",
                    "Vector-Vector Signed Maximum. Writes the element-wise signed maximum of "
                    "vs2 and vs1 to vd."};
    arr[VMAX_VX] = {"VMAX.VX",
                    "Vector-Scalar Signed Maximum. Writes the element-wise signed maximum of "
                    "vs2 and scalar rs1 to vd."};
    arr[VMAXU_VV] = {"VMAXU.VV",
                     "Vector-Vector Unsigned Maximum. Writes the element-wise unsigned "
                     "maximum of vs2 and vs1 to vd."};
    arr[VMAXU_VX] = {"VMAXU.VX",
                     "Vector-Scalar Unsigned Maximum. Writes the element-wise unsigned "
                     "maximum of vs2 and scalar rs1 to vd."};
    arr[VLSE8_V] = {"VLSE8.V",
                    "Vector Strided Load (8-bit elements). Loads vl 8-bit elements from "
                    "addresses rs1 + i * rs2 into vd."};
    arr[VLSE16_V] = {"VLSE16.V",
                     "Vector Strided Load (16-bit elements). Loads vl 16-bit elements from "
                     "addresses rs1 + i * rs2 into vd."};
    arr[VLSE32_V] = {"VLSE32.V",
                     "Vector Strided Load (32-bit elements). Loads vl 32-bit elements from "
                     "addresses rs1 + i * rs2 into vd."};
    arr[VLSE64_V] = {"VLSE64.V",
                     "Vector Strided Load (64-bit elements). Loads vl 64-bit elements from "
                     "addresses rs1 + i * rs2 into vd."};
    arr[VSSE8_V] = {"VSSE8.V",
                    "Vector Strided Store (8-bit elements). Stores vl 8-bit elements from "
                    "vs3 to addresses rs1 + i * rs2."};
    arr[VSSE16_V] = {"VSSE16.V",
                     "Vector Strided Store (16-bit elements). Stores vl 16-bit elements from "
                     "vs3 to addresses rs1 + i * rs2."};
    arr[VSSE32_V] = {"VSSE32.V",
                     "Vector Strided Store (32-bit elements). Stores vl 32-bit elements from "
                     "vs3 to addresses rs1 + i * rs2."};
    arr[VSSE64_V] = {"VSSE64.V",
                     "Vector Strided Store (64-bit elements). Stores vl 64-bit elements from "
                     "vs3 to addresses rs1 + i * rs2."};
    arr[VLUXEI8_V] = {"VLUXEI8.V",
                      "Vector Unordered-Indexed Load (8-bit indices). Loads SEW-bit elements "
                      "from addresses rs1 + vs2[i] into vd."};
    arr[VLUXEI16_V] = {"VLUXEI16.V",
                       "Vector Unordered-Indexed Load (16-bit indices). Loads SEW-bit "
                       "elements from addresses rs1 + vs2[i] into vd."};
    arr[VLUXEI32_V] = {"VLUXEI32.V",
                       "Vector Unordered-Indexed Load (32-bit indices). Loads SEW-bit "
                       "elements from addresses rs1 + vs2[i] into vd."};
    arr[VLUXEI64_V] = {"VLUXEI64.V",
                       "Vector Unordered-Indexed Load (64-bit indices). Loads SEW-bit "
                       "elements from addresses rs1 + vs2[i] into vd."};
    arr[VLOXEI8_V] = {"VLOXEI8.V",
                      "Vector Ordered-Indexed Load (8-bit indices). Loads SEW-bit elements "
                      "from addresses rs1 + vs2[i] in element order into vd."};
    arr[VLOXEI16_V] = {"VLOXEI16.V",
                       "Vector Ordered-Indexed Load (16-bit indices). Loads SEW-bit elements "
                       "from addresses rs1 + vs2[i] in element order into vd."};
    arr[VLOXEI32_V] = {"VLOXEI32.V",
                       "Vector Ordered-Indexed Load (32-bit indices). Loads SEW-bit elements "
                       "from addresses rs1 + vs2[i] in element order into vd."};
    arr[VLOXEI64_V] = {"VLOXEI64.V",
                       "Vector Ordered-Indexed Load (64-bit indices). Loads SEW-bit elements "
                       "from addresses rs1 + vs2[i] in element order into vd."};
    arr[VSUXEI8_V] = {"VSUXEI8.V",
                      "Vector Unordered-Indexed Store (8-bit indices). Stores SEW-bit "
                      "elements from vs3 to addresses rs1 + vs2[i]."};
    arr[VSUXEI16_V] = {"VSUXEI16.V",
                       "Vector Unordered-Indexed Store (16-bit indices). Stores SEW-bit "
                       "elements from vs3 to addresses rs1 + vs2[i]."};
    arr[VSUXEI32_V] = {"VSUXEI32.V",
                       "Vector Unordered-Indexed Store (32-bit indices). Stores SEW-bit "
                       "elements from vs3 to addresses rs1 + vs2[i]."};
    arr[VSUXEI64_V] = {"VSUXEI64.V",
                       "Vector Unordered-Indexed Store (64-bit indices). Stores SEW-bit "
                       "elements from vs3 to addresses rs1 + vs2[i]."};
    arr[VSOXEI8_V] = {"VSOXEI8.V",
                      "Vector Ordered-Indexed Store (8-bit indices). Stores SEW-bit elements "
                      "from vs3 to addresses rs1 + vs2[i] in element order."};
    arr[VSOXEI16_V] = {"VSOXEI16.V",
                       "Vector Ordered-Indexed Store (16-bit indices). Stores SEW-bit "
                       "elements from vs3 to addresses rs1 + vs2[i] in element order."};
    arr[VSOXEI32_V] = {"VSOXEI32.V",
                       "Vector Ordered-Indexed Store (32-bit indices). Stores SEW-bit "
                       "elements from vs3 to addresses rs1 + vs2[i] in element order."};
    arr[VSOXEI64_V] = {"VSOXEI64.V",
                       "Vector Ordered-Indexed Store (64-bit elements). Stores SEW-bit "
                       "elements from vs3 to addresses rs1 + vs2[i] in element order."};
    arr[VID_V] = {"VID.V",
                  "Vector Element Index. Writes each element's zero-based index (0, 1, ..., "
                  "vl-1) into destination vector register group vd."};
    arr[VFMACC_VV] = {"VFMACC.VV",
                      "Vector-Vector Floating-Point Fused Multiply-Accumulate. Computes "
                      "vd[i] = +(vs1[i] * vs2[i]) + vd[i]."};
    arr[VFMACC_VF] = {"VFMACC.VF",
                      "Vector-Scalar Floating-Point Fused Multiply-Accumulate. Computes "
                      "vd[i] = +(f[rs1] * vs2[i]) + vd[i]."};
    arr[VREDSUM_VS] = {"VREDSUM.VS",
                       "Vector Single-Width Integer Sum Reduction. Sums active elements of "
                       "vs2 with scalar element vs1[0] and writes the sum to vd[0]."};
    arr[VWMUL_VV] = {"VWMUL.VV",
                     "Vector Widening Signed Integer Multiply (Vector-Vector). Multiplies "
                     "signed SEW-bit elements of vs2 and vs1 into 2*SEW-bit elements in vd."};
    arr[VWMUL_VX] = {"VWMUL.VX",
                     "Vector Widening Signed Integer Multiply (Vector-Scalar). Multiplies "
                     "signed SEW-bit elements of vs2 by scalar rs1 into 2*SEW-bit elements in "
                     "vd."};
    arr[VNCLIP_WV] = {"VNCLIP.WV",
                      "Vector Narrowing Fixed-Point Clip Signed (Vector-Vector). Rounds, "
                      "shifts right, and saturates 2*SEW elements in vs2 to SEW bits in vd."};
    arr[VNCLIP_WX] = {"VNCLIP.WX",
                      "Vector Narrowing Fixed-Point Clip Signed (Vector-Scalar). Rounds, "
                      "shifts right by rs1, and saturates 2*SEW elements in vs2 to SEW bits "
                      "in vd."};
    arr[VNCLIP_WI] = {"VNCLIP.WI",
                      "Vector Narrowing Fixed-Point Clip Signed (Vector-Immediate). Rounds, "
                      "shifts right by uimm, and saturates 2*SEW elements in vs2 to SEW bits "
                      "in vd."};
    arr[VNCLIPU_WV] = {"VNCLIPU.WV",
                       "Vector Narrowing Fixed-Point Clip Unsigned (Vector-Vector). Rounds, "
                       "shifts right, and saturates unsigned 2*SEW elements in vs2 to SEW "
                       "bits in vd."};
    arr[VNCLIPU_WX] = {"VNCLIPU.WX",
                       "Vector Narrowing Fixed-Point Clip Unsigned (Vector-Scalar). Rounds, "
                       "shifts right by rs1, and saturates unsigned 2*SEW elements in vs2 to "
                       "SEW bits in vd."};
    arr[VNCLIPU_WI] = {"VNCLIPU.WI",
                       "Vector Narrowing Fixed-Point Clip Unsigned (Vector-Immediate). "
                       "Rounds, shifts right by uimm, and saturates unsigned 2*SEW elements "
                       "in vs2 to SEW bits in vd."};
    arr[VNSRL_WV] = {"VNSRL.WV",
                     "Vector Narrowing Logical Shift Right (Vector-Vector). Shifts 2*SEW "
                     "elements in vs2 right logically by vs1 and extracts low SEW bits to vd."};
    arr[VNSRL_WX] = {"VNSRL.WX",
                     "Vector Narrowing Logical Shift Right (Vector-Scalar). Shifts 2*SEW "
                     "elements in vs2 right logically by rs1 and extracts low SEW bits to vd."};
    arr[VNSRL_WI] = {"VNSRL.WI",
                     "Vector Narrowing Logical Shift Right (Vector-Immediate). Shifts 2*SEW "
                     "elements in vs2 right logically by uimm and extracts low SEW bits to "
                     "vd."};
    arr[VNSRA_WV] = {"VNSRA.WV",
                     "Vector Narrowing Arithmetic Shift Right (Vector-Vector). Shifts 2*SEW "
                     "elements in vs2 right arithmetically by vs1 and extracts low SEW bits "
                     "to vd."};
    arr[VNSRA_WX] = {"VNSRA.WX",
                     "Vector Narrowing Arithmetic Shift Right (Vector-Scalar). Shifts 2*SEW "
                     "elements in vs2 right arithmetically by rs1 and extracts low SEW bits "
                     "to vd."};
    arr[VNSRA_WI] = {"VNSRA.WI",
                     "Vector Narrowing Arithmetic Shift Right (Vector-Immediate). Shifts "
                     "2*SEW elements in vs2 right arithmetically by uimm and extracts low SEW "
                     "bits to vd."};
    arr[VSLIDE1UP_VX] = {"VSLIDE1UP.VX",
                         "Vector Slide 1 Up. Shifts active elements of vs2 up by 1 index and "
                         "places scalar x[rs1] at element 0 of vd."};
    arr[VSLIDE1DOWN_VX] = {"VSLIDE1DOWN.VX",
                           "Vector Slide 1 Down. Shifts active elements of vs2 down by 1 "
                           "index and places scalar x[rs1] at element vl-1 of vd."};
    arr[VFADD_VV] = {"VFADD.VV",
                     "Vector-Vector Floating-Point Add. Adds floating-point elements of vs2 "
                     "and vs1 and writes to vd."};
    arr[VFADD_VF] = {"VFADD.VF",
                     "Vector-Scalar Floating-Point Add. Adds floating-point scalar f[rs1] to "
                     "elements of vs2 and writes to vd."};
    arr[VFIRST_M] = {"VFIRST.M",
                     "Vector Find First Set Mask Bit. Finds the lowest active element index "
                     "in mask register vs2 whose bit is 1 and writes that index to GPR rd "
                     "(or -1 if none)."};
    arr[VCPOP_M] = {"VCPOP.M",
                    "Vector Count Population in Mask. Counts the number of active mask bits "
                    "set to 1 in vs2 and writes the count to GPR rd."};
    arr[VMAND_MM] = {"VMAND.MM",
                     "Vector Mask Logical AND. Computes vd.mask[i] = vs2.mask[i] & "
                     "vs1.mask[i]."};
    arr[VMNAND_MM] = {"VMNAND.MM",
                      "Vector Mask Logical NAND. Computes vd.mask[i] = ~(vs2.mask[i] & "
                      "vs1.mask[i])."};
    arr[VMANDN_MM] = {"VMANDN.MM",
                      "Vector Mask Logical AND-NOT. Computes vd.mask[i] = vs2.mask[i] & "
                      "~vs1.mask[i]."};
    arr[VMXOR_MM] = {"VMXOR.MM",
                     "Vector Mask Logical XOR. Computes vd.mask[i] = vs2.mask[i] ^ "
                     "vs1.mask[i]."};
    arr[VMOR_MM] = {"VMOR.MM",
                    "Vector Mask Logical OR. Computes vd.mask[i] = vs2.mask[i] | "
                    "vs1.mask[i]."};
    arr[VMNOR_MM] = {"VMNOR.MM",
                     "Vector Mask Logical NOR. Computes vd.mask[i] = ~(vs2.mask[i] | "
                     "vs1.mask[i])."};
    arr[VMORN_MM] = {"VMORN.MM",
                     "Vector Mask Logical OR-NOT. Computes vd.mask[i] = vs2.mask[i] | "
                     "~vs1.mask[i]."};
    arr[VMXNOR_MM] = {"VMXNOR.MM",
                      "Vector Mask Logical XNOR. Computes vd.mask[i] = ~(vs2.mask[i] ^ "
                      "vs1.mask[i])."};
    arr[VFMV_F_S] = {"VFMV.F.S",
                     "Vector Floating-Point Move Element 0 to Scalar. Copies element 0 of "
                     "vector register vs2 to floating-point register rd."};
    arr[VFMV_S_F] = {"VFMV.S.F",
                     "Vector Floating-Point Move Scalar to Element 0. Copies floating-point "
                     "register rs1 to element 0 of vector register vd."};
    arr[VFMERGE_VFM] = {"VFMERGE.VFM",
                        "Vector Floating-Point Merge. Selects f[rs1] where v0.mask[i] is 1 "
                        "and vs2[i] where v0.mask[i] is 0, writing to vd[i]."};
    arr[VCHECK] = {"VCHECK",
                   "Vector Check Assertion. Validates vector registers or states for "
                   "debugging/test harness purposes."};

    // Whole register moves
    arr[VMV1R_V] = {"VMV1R.V",
                    "Vector Copy 1 Whole Register. Copies the entire contents of 1 vector "
                    "register vs2 to vd regardless of vtype and vl."};
    arr[VMV2R_V] = {"VMV2R.V",
                    "Vector Copy 2 Whole Registers. Copies the entire contents of 2 aligned "
                    "vector registers starting at vs2 to vd."};
    arr[VMV4R_V] = {"VMV4R.V",
                    "Vector Copy 4 Whole Registers. Copies the entire contents of 4 aligned "
                    "vector registers starting at vs2 to vd."};
    arr[VMV8R_V] = {"VMV8R.V",
                    "Vector Copy 8 Whole Registers. Copies the entire contents of 8 aligned "
                    "vector registers starting at vs2 to vd."};

    // Whole register loads
    arr[VL1RE8_V] = {"VL1RE8.V",
                     "Vector Load 1 Whole Register (EEW=8). Loads 1 vector register of bytes "
                     "from memory address rs1 into vd."};
    arr[VL1RE16_V] = {"VL1RE16.V",
                      "Vector Load 1 Whole Register (EEW=16). Loads 1 vector register of "
                      "16-bit elements from memory address rs1 into vd."};
    arr[VL1RE32_V] = {"VL1RE32.V",
                      "Vector Load 1 Whole Register (EEW=32). Loads 1 vector register of "
                      "32-bit elements from memory address rs1 into vd."};
    arr[VL1RE64_V] = {"VL1RE64.V",
                      "Vector Load 1 Whole Register (EEW=64). Loads 1 vector register of "
                      "64-bit elements from memory address rs1 into vd."};
    arr[VL2RE8_V] = {"VL2RE8.V",
                     "Vector Load 2 Whole Registers (EEW=8). Loads 2 vector registers from "
                     "memory address rs1 into vd."};
    arr[VL2RE16_V] = {"VL2RE16.V",
                      "Vector Load 2 Whole Registers (EEW=16). Loads 2 vector registers from "
                      "memory address rs1 into vd."};
    arr[VL2RE32_V] = {"VL2RE32.V",
                      "Vector Load 2 Whole Registers (EEW=32). Loads 2 vector registers from "
                      "memory address rs1 into vd."};
    arr[VL2RE64_V] = {"VL2RE64.V",
                      "Vector Load 2 Whole Registers (EEW=64). Loads 2 vector registers from "
                      "memory address rs1 into vd."};
    arr[VL4RE8_V] = {"VL4RE8.V",
                     "Vector Load 4 Whole Registers (EEW=8). Loads 4 vector registers from "
                     "memory address rs1 into vd."};
    arr[VL4RE16_V] = {"VL4RE16.V",
                      "Vector Load 4 Whole Registers (EEW=16). Loads 4 vector registers from "
                      "memory address rs1 into vd."};
    arr[VL4RE32_V] = {"VL4RE32.V",
                      "Vector Load 4 Whole Registers (EEW=32). Loads 4 vector registers from "
                      "memory address rs1 into vd."};
    arr[VL4RE64_V] = {"VL4RE64.V",
                      "Vector Load 4 Whole Registers (EEW=64). Loads 4 vector registers from "
                      "memory address rs1 into vd."};
    arr[VL8RE8_V] = {"VL8RE8.V",
                     "Vector Load 8 Whole Registers (EEW=8). Loads 8 vector registers from "
                     "memory address rs1 into vd."};
    arr[VL8RE16_V] = {"VL8RE16.V",
                      "Vector Load 8 Whole Registers (EEW=16). Loads 8 vector registers from "
                      "memory address rs1 into vd."};
    arr[VL8RE32_V] = {"VL8RE32.V",
                      "Vector Load 8 Whole Registers (EEW=32). Loads 8 vector registers from "
                      "memory address rs1 into vd."};
    arr[VL8RE64_V] = {"VL8RE64.V",
                      "Vector Load 8 Whole Registers (EEW=64). Loads 8 vector registers from "
                      "memory address rs1 into vd."};

    // Whole register stores
    arr[VS1R_V] = {"VS1R.V",
                   "Vector Store 1 Whole Register. Stores 1 full vector register vs3 to "
                   "memory address rs1."};
    arr[VS2R_V] = {"VS2R.V",
                   "Vector Store 2 Whole Registers. Stores 2 full vector registers starting "
                   "at vs3 to memory address rs1."};
    arr[VS4R_V] = {"VS4R.V",
                   "Vector Store 4 Whole Registers. Stores 4 full vector registers starting "
                   "at vs3 to memory address rs1."};
    arr[VS8R_V] = {"VS8R.V",
                   "Vector Store 8 Whole Registers. Stores 8 full vector registers starting "
                   "at vs3 to memory address rs1."};

    // Add with carry / subtract with borrow
    arr[VADC_VVM] = {"VADC.VVM",
                     "Vector Add with Carry (Vector-Vector-Mask). Computes vd[i] = vs2[i] + "
                     "vs1[i] + v0.mask[i]."};
    arr[VADC_VXM] = {"VADC.VXM",
                     "Vector Add with Carry (Vector-Scalar-Mask). Computes vd[i] = vs2[i] + "
                     "x[rs1] + v0.mask[i]."};
    arr[VADC_VIM] = {"VADC.VIM",
                     "Vector Add with Carry (Vector-Immediate-Mask). Computes vd[i] = vs2[i] "
                     "+ sext(simm) + v0.mask[i]."};
    arr[VMADC_VV] = {"VMADC.VV",
                     "Vector Mask Carry-Out from Add (Vector-Vector). Writes carry-out of "
                     "vs2[i] + vs1[i] to mask bit vd[i]."};
    arr[VMADC_VX] = {"VMADC.VX",
                     "Vector Mask Carry-Out from Add (Vector-Scalar). Writes carry-out of "
                     "vs2[i] + x[rs1] to mask bit vd[i]."};
    arr[VMADC_VI] = {"VMADC.VI",
                     "Vector Mask Carry-Out from Add (Vector-Immediate). Writes carry-out of "
                     "vs2[i] + sext(simm) to mask bit vd[i]."};
    arr[VMADC_VVM] = {"VMADC.VVM",
                      "Vector Mask Carry-Out from Add with Carry (Vector-Vector). Writes "
                      "carry-out of vs2[i] + vs1[i] + v0.mask[i] to mask bit vd[i]."};
    arr[VMADC_VXM] = {"VMADC.VXM",
                      "Vector Mask Carry-Out from Add with Carry (Vector-Scalar). Writes "
                      "carry-out of vs2[i] + x[rs1] + v0.mask[i] to mask bit vd[i]."};
    arr[VMADC_VIM] = {"VMADC.VIM",
                      "Vector Mask Carry-Out from Add with Carry (Vector-Immediate). Writes "
                      "carry-out of vs2[i] + sext(simm) + v0.mask[i] to mask bit vd[i]."};
    arr[VMSBC_VV] = {"VMSBC.VV",
                     "Vector Mask Borrow-Out from Subtract (Vector-Vector). Writes borrow-out "
                     "of vs2[i] - vs1[i] to mask bit vd[i]."};
    arr[VMSBC_VX] = {"VMSBC.VX",
                     "Vector Mask Borrow-Out from Subtract (Vector-Scalar). Writes borrow-out "
                     "of vs2[i] - x[rs1] to mask bit vd[i]."};
    arr[VMSBC_VVM] = {"VMSBC.VVM",
                      "Vector Mask Borrow-Out from Subtract with Borrow (Vector-Vector). "
                      "Writes borrow-out of vs2[i] - vs1[i] - v0.mask[i] to mask bit vd[i]."};
    arr[VMSBC_VXM] = {"VMSBC.VXM",
                      "Vector Mask Borrow-Out from Subtract with Borrow (Vector-Scalar). "
                      "Writes borrow-out of vs2[i] - x[rs1] - v0.mask[i] to mask bit vd[i]."};

    // Mask instructions
    arr[VMSBF_M] = {"VMSBF.M",
                    "Vector Mask Set-Before-First. Sets mask bits in vd to 1 for all active "
                    "elements prior to the first active 1 bit in vs2."};
    arr[VMSIF_M] = {"VMSIF.M",
                    "Vector Mask Set-Including-First. Sets mask bits in vd to 1 for all "
                    "active elements up to and including the first active 1 bit in vs2."};
    arr[VMSOF_M] = {"VMSOF.M",
                    "Vector Mask Set-Only-First. Sets only the mask bit in vd corresponding "
                    "to the first active 1 bit in vs2."};
    arr[VIOTA_M] = {"VIOTA.M",
                    "Vector Iota. Writes the parallel prefix sum of preceding active 1 bits "
                    "in mask register vs2 to each element of vd."};
    arr[VCOMPRESS_VM] = {"VCOMPRESS.VM",
                         "Vector Compress. Packs elements of vs2 for which mask bit vs1[i] is "
                         "1 into contiguous elements at the start of vd."};

    // Vector bitmanip Zvbb/Zvbc
    arr[VANDN_VV] = {"VANDN.VV",
                     "Vector Bitwise AND-NOT (Vector-Vector). Computes vd[i] = vs2[i] & "
                     "~vs1[i]."};
    arr[VANDN_VX] = {"VANDN.VX",
                     "Vector Bitwise AND-NOT (Vector-Scalar). Computes vd[i] = vs2[i] & "
                     "~x[rs1]."};
    arr[VROL_VV] = {"VROL.VV",
                    "Vector Rotate Left (Vector-Vector). Rotates each element vs2[i] left by "
                    "vs1[i] bits."};
    arr[VROL_VX] = {"VROL.VX",
                    "Vector Rotate Left (Vector-Scalar). Rotates each element vs2[i] left by "
                    "x[rs1] bits."};
    arr[VROR_VV] = {"VROR.VV",
                    "Vector Rotate Right (Vector-Vector). Rotates each element vs2[i] right "
                    "by vs1[i] bits."};
    arr[VROR_VX] = {"VROR.VX",
                    "Vector Rotate Right (Vector-Scalar). Rotates each element vs2[i] right "
                    "by x[rs1] bits."};
    arr[VROR_VI] = {"VROR.VI",
                    "Vector Rotate Right (Vector-Immediate). Rotates each element vs2[i] "
                    "right by uimm bits."};
    arr[VCLZ_V] = {"VCLZ.V",
                   "Vector Count Leading Zeros. Counts leading zero bits in each SEW-bit "
                   "element of vs2."};
    arr[VCTZ_V] = {"VCTZ.V",
                   "Vector Count Trailing Zeros. Counts trailing zero bits in each SEW-bit "
                   "element of vs2."};
    arr[VCPOP_V] = {"VCPOP.V",
                    "Vector Count Population. Counts set bits in each SEW-bit element of vs2."};
    arr[VBREV_V] = {"VBREV.V",
                    "Vector Reverse Bits in Elements. Reverses the bit order of each SEW-bit "
                    "element in vs2."};
    arr[VBREV8_V] = {"VBREV8.V",
                     "Vector Reverse Bits in Bytes. Reverses the bit order within each byte "
                     "of vs2."};
    arr[VREV8_V] = {"VREV8.V",
                    "Vector Reverse Bytes. Reverses the byte order of each SEW-bit element "
                    "in vs2."};
    arr[VCLMUL_VV] = {"VCLMUL.VV",
                      "Vector Carry-less Multiply Low (Vector-Vector). Computes the low SEW "
                      "bits of the carry-less product of vs2[i] and vs1[i]."};
    arr[VCLMUL_VX] = {"VCLMUL.VX",
                      "Vector Carry-less Multiply Low (Vector-Scalar). Computes the low SEW "
                      "bits of the carry-less product of vs2[i] and x[rs1]."};
    arr[VCLMULH_VV] = {"VCLMULH.VV",
                       "Vector Carry-less Multiply High (Vector-Vector). Computes the high "
                       "SEW bits of the carry-less product of vs2[i] and vs1[i]."};
    arr[VCLMULH_VX] = {"VCLMULH.VX",
                       "Vector Carry-less Multiply High (Vector-Scalar). Computes the high "
                       "SEW bits of the carry-less product of vs2[i] and x[rs1]."};

    // Widening shifts & float moves
    arr[VFMV_V_F] = {"VFMV.V.F",
                     "Vector Floating-Point Move Scalar to Vector. Splats floating-point "
                     "scalar f[rs1] into all active elements of vd."};
    arr[VWSLL_VV] = {"VWSLL.VV",
                     "Vector Widening Shift Left Logical (Vector-Vector). Zero-extends "
                     "SEW-bit elements in vs2 to 2*SEW and shifts left by vs1[i]."};
    arr[VWSLL_VX] = {"VWSLL.VX",
                     "Vector Widening Shift Left Logical (Vector-Scalar). Zero-extends "
                     "SEW-bit elements in vs2 to 2*SEW and shifts left by x[rs1]."};
    arr[VWSLL_VI] = {"VWSLL.VI",
                     "Vector Widening Shift Left Logical (Vector-Immediate). Zero-extends "
                     "SEW-bit elements in vs2 to 2*SEW and shifts left by uimm."};
}

}  // namespace simrv::explain::detail
