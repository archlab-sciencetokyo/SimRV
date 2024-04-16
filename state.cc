/******************************************************************************************/
/**** SimCore/RISC-V since 2018-07-05                             ArchLab. TokyoTech   ****/
/******************************************************************************************/
#include "state.h"

/***** invalidate (flush) all entries of all TLBs                                     *****/
/******************************************************************************************/
void CPU::TLB_flush (){
    for (int i = 0; i < TLB_SIZE; i++) {
        TLB_inst_r[i].v_addr = TLB_inst_r[i].p_addr = -1;
        TLB_data_r[i].v_addr = TLB_data_r[i].p_addr = -1;
        TLB_data_w[i].v_addr = TLB_data_w[i].p_addr = -1;
    }
}

/******************************************************************************************/
uint32_t CPU::get_mstatus(uint32_t mask){
    uint32_t val = (mstatus | 0x6000) & mask;
    return ((val >> 13) == 3) | ((val >> 15) == 3) ? val | 0x80000000 : val;
}

/******************************************************************************************/
void CPU::set_mstatus(uint32_t wdata){
    /* flush the TLBs if change of MMU config */
    uint32_t mod = mstatus ^ wdata;
    if ((mod & (MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_MXR)) != 0 ||
        ((mstatus & MSTATUS_MPRV) && (mod & MSTATUS_MPP) != 0)) {
        //printf("%8ld:TLB FLHSH in set_mstatus\n", mtime);
        TLB_flush();
    }
    uint32_t mask = MSTATUS_MASK & ~MSTATUS_FS;
    mstatus = (mstatus & ~mask) | (wdata & mask);
}

/******************************************************************************************/
uint32_t CPU::read_csr (uint32_t addr){
    uint32_t rcsr = 0;
    switch (addr) {
        case 0x3A0          : rcsr = 0; break;
        case 0x3B0          : rcsr = 0; break;
        case CSR_FFLAGS     : rcsr = 0; break; // 0x001
        case CSR_FRM        : rcsr = 0; break; // 0x002
        case CSR_FCSR       : rcsr = 0; break; // fflags | (frm << 5) // 0x003
        case CSR_SIE        : rcsr = mie & mideleg; break; // 0x104
        case CSR_STVEC      : rcsr = stvec;         break;  // 0x105
        case CSR_SCOUNTEREN : rcsr = scounteren;    break;  // 0x106
        case CSR_SSCRATCH   : rcsr = sscratch;      break;  // 0x140
        case CSR_SEPC       : rcsr = sepc;          break;  // 0x141
        case CSR_SCAUSE     : rcsr = scause;        break;  // 0x142
        case CSR_STVAL      : rcsr = stval;         break;  // 0x143
        case CSR_SIP        : rcsr = mip & mideleg; break; // 0x144
        case CSR_SATP       : rcsr = satp;          break;  // 0x180
        
        case CSR_MEDELEG    : rcsr = medeleg;       break;  // 0x302
        case CSR_MIDELEG    : rcsr = mideleg;       break;  // 0x303
        case CSR_MIE        : rcsr = mie;           break;  // 0x304
        case CSR_MTVEC      : rcsr = mtvec;         break;  // 0x305
        case CSR_MCOUNTEREN : rcsr = mcounteren;    break;  // 0x306
        case CSR_MSCRATCH   : rcsr = mscratch;      break;  // 0x340
        case CSR_MEPC       : rcsr = mepc;          break;  // 0x341
        case CSR_MCAUSE     : rcsr = mcause;        break;  // 0x342
        case CSR_MTVAL      : rcsr = mtval;         break;  // 0x343
        case CSR_MIP        : rcsr = mip;           break;  // 0x344
        case CSR_MISA       : rcsr = misa | 0x40000000;    break; // 0x301
        
        case CSR_MCYCLE     : // 0xB00
        case CSR_MINSTRET   : // 0xB02
        case CSR_CYCLE      : // 0xC00
        case CSR_INSTRET    : // 0xC02
        case CSR_TIME       : rcsr = (uint32_t) mtime;       break;  // 0xC01
        
        case CSR_MCYCLEH    : // 0xB80
        case CSR_MINSTRETH  : // 0xB82
        case CSR_CYCLEH     : // 0xC80
        case CSR_INSTRETH   : // 0xC82
        case CSR_TIMEH      : rcsr = (uint32_t)(mtime >> 32); break; // 0xC81
            
        case CSR_SSTATUS    : rcsr = get_mstatus(0x000de133); break; // 0x100
        case CSR_MSTATUS    : rcsr = get_mstatus(0xffffffff); break; // 0x300
            
        //case CSR_MVENDORID  : rcsr = mvendorid;  break;  // 0xF11
        //case CSR_MARCHID    : rcsr = marchid;    break;  // 0xF12
        //case CSR_MIMPID     : rcsr = mimpid;     break;  // 0xF13
        case CSR_MHARTID    : rcsr = mhartid;    break;  // 0xF14
            
        default: break;
    }
    return rcsr;
}

