#pragma once

#include <string>
#include <string_view>

namespace simrv::util {

/** Generate a shell completion script for the simulator runtime CLI. */
auto generate_shell_completion(std::string_view shell) -> std::string;

}  // namespace simrv::util
