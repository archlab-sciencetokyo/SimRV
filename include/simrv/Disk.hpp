/**
 * @file Disk.hpp
 * @brief SimRV declarations.
 */
#pragma once
#include "Define.hpp"
#include "State.hpp"
class Disk {
   public:
    Disk();
    Word disk_read(Address);
    void disk_write(CPU*, Address, Word);

    Byte* mmem;    // main memory
    Byte* sector;  // disk image

    QueueState* Queue; /* Queue of Disk */

    Word DeviceFeaturesSel;
    Word DriverFeatures;
    Word DriverFeaturesSel;
    Word InterruptStatus;
    Word Status;
    Word QueueSel;
    Word QueueNum;
    // struct QueueState Queue[DISK_MAX_QUEUE_NUM];
   private:
};
