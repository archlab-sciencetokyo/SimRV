/******************************************************************************************/
/**** SimCore/RISC-V since 2018-07-05                            ArchLab. TokyoTech   ****/
/******************************************************************************************/
#ifndef __console_hpp__
#define __console_hpp__
#include "define.h"
#include "state.h"
/******************************************************************************************/
class Console {
public:
    Console ();
    uint32_t console_read (uint32_t offset);
    void console_write (CPU*, uint32_t, uint32_t);
    int  recieve_input ();
    int  MC_recieve_input ();

    uint8_t *mmem;   // main memory

    QueueState *Queue; /* Queue of Console */
    
    uint32_t DeviceFeaturesSel;
    uint32_t DriverFeatures;
    uint32_t DriverFeaturesSel;
    uint32_t InterruptStatus;
    uint32_t Status;
    uint32_t QueueSel;
    uint32_t QueueNum;
    
    uint8_t  cons_fifo;
    uint8_t  fifo_en;
    
    //struct QueueState Queue[CONSOLE_MAX_QUEUE_NUM];
private:
};
#endif /* console_hpp */
/******************************************************************************************/
