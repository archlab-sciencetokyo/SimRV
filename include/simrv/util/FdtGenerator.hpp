/**
 * @file FdtGenerator.hpp
 * @brief Programmatic in-memory Flattened Device Tree (FDT) generator.
 */
#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/MachineConfig.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::device {
class PlatformDeviceRegistry;
}

namespace simrv::util {

/**
 * @class FdtGenerator
 * @brief Constructs binary Flattened Device Trees (DTB) adhering to Devicetree Spec v0.4.
 *
 * Programmatically builds `/cpus`, `/memory`, `/soc`, and peripheral nodes directly
 * in guest RAM without requiring static template DTB files on host disk.
 */
class FdtGenerator {
   public:
    FdtGenerator();

    void begin_node(std::string_view name);
    void end_node();

    void property_u32(std::string_view name, uint32_t value);
    void property_u64(std::string_view name, uint64_t value);
    void property_string(std::string_view name, std::string_view value);
    void property_stringlist(std::string_view name, std::span<const std::string_view> strings);
    void property_array_u32(std::string_view name, std::span<const uint32_t> values);
    void property_array_u32(std::string_view name, std::initializer_list<uint32_t> values) {
        property_array_u32(name, std::span<const uint32_t>(values.begin(), values.end()));
    }
    void property_array_u64(std::string_view name, std::span<const uint64_t> values);
    void property_array_u64(std::string_view name, std::initializer_list<uint64_t> values) {
        property_array_u64(name, std::span<const uint64_t>(values.begin(), values.end()));
    }
    void property_empty(std::string_view name);
    void property_raw(std::string_view name, std::span<const uint8_t> data);

    [[nodiscard]] auto serialize() const -> std::vector<uint8_t>;

   private:
    [[nodiscard]] auto get_string_offset(std::string_view name) -> uint32_t;
    static void put_be32(std::vector<uint8_t>& buf, uint32_t val);
    static void put_be64(std::vector<uint8_t>& buf, uint64_t val);
    static void pad4(std::vector<uint8_t>& buf);

    std::vector<uint8_t> struct_data_{};
    std::vector<uint8_t> string_data_{};
    mutable std::map<std::string, uint32_t> string_offsets_{};
};

/**
 * @brief Synthesizes a complete, valid device tree for the current machine configuration.
 */
auto generate_platform_fdt(const core::MachineConfig& config,
                           const device::PlatformDeviceRegistry* registry = nullptr)
    -> std::vector<uint8_t>;

}  // namespace simrv::util
