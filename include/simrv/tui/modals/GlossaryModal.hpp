/**
 * @file GlossaryModal.hpp
 * @brief Modal dialog handler for RISC-V Architecture Glossary and Concept Explorer.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace simrv::tui::modals {

enum class GlossaryTopic : uint8_t {
    RegistersAbi,
    PipelineHazards,
    MemoryCaches,
    VirtualMemoryTlb,
    BranchPrediction,
    PrivilegeTraps
};

class GlossaryModal {
   public:
    static void open(int& topic_idx, int& scroll_offset);
    static void move_topic(int& topic_idx, int& scroll_offset, int delta);
    static void scroll_content(int& scroll_offset, int delta, int max_lines);
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb, int topic_idx,
                       int scroll_offset, int term_height, int box_w);
};

}  // namespace simrv::tui::modals
