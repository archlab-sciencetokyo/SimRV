/******************************************************************************************/
/**** SimCore/RISC-V since 2018-07-05                             ArchLab. TokyoTech   ****/
/******************************************************************************************/
#include "disk.h"
#include "machine.h"
/******************************************************************************************/
extern Machine mm;  /* class machine                     */
extern Microcn cc;  /* I/O controller (micro-controller) */

/******************************************************************************************/
#define DISK_MAGIC_VALUE       0x74726976
#define DISK_VERSION           2
#define DISK_DEVIDE_ID         2
#define DISK_VENDOR_ID         0xffff
#define DISK_DEVICE_FEATURES   1
#define DISK_CONFIG_GENERATION 0
#define DISK_QUEUE_NUM_MAX     4

/***** mc_code: main memory load                                                      *****/
/******************************************************************************************/
uint32_t ram_ld(uint32_t addr, int n, uint8_t *ram){
    if(n!=1 && n!=2 && n!=4){printf("__ Error: ram_r() not supported n=%d\n", n); exit(0);}
    uint32_t data = 0;
    for (int i=0; i<n; i++) { data |= ((uint32_t)ram[(addr + i) & DRAM_MASK]) << (8*i); }

    return data;
}
/***** mc_code: main memory store                                                     *****/
/******************************************************************************************/
void ram_st(uint32_t addr, uint32_t data, int n, uint8_t *ram){
    if(n!=1 && n!=2 && n!=4){printf("__ Error: dsk_w() not supported n=%d\n", n); exit(0);}
    if(n==1){
        ram[addr & DRAM_MASK] = data & 0xff;
    }
    else if (n==2){
        ram[ addr    & DRAM_MASK] =  data       & 0xff;
        ram[(addr+1) & DRAM_MASK] = (data >> 8) & 0xff;
    }
    else if (n==4){
        ram[ addr    & DRAM_MASK] =  data       & 0xff;
        ram[(addr+1) & DRAM_MASK] = (data >> 8) & 0xff;
        ram[(addr+2) & DRAM_MASK] = (data >>16) & 0xff;
        ram[(addr+3) & DRAM_MASK] = (data >>24) & 0xff;
    }
}

/***** mc_code: disk load                                                             *****/
/******************************************************************************************/
uint32_t dsk_ld(uint32_t addr, int n, uint8_t *dsk){
    if(n!=4) { printf("__ Error: dsk_ld()\n"); exit(0); }
    uint32_t* dsk_tmp = (uint32_t*)dsk;
    return dsk_tmp[addr/4];
}

/***** mc_code: disk store                                                            *****/
/******************************************************************************************/
void dsk_st(uint32_t addr, uint8_t data, int n, uint8_t *dsk){
    if(n!=4) { printf("__ Error: dsk_st()\n"); exit(0); }
    uint32_t* dsk_tmp = (uint32_t*)dsk;
    dsk_tmp[addr/4] = data;
}

/*** mc_code: update the used ring                                                    *****/
/******************************************************************************************/
void update_descriptor(uint32_t desc_idx, uint32_t desc_len, int q_num, 
                       QueueState *qs, uint8_t *mmem){
    uint32_t addr_used_idx = qs->UsedLow + 2;
    uint32_t index = (uint16_t)ram_ld(addr_used_idx, 2, mmem);

    ram_st(addr_used_idx, index+1, 2, mmem);

    uint32_t addr_used_entry = qs->UsedLow + 4 + (index & (q_num - 1)) * 8;
    ram_st(addr_used_entry,   desc_idx, 4, mmem);
    ram_st(addr_used_entry+4, desc_len, 4, mmem);
}

