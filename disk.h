
#ifndef __disk_hpp__
#define __disk_hpp__

#include "define.h"
#include "state.h"

/**
 * @class Disk
 * @brief Represents a disk device.
 */
class Disk {
public:
    /**
     * @brief Default constructor for Disk class.
     */
    Disk ();

    /**
     * @brief Reads data from the disk.
     * @param address The address to read from.
     * @return The data read from the disk.
     */
    uint32_t disk_read(uint32_t address);

    /**
     * @brief Writes data to the disk.
     * @param cpu Pointer to the CPU object.
     * @param address The address to write to.
     * @param data The data to write.
     */
    void disk_write(CPU* cpu, uint32_t address, uint32_t data);

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
