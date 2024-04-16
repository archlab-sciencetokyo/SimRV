/******************************************************************************************/
/**** SimCore/RISC-V since 2018-07-05                             ArchLab. TokyoTech   ****/
/******************************************************************************************/
#ifndef __module_hpp__
#define __module_hpp__
#include "define.h"

uint32_t CB_inst_decomp (uint32_t ir); // Combinatorial Logical Circuit
uint32_t CB_imm_gen (uint32_t ir);     // Combinatorial Logical Circuit
OPERATION_ID decoder (uint32_t ir);    

uint32_t ALU_IM (uint32_t in1, uint32_t in2, uint32_t funct3, uint32_t funct7);
uint32_t ALU_B (uint32_t in1, uint32_t in2, uint32_t funct3);
uint32_t ALU_A (uint32_t in1, uint32_t in2, uint32_t funct5);
uint32_t ALU_C (uint32_t rcsr, uint32_t rrs1, uint32_t imm, uint32_t funct3);

extern char OPERATION_NAME[NUMOFID___][11];

#endif /* module_hpp */
/******************************************************************************************/
