/**
 * @file VirtioFsBackend.hpp
 * @brief Host-directory implementation of the VirtIO-FS FUSE request protocol.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "simrv/device/virtio/VirtioCore.hpp"
#include "simrv/util/UniqueFd.hpp"

namespace simrv::device::virtio {

class VirtioFsBackend final {
   public:
    explicit VirtioFsBackend(std::string tag = "simrv");
    ~VirtioFsBackend();

    auto set_shared_directory(const std::filesystem::path& path) -> bool;
    [[nodiscard]] auto is_ready() const noexcept -> bool { return static_cast<bool>(root_fd_); }
    [[nodiscard]] auto tag() const noexcept -> const std::string& { return tag_; }

    /** Process one complete FUSE request and return its FUSE reply. */
    auto process(std::span<const std::byte> request) -> std::vector<std::byte>;

    using DmaRead = std::function<bool(uint64_t, void*, size_t)>;
    using DmaWrite = std::function<bool(uint64_t, const void*, size_t)>;
    using Interrupt = std::function<void()>;
    void process_queue(QueueState& queue, const DmaRead& dma_read, const DmaWrite& dma_write,
                       const Interrupt& interrupt);

   private:
    auto path_for(uint64_t nodeid) const -> std::optional<std::filesystem::path>;
    auto open_beneath(const std::filesystem::path& relative, int flags, uint32_t mode = 0) const
        -> util::UniqueFd;
    auto allocate_node(const std::filesystem::path& relative) -> uint64_t;

    std::string tag_;
    util::UniqueFd root_fd_;
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, std::filesystem::path> nodes_;
    std::unordered_map<uint64_t, uint64_t> node_lookups_;
    std::unordered_map<uint64_t, util::UniqueFd> handles_;
};

}  // namespace simrv::device::virtio
