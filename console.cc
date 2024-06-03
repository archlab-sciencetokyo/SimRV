
/**
 * @file console.cc
 * @brief Implementation of the Console class and related functions.
 *
 * This file contains the implementation of the Console class, which handles
 * display output and keyboard input. It also includes functions for handling
 * console requests and receiving input from the keyboard.
 */
#include "console.h"

#include "machine.h"

/******************************************************************************************/
extern Machine mm; /* class machine                     */
extern Microcn cc; /* I/O controller (micro-controller) */

/******************************************************************************************/
#define CONSOLE_MAGIC_VALUE 0x74726976
#define CONSOLE_VERSION 2
#define CONSOLE_DEVIDE_ID 3
#define CONSOLE_VENDOR_ID 0xffff
#define CONSOLE_DEVICE_FEATURES 1
#define CONSOLE_CONFIG_GENERATION 0
#define CONSOLE_QUEUE_NUM_MAX 2
/******************************************************************************************/
extern void update_descriptor(uint32_t, uint32_t, int, QueueState *, uint8_t *);
extern uint32_t ram_ld(uint32_t addr, int n, uint8_t *ram);
extern void ram_st(uint32_t addr, uint32_t data, int n, uint8_t *ram);

#define DESC_SIZE 16 /* descriptor size 16 byte */
/******************************************************************************************/
/*** mc_code: console_request for display output ****/
/******************************************************************************************/
void cons_request(uint8_t *mmem, uint32_t q_num, QueueState *qs) {
  Descriptor desc;
  uint8_t *p;
  uint16_t avail_idx = (uint16_t)ram_ld(qs->AvailLow + 2, 2, mmem);
  while (qs->last_avail_idx != avail_idx) {
    uint32_t adr = qs->AvailLow + 4 + (qs->last_avail_idx & (q_num - 1)) * 2;
    uint16_t desc_idx_header = ram_ld(adr, 2, mmem);
    uint32_t desc_adr_header = desc_idx_header * DESC_SIZE + qs->DescLow;

    p = (uint8_t *)&desc;
    for (int i = 0; i < DESC_SIZE; i++) {
      *p = ram_ld(desc_adr_header + i, 1, mmem);
      p++;
    }

    for (int i = 0; i < (int)desc.len; i++) { /***** write to stdout *****/
      uint8_t d = ram_ld(desc.adr + i, 1, mmem);
      if (write(fileno(stdout), &d, 1) < 0)
        printf("__ ERROR in cons_request!\n");
    }
    fflush(stdout);

    update_descriptor(desc_idx_header, 0, q_num, qs, mmem);
    qs->last_avail_idx++;
  }
}

/******************************************************************************************/
/*** input from keyboard ****/
/******************************************************************************************/
int Console::receive_input() {
  Descriptor desc;
  uint8_t *p;

  QueueState *qs = &Queue[0];
  if (!qs->Ready) return 0;

  uint16_t avail_idx = (uint16_t)ram_ld(qs->AvailLow + 2, 2, mmem);
  if (qs->last_avail_idx == avail_idx) return 0;

  uint32_t adr = qs->AvailLow + 4 + (qs->last_avail_idx & (QueueNum - 1)) * 2;
  uint16_t desc_idx_header = ram_ld(adr, 2, mmem);
  uint32_t desc_adr_header = desc_idx_header * DESC_SIZE + qs->DescLow;

  p = (uint8_t *)&desc;
  for (int i = 0; i < DESC_SIZE; i++) {
    *p = ram_ld(desc_adr_header + i, 1, mmem);
    p++;
  }

  int stdin_fd = 0;
  struct timeval tv;
  fd_set rfds, wfds, efds;
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);
  FD_SET(stdin_fd, &rfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  ssize_t r_len = 0;
  if (select(stdin_fd + 1, &rfds, &wfds, &efds, &tv) > 0 &&
      FD_ISSET(stdin_fd, &rfds)) {
    uint8_t buf;
    r_len = read(fileno(stdin), &buf, 1);
    if (buf == 0x11) {
      printf("\n__ Terminated by Control+'q'\n");
      return -1;
    }
    if (r_len != 1) {
      printf("__ ERROR: in console input\n");
      exit(0);
    }

    ram_st(desc.adr, (uint32_t)buf, 1, mmem);                /*****/
    update_descriptor(desc_idx_header, r_len, 2, qs, mmem);  // 2019-08-30
    qs->last_avail_idx++;
  }
  return r_len;
}

