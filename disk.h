/******************************************************************************************/
/**** SimCore/RISC-V since 2018-07-05                             ArchLab. TokyoTech   ****/
/******************************************************************************************/
#ifndef __disk_hpp__
#define __disk_hpp__
#include "define.h"
#include "state.h"
/******************************************************************************************/
class Disk {
public:
    Disk ();
    uint32_t disk_read(uint32_t);
    void     disk_write(CPU*, uint32_t, uint32_t);
    
    uint8_t *mmem;   // main memory
    uint8_t *sector; // disk image

    QueueState *Queue;  /* Queue of Disk */
    
    uint32_t DeviceFeaturesSel;
    uint32_t DriverFeatures;
    uint32_t DriverFeaturesSel;
    uint32_t InterruptStatus;
    uint32_t Status;
    uint32_t QueueSel;
    uint32_t QueueNum;
    //struct QueueState Queue[DISK_MAX_QUEUE_NUM];
private:
};
#endif
/******************************************************************************************/