#define DESC_SIZE 16 /* descriptor size 16 byte */
/******************************************************************************************/
/*** mc_code: disc sector read & write                                                 ****/
/******************************************************************************************/
void disk_request(uint8_t *mmem, uint8_t *mdsk, int q_num, QueueState *qs){
    Descriptor desc;
    BlockRequestHeader header;
    uint8_t *p;

    uint16_t avail_idx = (uint16_t)ram_ld(qs->AvailLow+2, 2, mmem);
    while (qs->last_avail_idx != avail_idx) { /***** header -> sector -> footer *****/

        // (1) header
        uint32_t adr = qs->AvailLow + 4 + (qs->last_avail_idx & (q_num - 1)) * 2;
        uint16_t desc_idx_header = ram_ld(adr, 2, mmem);
        uint32_t desc_adr_header = desc_idx_header * DESC_SIZE + qs->DescLow;

        p = (uint8_t*)&desc;
        for(int i=0; i<DESC_SIZE; i++){ *p = ram_ld(desc_adr_header+i, 1, mmem); p++; }

        p = (uint8_t*)&header;
        for(int i=0; i<(int)desc.len; i++){ *p = ram_ld(desc.adr+i, 1, mmem); p++; }
        if (desc.len!=16) { printf("__ ERROR: disk_request() desc.len!=16\n"); exit(0); }
        
        // (2) sector
        uint16_t desc_idx_sector = desc.next;
        uint32_t desc_adr_sector = desc_idx_sector * DESC_SIZE + qs->DescLow;
        p = (uint8_t*)&desc;
        for(int i=0; i<DESC_SIZE; i++){ *p = ram_ld(desc_adr_sector+i, 1, mmem); p++; }

        uint32_t sector_len = desc.len;
        uint32_t sector_adr = (uint32_t)desc.adr;
        
        // (3) footer
        uint16_t desc_idx_footer = desc.next;
        uint32_t desc_adr_footer = desc_idx_footer * DESC_SIZE + qs->DescLow;
        p = (uint8_t*)&desc;
        for(int i=0; i<DESC_SIZE; i++){ *p = ram_ld(desc_adr_footer+i, 1, mmem); p++; }

        uint32_t footer_adr = (uint32_t)desc.adr;
 
        uint32_t request_size = 0;
        switch (header.type) {
        case VIRTIO_BLK_T_IN: { /////  disk -> dram
            request_size = sector_len + 1;
            for(int i=0; i<(int)sector_len; i=i+4){ //++){
                uint32_t d = dsk_ld(header.sector_num * SECTOR_SIZE + i, 4, mdsk);
                ram_st(sector_adr+i, d, 4, mmem);
            }
            ram_st(footer_adr, 0, 1, mmem); //  VIRTIO_BLK_S_OK
            break; }
        case VIRTIO_BLK_T_OUT: { ///// dram -> disk
            request_size = 1;
            for(int i=0; i<(int)sector_len; i=i+4){
                uint32_t d = ram_ld(sector_adr, 4, mmem);
                dsk_st(header.sector_num * SECTOR_SIZE + i, d, 4, mdsk);
            }
            ram_st(sector_adr+sector_len-1, 0, 1, mmem); //  VIRTIO_BLK_S_OK
            break; }
        default: { printf("__ ERROR: disk unknown header %x\n", header.type); exit(0); }
        }

        update_descriptor(desc_idx_header, request_size, q_num, qs, mmem);
        qs->last_avail_idx++;
    }
}

/******************************************************************************************/
Disk::Disk(){
    memset(this, 0, sizeof(Disk));
    sector = new uint8_t[DISK_SIZE]; // sector is nog good name, rename this!
}

/******************************************************************************************/
uint32_t Disk::disk_read(uint32_t offset){
    uint32_t rdata = 0;
    switch (offset) {
    case 0x000: rdata = DISK_MAGIC_VALUE;       break;
    case 0x004: rdata = DISK_VERSION;           break;
    case 0x008: rdata = DISK_DEVIDE_ID;         break;
    case 0x00c: rdata = DISK_VENDOR_ID;         break;
    case 0x010: rdata = DISK_DEVICE_FEATURES;   break;
    case 0x034: rdata = DISK_QUEUE_NUM_MAX;     break;
    case 0x0fc: rdata = DISK_CONFIG_GENERATION; break;
    case 0x044: rdata = Queue[QueueSel].Ready;  break;
    case 0x060: rdata = InterruptStatus;        break;
    case 0x070: rdata = Status;                 break;
    case 0x100: rdata = 0;                      break;
    case 0x104: rdata = 0;                      break;
    default:  break;//{ printf("__ Error: disk_read() default %x.\n", offset); exit(0); }
    }
    //printf("%8ld:CALL Disk READ mem[%x]->%x\n", cpu->mtime, offset, rdata);
    return rdata;
}

/******************************************************************************************/
void Disk::disk_write(CPU *cpu, uint32_t offset, uint32_t wdata){
    switch(offset) {
    case 0x038: QueueNum                 = wdata; break;
    case 0x044: Queue[QueueSel].Ready    = wdata; break;
    case 0x070: Status                   = wdata; break;
    case 0x080: Queue[QueueSel].DescLow  = wdata; break;
    case 0x090: Queue[QueueSel].AvailLow = wdata; break;
    case 0x0a0: Queue[QueueSel].UsedLow  = wdata; break;
    case 0x050: { 
        Queue[QueueSel].Notify=wdata; 
//        printf("__ disk_request %10ld: %d\n", cpu->mtime, wdata);
        if(mm.s_use_uc) {
            cc.pc = 0;
            cc.Qnum = QueueNum;
            cc.Mode = 2;
            cc.Qsel = wdata;
            for(int i=0; i<32; i++) cc.reg[i] = 0;
            cc.reg[11] =  0x8000;
            while(cc.exec()); 
        }
        else{
            disk_request(mmem, sector, QueueNum, &Queue[wdata]); /***** request *****/
        }
        InterruptStatus |= 1;
        cpu->plic_set_irq(VIRTIO_DISK_IRQ, 1);
        break; 
    }
    case 0x064: {
        InterruptStatus &= ~wdata;
        if (InterruptStatus == 0) { cpu->plic_set_irq(VIRTIO_DISK_IRQ, 0); }
        break; }
    }
}
/******************************************************************************************/
