/**
 * @file BuildrootImageBundle.hpp
 * @brief Locate the matching firmware, DTB, and rootfs in a SimRV Buildroot image bundle.
 */
#pragma once

#include <charconv>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <format>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace simrv::util {

struct BuildrootImageBundle {
    std::filesystem::path firmware;
    std::filesystem::path dtb;
    std::filesystem::path rootfs;
    std::optional<uint64_t> dram_size_bytes;
};

[[nodiscard]] inline auto resolve_buildroot_image_bundle(const std::filesystem::path& directory)
    -> std::expected<BuildrootImageBundle, std::string> {
    namespace fs = std::filesystem;
    std::error_code error;
    if (!fs::is_directory(directory, error) || error) {
        return std::unexpected(std::format("Buildroot output directory not found: {}",
                                           directory.string()));
    }
    const fs::path images = directory.filename() == "images" ? directory : directory / "images";
    const auto locate = [&](std::initializer_list<fs::path> candidates,
                            std::string_view label) -> std::expected<fs::path, std::string> {
        for (const auto& candidate : candidates) {
            error.clear();
            if (fs::is_regular_file(candidate, error) && !error) return candidate;
        }
        return std::unexpected(std::format(
            "Buildroot output '{}' is missing {}; expected artifacts in '{}'",
            directory.string(), label, images.string()));
    };
    // Accept the canonical SimRV bundle name first, then Buildroot's and older
    // SimRV bundles' standard OpenSBI filename for backwards compatibility.
    auto firmware = locate({images / "opensbi-linux-payload.elf",
                            directory / "opensbi-linux-payload.elf", images / "fw_payload.bin",
                            directory / "fw_payload.bin"},
                           "OpenSBI Linux payload");
    if (!firmware) return std::unexpected(firmware.error());
    auto dtb = locate({images / "devicetree.dtb", images / "virt.dtb",
                       directory / "devicetree.dtb"}, "devicetree.dtb");
    if (!dtb) return std::unexpected(dtb.error());
    // Buildroot emits rootfs.ext4; root.img is retained for old output folders.
    auto rootfs = locate({images / "rootfs.img", images / "rootfs.ext4", images / "root.img",
                          directory / "rootfs.img", directory / "root.img"},
                         "root filesystem image");
    if (!rootfs) return std::unexpected(rootfs.error());
    std::optional<uint64_t> dram_size_bytes;
    for (const auto& manifest : {images / "simrv-manifest.json", images / "manifest.json",
                                 directory / "manifest.json"}) {
        std::ifstream input(manifest);
        if (!input) continue;
        const std::string contents{std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>()};
        const auto key = contents.find("\"dram_size_mb\"");
        if (key == std::string::npos) continue;
        const auto colon = contents.find(':', key);
        if (colon == std::string::npos) continue;
        auto begin = contents.data() + colon + 1;
        const auto end = contents.data() + contents.size();
        while (begin != end && (*begin == ' ' || *begin == '\t')) ++begin;
        uint64_t megabytes = 0;
        const auto parsed = std::from_chars(begin, end, megabytes);
        if (parsed.ec == std::errc{} && megabytes != 0 &&
            megabytes <= std::numeric_limits<uint64_t>::max() / (1024 * 1024)) {
            dram_size_bytes = megabytes * 1024 * 1024;
            break;
        }
    }
    return BuildrootImageBundle{std::move(*firmware), std::move(*dtb), std::move(*rootfs),
                                dram_size_bytes};
}

}  // namespace simrv::util
