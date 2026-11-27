/**
 * @file UefiDiskGenerator.hpp
 * @brief Self-contained unprivileged UEFI GPT disk generator (FAT32 ESP + optional rootfs).
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace simrv::util {

struct UefiDiskConfig {
    std::filesystem::path output_image;
    std::filesystem::path kernel_payload;
    std::optional<std::filesystem::path> rootfs_payload;
    uint32_t esp_size_mb{64};
};

/// Generate a partitioned GPT disk image containing an EFI System Partition (FAT32)
/// with \EFI\BOOT\BOOTRISCV64.EFI and an optional second partition for rootfs.
[[nodiscard]] auto generate_uefi_disk(const UefiDiskConfig& config) -> bool;

}  // namespace simrv::util