/******************************************************************************************/
void CPU::write_csr (uint32_t addr, uint32_t wdata){
    uint32_t mask1 = (1 << (CAUSE_STORE_PAGE_FAULT + 1)) - 1;
    uint32_t mask2 = MIP_SSIP | MIP_STIP | MIP_SEIP;
    uint32_t mask3 = MIP_MSIP | MIP_MTIP | MIP_SSIP | MIP_STIP | MIP_SEIP;
    uint32_t mask4 = MIP_SSIP | MIP_STIP;
    
    switch (addr) {
        case CSR_FFLAGS : break; // 0x001
        case CSR_FRM    : break; // 0x002
        case CSR_FCSR   : break; // 0x003
        case CSR_MHARTID: break; // 0xF14
        case 0x3A0: break;
        case 0x3B0: break;
        case CSR_TIME : break; // 0xC01
        case CSR_TIMEH: break; // 0xC81
        case CSR_MISA : break; // 0x301
            
        case CSR_STVEC      : stvec       = wdata & ~3; break; // 0x105
        case CSR_SCOUNTEREN : scounteren  = wdata & 5;  break; // 0x106
        case CSR_SSCRATCH   : sscratch    = wdata;      break; // 0x140
        case CSR_SEPC       : sepc        = wdata & ~1; break; // 0x141
        case CSR_SCAUSE     : scause      = wdata;      break; // 0x142
        case CSR_STVAL      : stval       = wdata;      break; // 0x143
            
        case CSR_MTVEC      : mtvec       = wdata & ~3; break; // 0x305
        case CSR_MCOUNTEREN : mcounteren  = wdata &  5; break; // 0x306
        case CSR_MSCRATCH   : mscratch    = wdata;      break; // 0x340
        case CSR_MEPC       : mepc        = wdata & ~1; break; // 0x341
        case CSR_MCAUSE     : mcause      = wdata;      break; // 0x342
        case CSR_MTVAL      : mtval       = wdata;      break; // 0x343
        
        case CSR_SIE        : mie = (mie & ~mideleg) | (wdata & mideleg); break;
        case CSR_SIP        : mip = (mip & ~mideleg) | (wdata & mideleg); break;
        case CSR_MEDELEG    : medeleg = (medeleg & ~mask1) | (wdata & mask1); break;
        case CSR_MIDELEG    : mideleg = (mideleg & ~mask2) | (wdata & mask2); break;
        case CSR_MIE        : mie = (mie & ~mask3) | (wdata & mask3); break;
        case CSR_MIP        : mip = (mip & ~mask4) | (wdata & mask4); break;
        
        case CSR_SATP       : satp = wdata; break; // 0x180
        
        case CSR_MSTATUS    : set_mstatus(wdata); break; // 0x300
        case CSR_SSTATUS    : set_mstatus((mstatus & ~SSTATUS_MASK) 
                                          | (wdata &  SSTATUS_MASK)); break; // 0x100
    default: break;
    }
}

/******************************************************************************************/
void CPU::mret (){
    uint32_t mpp  = (mstatus >> MSTATUS_MPP_SHIFT) & 0x3;
    uint32_t mpie = (mstatus >> MSTATUS_MPIE_SHIFT) & 0x1;
    uint32_t nxt_mstatus = (mstatus & ~(1 << mpp)) | (mpie << mpp);
    nxt_mstatus |= MSTATUS_MPIE;
    nxt_mstatus &= ~MSTATUS_MPP;
    mstatus = nxt_mstatus;
    priv = mpp;
    pc = mepc;
    //printf("%8ld:TLB FLHSH in mret\n", mtime);
    TLB_flush();
}

/******************************************************************************************/
void CPU::sret (){
    uint32_t spp  = (mstatus >> 8) & 1;
    uint32_t spie = (mstatus >> 5) & 1;
    uint32_t nxt_mstatus = (((mstatus & ~(1<<spp)) | (spie << spp)) | 0x20) & ~0x100;
    mstatus = nxt_mstatus;
    priv = spp;
    pc = sepc;
    //printf("%8ld:TLB FLHSH in sret\n", mtime);
    TLB_flush();
}

/******************************************************************************************/
void CPU::plic_update_mip (){
    uint32_t mask;
    mask = plic_pending_irq & ~plic_served_irq;
    if (mask) mip |=  (MIP_MEIP | MIP_SEIP);
    else      mip &= ~(MIP_MEIP | MIP_SEIP);
}

/******************************************************************************************/
void CPU::plic_set_irq (int irq_num, int state){
    uint32_t mask = 1 << (irq_num - 1);
    if (state) plic_pending_irq |= mask;
    else       plic_pending_irq &= ~mask;
    plic_update_mip();
}

/******************************************************************************************/
void CPU::raise_exception (uint32_t cause, uint32_t tval){
    uint32_t deleg;
    if (priv <= PRIV_S) {
        if (cause & CAUSE_INTERRUPT) { deleg = (mideleg >> (cause & 0x1F)) & 1;  } 
        else                         { deleg = (medeleg >> (cause & 0x1F)) & 1;  }
    } else                           { deleg = 0; }

    if (deleg) {
        scause  = cause;
        sepc    = pc;
        stval   = tval;
        mstatus = (mstatus & ~MSTATUS_SPIE) | (((mstatus>>priv)&1) << MSTATUS_SPIE_SHIFT);
        mstatus = (mstatus & ~MSTATUS_SPP) | (priv << MSTATUS_SPP_SHIFT);
        mstatus &= ~MSTATUS_SIE;
        priv    = PRIV_S;
        pc      = stvec;
    } else {
        mcause  = cause;
        mepc    = pc;
        mtval   = tval;
        mstatus = (mstatus & ~MSTATUS_MPIE) | (((mstatus>>priv)&1) << MSTATUS_MPIE_SHIFT);
        mstatus = (mstatus & ~MSTATUS_MPP) | (priv << MSTATUS_MPP_SHIFT);
        mstatus &= ~MSTATUS_MIE;
        priv    = PRIV_M;
        pc      = mtvec;
    }
    //printf("%8ld:TLB FLHSH in raise_ecxeption\n", mtime);
    TLB_flush();
}
/******************************************************************************************/
