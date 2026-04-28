/**
 * @file IoController.hpp
 * @brief I/O Controller (microcontroller) declarations for device coordination.
 *
 * The IoController manages auxiliary I/O operations including console and disk
 * virtio queue coordination. It executes a simplified in-order RV32I pipeline
 * for device-specific firmware.
 */
#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "Define.hpp"

class Machine;  // Forward declaration

/**
 * @class IoController
 * @brief Auxiliary I/O controller for device firmware execution.
 *
 * Executes optional I/O controller program using a condensed single-cycle
 * in-order pipeline (Fetch→Decode→Execute→Memory→Writeback→Commit).
 * Supports RV32I base ISA for MMIO operations to console/disk devices.
 */
class IoController {
   public:
    /**
     * Initialize I/O controller state and load firmware image.
     *
     * @param image_path Path to the I/O controller firmware binary
     */
    void init(std::string_view image_path);

    /**
     * Execute one cycle of the I/O controller pipeline.
     *
     * @return true if execution should continue; false if power-off command received
     */
    [[nodiscard]] auto exec() -> bool;

    // Ownership & context linkage
    Machine* owner = nullptr;  ///< Parent machine for statistics tracking

    // Memory hierarchy
    Byte* cmem = nullptr;  ///< Local controller memory (instruction/data)
    Byte* mmem = nullptr;  ///< Main memory reference (for MMIO access)

    // Device queue state
    QueueState* cons_queue = nullptr;  ///< Console virtio queue state
    QueueState* disk_queue = nullptr;  ///< Disk virtio queue state

    // Device ports
    Byte cons_fifo{};      ///< Console FIFO input data
    Byte fifo_en{};        ///< Console FIFO enable flag
    Byte* disk = nullptr;  ///< Disk sector buffer pointer

    // Device control registers
    Word Mode = 0;  ///< Device mode register
    Word Qnum = 0;  ///< Queue number register
    Word Qsel = 0;  ///< Queue select register

    // Architectural state
    Register pc = 0;               ///< Program counter
    std::array<Register, 32> reg;  ///< General-purpose register file

    Counter icnt = 0;  ///< Instruction count (for diagnostics)

   private:
    /**
     * Allocate and manage local controller memory.
     */
    std::vector<Byte> cmem_storage_;

    /**
     * Load firmware image file into controller memory.
     *
     * @param file_path Path to firmware binary
     * @param dest Memory destination buffer
     */
    static void load_image(std::string_view file_path, Byte* dest);
};
