/**
 * @file MissionTools.cpp
 * @brief Classroom mission validation, preview, and scaffolding utilities.
 */
#include "simrv/util/MissionTools.hpp"

#include <format>
#include <fstream>
#include <iostream>

#include "simrv/tui/TuiMission.hpp"

namespace simrv::util {

auto check_mission_file(const std::filesystem::path& path, bool preview) -> bool {
    if (!std::filesystem::is_regular_file(path)) {
        std::cerr << std::format("Error: Mission file does not exist: {}\n", path.string());
        return false;
    }

    tui::MissionProgress mission;
    if (!mission.load(path.string())) {
        std::cerr << std::format("Error loading mission '{}': {}\n", path.string(),
                                 mission.detail());
        return false;
    }

    if (preview) {
        std::cout << std::format("Mission '{}' (Program: '{}')\n", mission.lesson_id(),
                                 mission.expected_program());
        std::cout << std::format("  Description: {}\n", mission.description());
        std::cout << std::format("  Goal:        {}\n", mission.goal());
        std::cout << std::format("  Default Speed: {} Hz\n", mission.speed_hz());
        std::cout << std::format("  Steps:       {}\n", mission.step_count());

        for (size_t i = 0; i < mission.step_count(); ++i) {
            const auto guidance = mission.step_guidance(i);
            if (guidance) {
                const auto speed = mission.step_speed(i);
                const auto speed_text =
                    (speed == 0) ? "maximum speed (0 Hz)" : std::format("{} Hz", speed);
                const auto min_width = (guidance->panels.size() > 1) ? 80 : 40;
                std::cout << std::format("  {}. {} [{}] — {}\n", i + 1, guidance->title,
                                         mission.step_id(i), speed_text);
                std::cout << std::format("     Panels: {} (minimum width {} columns)\n",
                                         guidance->panels.size(), min_width);
                std::cout << std::format("     Learn:  {}\n", guidance->learn);
                std::cout << std::format("     Do:     {}\n", guidance->prompt);
                if (!guidance->hint.empty()) {
                    std::cout << std::format("     Hint:   {}\n", guidance->hint);
                }
                std::cout << std::format("     Why:    {}\n", guidance->why);
            }
        }

        const auto checkpoints = mission.checkpoints_progress();
        if (!checkpoints.empty()) {
            std::cout << std::format("  Checkpoints ({}):\n", checkpoints.size());
            for (const auto& cp : checkpoints) {
                std::cout << std::format("    - checkpoint {} (after step {}): {}\n", cp.symbol,
                                         cp.after_step, cp.message);
                std::cout << std::format("      Reached message: {}\n", cp.message);
            }
        }
    }

    return true;
}

auto create_mission_scaffold(const std::filesystem::path& path, std::string_view lesson_id,
                             std::string_view program) -> bool {
    std::ofstream out(path);
    if (!out) {
        std::cerr << std::format("Error: Cannot create mission file: {}\n", path.string());
        return false;
    }

    out << std::format(R"([mission]
version = "1"
id = "{}"
program = "{}"
description = "Interactive classroom guidance mission."
goal = "Observe instruction execution and architectural state transitions."
speed_hz = "30"

[step]
id = "observe"
title = "Observe Instruction Flow"
prompt = "Single step or run to observe register updates."
hint = "Look at the destination register in the register panel."
why = "Understanding state changes is fundamental to computer architecture."
view = "explain"
panels = "registers,trace"
speed_hz = "30"

[checkpoint]
label = "main"
after_step = 1
message = "Reached program main entry point."
)",
                       lesson_id, program);

    std::cout << std::format("Created classroom mission scaffold: {}\n", path.string());
    return true;
}

}  // namespace simrv::util
