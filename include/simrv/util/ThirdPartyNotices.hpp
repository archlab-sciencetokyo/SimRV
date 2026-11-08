/**
 * @file ThirdPartyNotices.hpp
 * @brief Embedded third-party notices, license text, and specification attributions.
 */
#pragma once

#include <string_view>

namespace simrv::util {

/// Retrieve the formatted third-party notices and license attributions.
[[nodiscard]] auto get_third_party_notices() noexcept -> std::string_view;

/// Print third-party notices to standard output.
void print_third_party_notices();

}  // namespace simrv::util
