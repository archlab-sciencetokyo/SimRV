/**
 * @file GlossaryModal.hpp
 * @brief Modal dialog handler for RISC-V Architecture Glossary and Concept Explorer.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "simrv/tui/framework/Modal.hpp"

namespace simrv::tui::modals {

enum class GlossaryTopic : uint8_t {
    RegistersAbi,
    PipelineHazards,
    MemoryCaches,
    VirtualMemoryTlb,
    BranchPrediction,
    PrivilegeTraps
};

class GlossaryModal : public framework::IModal {
   public:
    explicit GlossaryModal(int& topic_idx, int& scroll_offset)
        : topic_idx_(topic_idx), scroll_offset_(scroll_offset) {}
    GlossaryModal() : topic_idx_(owned_topic_), scroll_offset_(owned_scroll_) {}

    [[nodiscard]] auto title() const -> std::string override {
        return "RISC-V Architecture Glossary & Concept Explorer";
    }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::Glossary; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt, framework::ScreenSize box_size)
        -> framework::ModalAction override;

    void scroll_to_top() override { scroll_offset_ = 0; }
    void scroll_to_bottom() override { scroll_offset_ = 999999; }
    void handle_wheel(int delta) override;
    auto handle_horizontal(int delta) -> bool override {
        move_topic(topic_idx_, scroll_offset_, delta);
        return true;
    }

    static void open(int& topic_idx, int& scroll_offset);
    static void move_topic(int& topic_idx, int& scroll_offset, int delta);
    static void scroll_content(int& scroll_offset, int delta, int max_lines);
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb, int topic_idx,
                       int scroll_offset, int term_height, int box_w);

   private:
    int owned_topic_ = 0;
    int owned_scroll_ = 0;
    int& topic_idx_;
    int& scroll_offset_;
};

}  // namespace simrv::tui::modals
