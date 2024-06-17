/**
 * @file console.h
 * @brief This file contains the declaration of the Console class
 *
 * @note This file is part of the SimCore/RISC-V project developed by
 * ArchLab. TokyoTech.
 * @since 2018-07-05
 *
 */
#ifndef __console_hpp__
#define __console_hpp__

#include "define.h"
#include "state.h"

/**
 * @class Console
 * @brief Represents a console device.
 */
class Console {
 public:
  /**
   * @brief Default constructor for the Console class.
   */
  Console();

  /**
   * @brief Reads data from the console.
   * @param offset The offset to read from.
   * @return The data read from the console.
   */
  uint32_t console_read(uint32_t offset);

  /**
   * @brief Writes data to the console.
   * @param cpu The CPU object.
   * @param offset The offset to write to.
   * @param data The data to write.
   */
  void console_write(CPU *cpu, uint32_t offset, uint32_t data);

  /**
   * @brief Receives input from the console.
   * @return The received input.
   */
  int receive_input();

  /**
   * @brief Receives input from the MC (Management Console).
   * @return The received input.
   */
  int MC_receive_input();

  uint8_t *mmem;  // main memory

  QueueState *Queue; /* Queue of Console */

  uint32_t DeviceFeaturesSel;
  uint32_t DriverFeatures;
  uint32_t DriverFeaturesSel;
  uint32_t InterruptStatus;
  uint32_t Status;
  uint32_t QueueSel;
  uint32_t QueueNum;

  uint8_t cons_fifo;
  uint8_t fifo_en;

  // struct QueueState Queue[CONSOLE_MAX_QUEUE_NUM];
 private:
};

#endif /* console_hpp */
