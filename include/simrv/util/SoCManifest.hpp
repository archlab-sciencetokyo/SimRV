/**
 * @file SoCManifest.hpp
 * @brief Canonical JSON serialization for normalized SoC presets.
 */
#pragma once

#include <ostream>
#include <string>

#include "simrv/core/SoCConfig.hpp"

namespace simrv::util {

/** Write a deterministic manifest consumed by tooling and future HDL generators. */
auto serialize_soc_manifest(const simrv::core::SoCConfig& config, std::ostream& out) -> bool;

/** Write a manifest to a file, returning false when the file cannot be opened. */
auto save_soc_manifest(const std::string& path, const simrv::core::SoCConfig& config) -> bool;

}  // namespace simrv::util
