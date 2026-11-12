/**
 * @file BuildrootImageBundle.hpp
 * @brief Locate the matching firmware, DTB, and rootfs in a SimRV Buildroot image bundle.
 */
#pragma once

#include <charconv>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
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

[[nodiscard]] inline auto locate_default_guest_bundle_dir()
    -> std::optional<std::filesystem::path> {
    namespace fs = std::filesystem;
    std::error_code error;

    const auto check_dir = [&](const fs::path& p) -> std::optional<fs::path> {
        if (p.empty() || !fs::is_directory(p, error) || error) return std::nullopt;
        const fs::path img_dir = p.filename() == "images" ? p : p / "images";
        for (const auto& d : {p, img_dir}) {
            bool has_fw = fs::exists(d / "opensbi-linux-payload.elf", error) ||
                          fs::exists(d / "fw_payload.bin", error);
            bool has_dtb =
                fs::exists(d / "devicetree.dtb", error) || fs::exists(d / "virt.dtb", error);
            if (has_fw && has_dtb) return p;
        }
        return std::nullopt;
    };

    // Check environment variables first
    for (const char* env_name :
         {"SIMRV_GUEST_BUILD_DIR", "SIMRV_GUEST_BUILDS_DIR", "SIMRV_LINUX_BUILD_DIR",
          "SIMRV_LINUX_BUILDS_DIR", "SIMRV_GUEST_IMAGES_DIR", "SIMRV_LINUX_IMAGES_DIR"}) {
        if (const char* val = std::getenv(env_name); val && *val) {
            fs::path p(val);
            if (auto found = check_dir(p)) return found;
            for (const auto& sub :
                 {"alpine-gui", "buildroot-output-rv64", "buildroot-fpga", "rv64/alpine-gui",
                  "rv32/alpine-gui", "rv64/buildroot-fpga", "rv32/buildroot-fpga"}) {
                if (auto found = check_dir(p / sub)) return found;
            }
        }
    }

    // Check standard repository paths
    for (const auto& local_cand :
         {"linux-images/rv64/alpine-gui", "linux-images/rv32/alpine-gui",
          "linux-images/rv64/buildroot-fpga", "linux-images/rv32/buildroot-fpga",
          "linux-build/buildroot-output-rv64", "linux-build/buildroot-output-rv32", "linux-build",
          "linux-images"}) {
        if (auto found = check_dir(local_cand)) return found;
    }

    return std::nullopt;
}

[[nodiscard]] inline auto resolve_buildroot_image_bundle(const std::filesystem::path& directory)
    -> std::expected<BuildrootImageBundle, std::string> {
    namespace fs = std::filesystem;
    std::error_code error;
    fs::path target = directory;
    if (target.empty() || target == "auto" || target == "default") {
        if (auto auto_dir = locate_default_guest_bundle_dir()) {
            target = *auto_dir;
        } else {
            return std::unexpected(
                "Guest bundle not found: auto-detection checked SIMRV_GUEST_BUILD_DIR, "
                "SIMRV_LINUX_BUILD_DIR, and SIMRV_LINUX_IMAGES_DIR");
        }
    }
    if (!fs::is_directory(target, error) || error) {
        return std::unexpected(
            std::format("Buildroot output directory not found: {}", target.string()));
    }
    const fs::path images = target.filename() == "images" ? target : target / "images";
    const auto locate = [&](std::initializer_list<fs::path> candidates,
                            std::string_view label) -> std::expected<fs::path, std::string> {
        for (const auto& candidate : candidates) {
            error.clear();
            if (fs::is_regular_file(candidate, error) && !error) return candidate;
        }
        return std::unexpected(
            std::format("Buildroot output '{}' is missing {}; expected artifacts in '{}'",
                        target.string(), label, images.string()));
    };
    // Accept the canonical SimRV bundle name first, then Buildroot's and older
    // SimRV bundles' standard OpenSBI filename for backwards compatibility.
    auto firmware =
        locate({images / "opensbi-linux-payload.elf", target / "opensbi-linux-payload.elf",
                images / "fw_payload.bin", target / "fw_payload.bin"},
               "OpenSBI Linux payload");
    if (!firmware) return std::unexpected(firmware.error());
    auto dtb = locate({images / "devicetree.dtb", images / "virt.dtb", target / "devicetree.dtb"},
                      "devicetree.dtb");
    if (!dtb) return std::unexpected(dtb.error());
    // Buildroot emits rootfs.ext4; root.img is retained for old output folders.
    auto rootfs = locate({images / "rootfs.img", images / "rootfs.ext4", images / "root.img",
                          target / "rootfs.img", target / "root.img"},
                         "root filesystem image");
    if (!rootfs) return std::unexpected(rootfs.error());
    std::optional<uint64_t> dram_size_bytes;
    for (const auto& manifest :
         {images / "simrv-manifest.json", images / "manifest.json", target / "manifest.json"}) {
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
    return BuildrootImageBundle{.firmware = std::move(*firmware),
                                .dtb = std::move(*dtb),
                                .rootfs = std::move(*rootfs),
                                .dram_size_bytes = dram_size_bytes};
}

}  // namespace simrv::util