/******************************************************************************************/
Console::Console() { memset(this, 0, sizeof(Console)); }

/******************************************************************************************/
uint32_t Console::console_read(uint32_t offset) {
  uint32_t rdata = 0;
  switch (offset) {
    case 0x000:
      rdata = CONSOLE_MAGIC_VALUE;
      break;
    case 0x004:
      rdata = CONSOLE_VERSION;
      break;
    case 0x008:
      rdata = CONSOLE_DEVIDE_ID;
      break;
    case 0x00c:
      rdata = CONSOLE_VENDOR_ID;
      break;
    case 0x010:
      rdata = CONSOLE_DEVICE_FEATURES;
      break;
    case 0x034:
      rdata = CONSOLE_QUEUE_NUM_MAX;
      break;
    case 0x0fc:
      rdata = CONSOLE_CONFIG_GENERATION;
      break;
    case 0x044:
      rdata = Queue[QueueSel].Ready;
      break;
    case 0x060:
      rdata = InterruptStatus;
      break;
    case 0x070:
      rdata = Status;
      break;
    default:
      break;  //{ printf("__ Error: console_read() default %x.\n", offset);
              // exit(0); }
  }
  // printf("%8ld:CALL Console READ mem[%x]->%x\n", cpu->mtime, offset, rdata);
  return rdata;
}

/******************************************************************************************/
void Console::console_write(CPU *cpu, uint32_t offset, uint32_t wdata) {
  switch (offset) {
    case 0x030:
      QueueSel = wdata;
      break;
    case 0x038:
      QueueNum = wdata;
      break;
    case 0x044:
      Queue[QueueSel].Ready = wdata;
      break;
    case 0x080:
      Queue[QueueSel].DescLow = wdata;
      break;
    case 0x090:
      Queue[QueueSel].AvailLow = wdata;
      break;
    case 0x0a0:
      Queue[QueueSel].UsedLow = wdata;
      break;
    case 0x070:
      Status = wdata;
      break;
    case 0x050: {
      Queue[QueueSel].Notify = wdata;
      if (wdata > 1) {
        printf("__ ERROR: wrong value console_write()\n");
        exit(0);
      }
      if (wdata == 1) {
        if (mm.s_use_uc) {
          cc.pc = 0;
          cc.Qnum = wdata;
          cc.Mode = 1;
          cc.Qsel = wdata;
          for (int i = 0; i < 32; i++) cc.reg[i] = 0;
          cc.reg[11] = 0x8000;
          while (cc.exec());

        } else {
          cons_request(mmem, wdata, &Queue[wdata]);
        }
      }
      break;
    }
    case 0x064: {
      InterruptStatus &= ~wdata;
      if (InterruptStatus == 0) {
        cpu->plic_set_irq(VIRTIO_CONSOLE_IRQ, 0);
      }
      break;
    }
  }
}

#define MICRO_CONT_MODE_KEY 0
/******************************************************************************************/
int Console::MC_receive_input() {
  int ret;
  if (mm.s_use_uc && MICRO_CONT_MODE_KEY) {
    cc.pc = 0;
    cc.Qnum = QueueNum;
    cc.Mode = 3;
    cc.cons_fifo = cons_fifo;
    for (int i = 0; i < 32; i++) cc.reg[i] = 0;
    cc.reg[11] = 0x8000;
    while (cc.exec());
    ret = 1;  // KEY_ON;
    InterruptStatus |= 1;
  } else {
    ret = receive_input();
    InterruptStatus |= 1;
  }
  return ret;
}
/******************************************************************************************/
