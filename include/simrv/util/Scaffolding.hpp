/**
 * @file Scaffolding.hpp
 * @brief Utilities for scaffolding starter CPU models and external device plugins.
 */
#pragma once

#include <string_view>

namespace simrv::util {

/**
 * @brief Generate a starter external device plugin project or single file.
 *
 * If output is empty, generates into a directory named `{name}_plugin`.
 * If output is "-", writes the C plugin template to standard output.
 * If output ends in ".c", writes just the C plugin source file.
 * Otherwise, creates the directory and generates {name}_plugin.c, CMakeLists.txt, and README.md.
 *
 * @param name Logical name of the device plugin (e.g. "mydevice").
 * @param output Destination directory, file path, or "-" for stdout.
 * @return True on success, false on error.
 */
auto scaffold_device_plugin(std::string_view name, std::string_view output) -> bool;

/**
 * @brief Generate a starter Custom Function Unit (CFU) plugin project or single file.
 *
 * If output is empty, generates into a directory named `{name}_cfu`.
 * If output is "-", writes the C plugin template to standard output.
 * If output ends in ".c", writes just the C plugin source file.
 * Otherwise, creates the directory and generates {name}_cfu.c, CMakeLists.txt, and README.md.
 *
 * @param name Logical name of the CFU plugin (e.g. "mycfu").
 * @param output Destination directory, file path, or "-" for stdout.
 * @return True on success, false on error.
 */
auto scaffold_cfu_plugin(std::string_view name, std::string_view output) -> bool;

/**
 * @brief Generate a starter CPU model configuration file.
 *
 * If output is empty or "-", writes the configuration to standard output.
 * Otherwise, writes to the designated file path.
 *
 * @param preset_name Base CPU model preset name (e.g. "balanced", "tiny", "performance").
 * @param output Destination file path or "-" for stdout.
 * @return True on success, false on error.
 */
auto scaffold_cpu_model(std::string_view preset_name, std::string_view output) -> bool;

}  // namespace simrv::util
