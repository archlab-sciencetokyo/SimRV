/**
 * @file MissionTools.hpp
 * @brief Classroom mission validation, preview, and scaffolding utilities.
 */
#pragma once

#include <filesystem>
#include <string_view>

namespace simrv::util {

/// Validate and preview a classroom .mission file. Returns true if valid.
[[nodiscard]] auto check_mission_file(const std::filesystem::path& path, bool preview = true)
    -> bool;

/// Scaffold a new classroom mission file. Returns true if successfully written.
[[nodiscard]] auto create_mission_scaffold(const std::filesystem::path& path,
                                           std::string_view lesson_id = "lesson",
                                           std::string_view program = "demo.elf") -> bool;

}  // namespace simrv::util
