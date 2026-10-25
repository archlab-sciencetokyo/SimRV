/**
 * @file PointerRouter.hpp
 * @brief Pure hierarchical, z-ordered pointer hit-testing and event routing.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

enum class PointerButton : uint8_t {
    Left = 0,
    Middle = 1,
    Right = 2,
    Release = 3,
    WheelUp = 64,
    WheelDown = 65,
    Other = 255,
};

enum class PointerEventKind : uint8_t {
    Click,
    Wheel,
    Drag,
    Move,
};

struct PointerEvent {
    ScreenCoord pos{};
    PointerButton button = PointerButton::Left;
    PointerEventKind kind = PointerEventKind::Click;
    int modifiers = 0;
};

using PointerHandler = std::function<bool(const PointerEvent& event)>;

struct HitTarget {
    ScreenRect bounds{};
    int z_order = 0;
    std::string tag{};
    PointerHandler handler{};
};

class PointerRouter {
   public:
    void clear() noexcept { targets_.clear(); }

    void add_target(ScreenRect bounds, int z_order, std::string tag, PointerHandler handler) {
        targets_.push_back({bounds, z_order, std::move(tag), std::move(handler)});
    }

    [[nodiscard]] auto target_count() const noexcept -> size_t { return targets_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return targets_.empty(); }

    [[nodiscard]] auto dispatch(const PointerEvent& event) const -> bool {
        std::vector<const HitTarget*> matches;
        for (const auto& target : targets_) {
            if (target.bounds.contains(event.pos)) {
                matches.push_back(&target);
            }
        }
        std::stable_sort(matches.begin(), matches.end(),
                         [](const auto* a, const auto* b) { return a->z_order > b->z_order; });

        for (const auto* target : matches) {
            if (target->handler && target->handler(event)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto top_target_at(ScreenCoord pt) const -> const HitTarget* {
        const HitTarget* best = nullptr;
        for (const auto& target : targets_) {
            if (target.bounds.contains(pt)) {
                if (!best || target.z_order > best->z_order) {
                    best = &target;
                }
            }
        }
        return best;
    }

   private:
    std::vector<HitTarget> targets_;
};

}  // namespace simrv::tui::framework
