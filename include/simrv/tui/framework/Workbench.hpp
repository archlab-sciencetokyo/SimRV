/**
 * @file Workbench.hpp
 * @brief Formalized container and focus model for multi-column workbench slots.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace simrv::tui::framework {

template <typename SlotData, size_t MaxSlots = 4>
class Workbench {
   public:
    static constexpr size_t kMaxSlots = MaxSlots;

    Workbench() { slots_.emplace_back(); }
    explicit Workbench(std::vector<SlotData> initial_slots) { set_slots(std::move(initial_slots)); }

    [[nodiscard]] auto size() const noexcept -> size_t { return slots_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return slots_.empty(); }
    [[nodiscard]] constexpr auto capacity() const noexcept -> size_t { return kMaxSlots; }
    [[nodiscard]] auto can_add() const noexcept -> bool { return slots_.size() < kMaxSlots; }
    [[nodiscard]] auto can_close() const noexcept -> bool { return slots_.size() > 1; }

    [[nodiscard]] auto focused_index() const noexcept -> size_t { return focused_index_; }
    [[nodiscard]] auto focused_index_ref() noexcept -> size_t& { return focused_index_; }

    void focus(size_t index) noexcept {
        if (slots_.empty()) {
            focused_index_ = 0;
            return;
        }
        focused_index_ = std::min(index, slots_.size() - 1);
    }

    void focus_next() noexcept {
        if (slots_.empty()) return;
        focused_index_ = (focused_index_ + 1) % slots_.size();
    }

    void focus_prev() noexcept {
        if (slots_.empty()) return;
        focused_index_ = (focused_index_ + slots_.size() - 1) % slots_.size();
    }

    [[nodiscard]] auto focused_slot() -> SlotData& {
        clamp_focus();
        return slots_[focused_index_];
    }
    [[nodiscard]] auto focused_slot() const -> const SlotData& {
        const size_t idx = std::min(focused_index_, slots_.empty() ? 0 : slots_.size() - 1);
        return slots_[idx];
    }

    auto add(SlotData slot) -> bool {
        if (!can_add()) return false;
        slots_.push_back(std::move(slot));
        focused_index_ = slots_.size() - 1;
        return true;
    }

    auto insert(size_t index, SlotData slot) -> bool {
        if (!can_add()) return false;
        const size_t pos = std::min(index, slots_.size());
        slots_.insert(slots_.begin() + static_cast<std::ptrdiff_t>(pos), std::move(slot));
        focused_index_ = pos;
        return true;
    }

    void clear() noexcept {
        slots_.clear();
        focused_index_ = 0;
    }

    auto close(size_t index) -> bool {
        if (!can_close() || index >= slots_.size()) return false;
        slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(index));
        if (focused_index_ >= slots_.size()) {
            focused_index_ = slots_.size() - 1;
        } else if (focused_index_ > index) {
            --focused_index_;
        }
        return true;
    }

    auto swap(size_t a, size_t b, bool follow_focus = false) -> bool {
        if (a >= slots_.size() || b >= slots_.size() || a == b) return false;
        std::swap(slots_[a], slots_[b]);
        if (follow_focus) {
            if (focused_index_ == a) {
                focused_index_ = b;
            } else if (focused_index_ == b) {
                focused_index_ = a;
            }
        }
        return true;
    }

    auto move_left(size_t index) -> bool {
        if (index == 0 || index >= slots_.size()) return false;
        return swap(index, index - 1, /*follow_focus=*/true);
    }

    auto move_right(size_t index) -> bool {
        if (index + 1 >= slots_.size()) return false;
        return swap(index, index + 1, /*follow_focus=*/true);
    }

    void set_slots(std::vector<SlotData> new_slots) {
        if (new_slots.empty()) {
            slots_.clear();
            slots_.emplace_back();
        } else {
            if (new_slots.size() > kMaxSlots) {
                new_slots.resize(kMaxSlots);
            }
            slots_ = std::move(new_slots);
        }
        clamp_focus();
    }

    [[nodiscard]] auto operator[](size_t i) -> SlotData& { return slots_[i]; }
    [[nodiscard]] auto operator[](size_t i) const -> const SlotData& { return slots_[i]; }
    [[nodiscard]] auto at(size_t i) -> SlotData& { return slots_.at(i); }
    [[nodiscard]] auto at(size_t i) const -> const SlotData& { return slots_.at(i); }

    [[nodiscard]] auto slots() -> std::vector<SlotData>& { return slots_; }
    [[nodiscard]] auto slots() const -> const std::vector<SlotData>& { return slots_; }

    auto begin() noexcept { return slots_.begin(); }
    auto end() noexcept { return slots_.end(); }
    auto begin() const noexcept { return slots_.begin(); }
    auto end() const noexcept { return slots_.end(); }
    auto cbegin() const noexcept { return slots_.cbegin(); }
    auto cend() const noexcept { return slots_.cend(); }

   private:
    void clamp_focus() noexcept {
        if (slots_.empty()) {
            focused_index_ = 0;
        } else if (focused_index_ >= slots_.size()) {
            focused_index_ = slots_.size() - 1;
        }
    }

    std::vector<SlotData> slots_;
    size_t focused_index_ = 0;
};

}  // namespace simrv::tui::framework
