/**
 * @file FdtGenerator.cpp
 * @brief Flattened Device Tree (FDT) binary generator implementation.
 */
#include "simrv/util/FdtGenerator.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <unordered_map>

namespace simrv::util {

namespace {

constexpr uint32_t kFdtMagic = 0xd00dfeed;
constexpr uint32_t kFdtVersion = 17;
constexpr uint32_t kFdtLastCompVersion = 16;

constexpr uint32_t kFdtBeginNode = 1;
constexpr uint32_t kFdtEndNode = 2;
constexpr uint32_t kFdtProp = 3;
constexpr uint32_t kFdtEnd = 9;

auto to_be32(uint32_t val) -> uint32_t {
    if constexpr (std::endian::native == std::endian::little) {
        return std::byteswap(val);
    } else {
        return val;
    }
}

auto to_be64(uint64_t val) -> uint64_t {
    if constexpr (std::endian::native == std::endian::little) {
        return std::byteswap(val);
    } else {
        return val;
    }
}

class FdtBuilder {
   public:
    void begin_node(std::string_view name) {
        push_u32(kFdtBeginNode);
        push_string_null_padded(name);
    }

    void end_node() { push_u32(kFdtEndNode); }

    void add_prop_empty(std::string_view name) {
        const uint32_t str_off = get_string_offset(name);
        push_u32(kFdtProp);
        push_u32(0);
        push_u32(str_off);
    }

    void add_prop_u32(std::string_view name, uint32_t val) {
        const uint32_t str_off = get_string_offset(name);
        push_u32(kFdtProp);
        push_u32(sizeof(uint32_t));
        push_u32(str_off);
        push_u32(val);
    }

    void add_prop_u64(std::string_view name, uint64_t val) {
        const uint32_t str_off = get_string_offset(name);
        push_u32(kFdtProp);
        push_u32(sizeof(uint64_t));
        push_u32(str_off);
        push_u64(val);
    }

    void add_prop_u32_array(std::string_view name, const std::vector<uint32_t>& vals) {
        const uint32_t str_off = get_string_offset(name);
        push_u32(kFdtProp);
        push_u32(static_cast<uint32_t>(vals.size() * sizeof(uint32_t)));
        push_u32(str_off);
        for (uint32_t val : vals) {
            push_u32(val);
        }
    }

    void add_prop_string(std::string_view name, std::string_view val) {
        const uint32_t str_off = get_string_offset(name);
        push_u32(kFdtProp);
        push_u32(static_cast<uint32_t>(val.size() + 1));
        push_u32(str_off);
        push_string_null_padded(val);
    }

    void add_prop_string_list(std::string_view name, const std::vector<std::string>& strings) {
        std::vector<uint8_t> data;
        for (const auto& s : strings) {
            data.insert(data.end(), s.begin(), s.end());
            data.push_back(0);
        }
        const uint32_t str_off = get_string_offset(name);
        push_u32(kFdtProp);
        push_u32(static_cast<uint32_t>(data.size()));
        push_u32(str_off);
        push_bytes_padded(data.data(), data.size());
    }

    void add_prop_bytes(std::string_view name, const std::vector<uint8_t>& bytes) {
        const uint32_t str_off = get_string_offset(name);
        push_u32(kFdtProp);
        push_u32(static_cast<uint32_t>(bytes.size()));
        push_u32(str_off);
        push_bytes_padded(bytes.data(), bytes.size());
    }

    auto build() -> std::vector<uint8_t> {
        push_u32(kFdtEnd);

        constexpr uint32_t kHeaderSize = 40;
        constexpr uint32_t kRsvmapSize = 16;  // empty reservation map (2x 64-bit zero)

        const uint32_t off_rsvmap = kHeaderSize;
        const uint32_t off_struct = off_rsvmap + kRsvmapSize;
        const uint32_t size_struct = static_cast<uint32_t>(struct_data_.size());
        const uint32_t off_strings = off_struct + size_struct;
        const uint32_t size_strings = static_cast<uint32_t>(strings_data_.size());
        const uint32_t totalsize = off_strings + size_strings;

        std::vector<uint8_t> fdt(totalsize, 0);

        auto* hdr = reinterpret_cast<uint32_t*>(fdt.data());
        hdr[0] = to_be32(kFdtMagic);
        hdr[1] = to_be32(totalsize);
        hdr[2] = to_be32(off_struct);
        hdr[3] = to_be32(off_strings);
        hdr[4] = to_be32(off_rsvmap);
        hdr[5] = to_be32(kFdtVersion);
        hdr[6] = to_be32(kFdtLastCompVersion);
        hdr[7] = to_be32(0);  // boot_cpuid_phys
        hdr[8] = to_be32(size_strings);
        hdr[9] = to_be32(size_struct);

        // Copy struct data
        std::memcpy(fdt.data() + off_struct, struct_data_.data(), struct_data_.size());
        // Copy strings data
        std::memcpy(fdt.data() + off_strings, strings_data_.data(), strings_data_.size());

        return fdt;
    }

   private:
    void push_u32(uint32_t val) {
        const uint32_t be = to_be32(val);
        const auto* ptr = reinterpret_cast<const uint8_t*>(&be);
        struct_data_.insert(struct_data_.end(), ptr, ptr + sizeof(uint32_t));
    }

    void push_u64(uint64_t val) {
        const uint64_t be = to_be64(val);
        const auto* ptr = reinterpret_cast<const uint8_t*>(&be);
        struct_data_.insert(struct_data_.end(), ptr, ptr + sizeof(uint64_t));
    }

    void push_bytes_padded(const uint8_t* bytes, size_t len) {
        struct_data_.insert(struct_data_.end(), bytes, bytes + len);
        size_t pad = (4 - (len % 4)) % 4;
        for (size_t i = 0; i < pad; ++i) {
            struct_data_.push_back(0);
        }
    }

    void push_string_null_padded(std::string_view s) {
        struct_data_.insert(struct_data_.end(), s.begin(), s.end());
        struct_data_.push_back(0);
        size_t total_len = s.size() + 1;
        size_t pad = (4 - (total_len % 4)) % 4;
        for (size_t i = 0; i < pad; ++i) {
            struct_data_.push_back(0);
        }
    }

    auto get_string_offset(std::string_view s) -> uint32_t {
        auto it = string_offsets_.find(std::string(s));
        if (it != string_offsets_.end()) {
            return it->second;
        }
        auto off = static_cast<uint32_t>(strings_data_.size());
        string_offsets_[std::string(s)] = off;
        strings_data_.insert(strings_data_.end(), s.begin(), s.end());
        strings_data_.push_back(0);
        return off;
    }

    std::vector<uint8_t> struct_data_;
    std::vector<uint8_t> strings_data_;
    std::unordered_map<std::string, uint32_t> string_offsets_;
};

}  // namespace

auto FdtGenerator::generate(const FdtConfig& config) -> std::vector<uint8_t> {
    FdtBuilder b;

    const uint32_t plic_phandle = 2;
    const uint32_t test_phandle = 3;

    // Root node "/"
    b.begin_node("");
    b.add_prop_u32("#address-cells", 2);
    b.add_prop_u32("#size-cells", 2);
    b.add_prop_string("compatible", "simrv,virt");
    b.add_prop_string("model", "simrv,virt-riscv");

    // aliases
    b.begin_node("aliases");
    b.add_prop_string("serial0", "/soc/serial@10000000");
    b.end_node();

    // chosen
    b.begin_node("chosen");
    b.add_prop_string("bootargs", config.bootargs);
    b.add_prop_string("stdout-path", "serial0:115200n8");
    b.end_node();

    // memory
    const std::string mem_node_name = std::format("memory@{:x}", config.dram_base);
    b.begin_node(mem_node_name);
    b.add_prop_string("device_type", "memory");
    std::vector<uint32_t> mem_reg = {
        static_cast<uint32_t>(config.dram_base >> 32),
        static_cast<uint32_t>(config.dram_base & 0xFFFFFFFF),
        static_cast<uint32_t>(config.dram_size >> 32),
        static_cast<uint32_t>(config.dram_size & 0xFFFFFFFF),
    };
    b.add_prop_u32_array("reg", mem_reg);
    b.end_node();  // memory

    // cpus
    b.begin_node("cpus");
    b.add_prop_u32("#address-cells", 1);
    b.add_prop_u32("#size-cells", 0);
    b.add_prop_u32("timebase-frequency", 10000000);

    for (uint32_t h = 0; h < config.num_harts; ++h) {
        const std::string cpu_name = std::format("cpu@{}", h);
        b.begin_node(cpu_name);
        b.add_prop_string("compatible", "riscv");
        b.add_prop_string("device_type", "cpu");
        b.add_prop_u32("reg", h);
        b.add_prop_string("riscv,isa", (config.xlen == 64) ? "rv64imafdcbv" : "rv32imafdcbv");
        b.add_prop_string("riscv,isa-base", (config.xlen == 64) ? "rv64i" : "rv32i");
        b.add_prop_string_list(
            "riscv,isa-extensions",
            {"i", "m", "a", "f", "d", "c", "b", "v", "zicntr", "zicsr", "zifencei", "zihintpause"});
        b.add_prop_string("mmu-type", (config.xlen == 64) ? "riscv,sv39" : "riscv,sv32");
        b.add_prop_u32("clock-frequency", 1000000000);

        // cpu_intc
        b.begin_node("interrupt-controller");
        b.add_prop_u32("#interrupt-cells", 1);
        b.add_prop_u32("#address-cells", 0);
        b.add_prop_string("compatible", "riscv,cpu-intc");
        b.add_prop_empty("interrupt-controller");
        const uint32_t intc_phandle = 1 + h;
        b.add_prop_u32("phandle", intc_phandle);
        b.end_node();

        b.end_node();  // cpu@h
    }
    b.end_node();  // cpus

    // poweroff
    b.begin_node("poweroff");
    b.add_prop_string("compatible", "syscon-poweroff");
    b.add_prop_u32("regmap", test_phandle);
    b.add_prop_u32("offset", 0);
    b.add_prop_u32("value", 0x5555);
    b.end_node();

    // reboot
    b.begin_node("reboot");
    b.add_prop_string("compatible", "syscon-reboot");
    b.add_prop_u32("regmap", test_phandle);
    b.add_prop_u32("offset", 0);
    b.add_prop_u32("value", 0x7777);
    b.end_node();

    // soc
    b.begin_node("soc");
    b.add_prop_u32("#address-cells", 2);
    b.add_prop_u32("#size-cells", 2);
    b.add_prop_string("compatible", "simple-bus");
    b.add_prop_empty("ranges");

    // test / syscon power device
    b.begin_node("test@100000");
    b.add_prop_string_list("compatible", {"sifive,test1", "sifive,test0", "syscon"});
    b.add_prop_u32_array("reg", {0, 0x100000, 0, 0x1000});
    b.add_prop_u32("phandle", test_phandle);
    b.end_node();

    // clint
    b.begin_node("timer@60000000");
    b.add_prop_string_list("compatible", {"sifive,clint0", "riscv,clint0"});
    b.add_prop_u32_array("reg", {0, 0x60000000, 0, 0x000c0000});
    b.add_prop_u32("clock-frequency", 10000000);

    std::vector<uint32_t> clint_irqs;
    for (uint32_t h = 0; h < config.num_harts; ++h) {
        const uint32_t intc_phandle = 1 + h;
        clint_irqs.push_back(intc_phandle);
        clint_irqs.push_back(3);  // M-mode software interrupt
        clint_irqs.push_back(intc_phandle);
        clint_irqs.push_back(7);  // M-mode timer interrupt
    }
    b.add_prop_u32_array("interrupts-extended", clint_irqs);
    b.end_node();

    // plic
    b.begin_node("interrupt-controller@50000000");
    b.add_prop_u32("#address-cells", 0);
    b.add_prop_u32("#interrupt-cells", 1);
    b.add_prop_string_list("compatible", {"sifive,plic-1.0.0", "riscv,plic0"});
    b.add_prop_empty("interrupt-controller");
    b.add_prop_u32_array("reg", {0, 0x50000000, 0, 0x00400000});
    b.add_prop_u32("riscv,ndev", 31);
    b.add_prop_u32("phandle", plic_phandle);

    std::vector<uint32_t> plic_irqs;
    for (uint32_t h = 0; h < config.num_harts; ++h) {
        const uint32_t intc_phandle = 1 + h;
        plic_irqs.push_back(intc_phandle);
        plic_irqs.push_back(11);  // M-mode external interrupt
        plic_irqs.push_back(intc_phandle);
        plic_irqs.push_back(9);  // S-mode external interrupt
    }
    b.add_prop_u32_array("interrupts-extended", plic_irqs);
    b.end_node();

    // uart (serial@10000000)
    b.begin_node("serial@10000000");
    b.add_prop_string("compatible", "ns16550a");
    b.add_prop_u32_array("reg", {0, 0x10000000, 0, 0x100});
    b.add_prop_u32("clock-frequency", 3686400);
    b.add_prop_u32("current-speed", 115200);
    b.add_prop_u32("reg-shift", 0);
    b.add_prop_u32("reg-io-width", 1);
    b.add_prop_u32("interrupt-parent", plic_phandle);
    b.add_prop_u32("interrupts", 3);
    b.end_node();

    // VirtIO-MMIO v2 nodes
    if (config.enable_mmio) {
        // virtio-console (virtio@10002000)
        b.begin_node("virtio@10002000");
        b.add_prop_string("compatible", "virtio,mmio");
        b.add_prop_u32_array("reg", {0, 0x10002000, 0, 0x1000});
        b.add_prop_u32("interrupt-parent", plic_phandle);
        b.add_prop_u32("interrupts", 1);
        b.end_node();

        // virtio-disk (virtio@10001000)
        b.begin_node("virtio@10001000");
        b.add_prop_string("compatible", "virtio,mmio");
        b.add_prop_u32_array("reg", {0, 0x10001000, 0, 0x1000});
        b.add_prop_u32("interrupt-parent", plic_phandle);
        b.add_prop_u32("interrupts", 2);
        b.end_node();

        // virtio-rng (virtio@10003000)
        b.begin_node("virtio@10003000");
        b.add_prop_string("compatible", "virtio,mmio");
        b.add_prop_u32_array("reg", {0, 0x10003000, 0, 0x1000});
        b.add_prop_u32("interrupt-parent", plic_phandle);
        b.add_prop_u32("interrupts", 4);
        b.end_node();

        // virtio-gpu (virtio@10004000)
        b.begin_node("virtio@10004000");
        b.add_prop_string("compatible", "virtio,mmio");
        b.add_prop_u32_array("reg", {0, 0x10004000, 0, 0x1000});
        b.add_prop_u32("interrupt-parent", plic_phandle);
        b.add_prop_u32("interrupts", 5);
        b.end_node();

        // virtio-input (virtio@10005000)
        b.begin_node("virtio@10005000");
        b.add_prop_string("compatible", "virtio,mmio");
        b.add_prop_u32_array("reg", {0, 0x10005000, 0, 0x1000});
        b.add_prop_u32("interrupt-parent", plic_phandle);
        b.add_prop_u32("interrupts", 6);
        b.end_node();

        // virtio-sound (virtio@10006000)
        b.begin_node("virtio@10006000");
        b.add_prop_string("compatible", "virtio,mmio");
        b.add_prop_u32_array("reg", {0, 0x10006000, 0, 0x1000});
        b.add_prop_u32("interrupt-parent", plic_phandle);
        b.add_prop_u32("interrupts", 7);
        b.end_node();

        // virtio-net (virtio@10007000)
        b.begin_node("virtio@10007000");
        b.add_prop_string("compatible", "virtio,mmio");
        b.add_prop_u32_array("reg", {0, 0x10007000, 0, 0x1000});
        b.add_prop_u32("interrupt-parent", plic_phandle);
        b.add_prop_u32("interrupts", 8);
        b.add_prop_bytes("local-mac-address", {0x52, 0x54, 0x00, 0x12, 0x34, 0x56});
        b.end_node();
    }

    if (config.enable_pcie) {
        // pcie root complex (pci@30000000)
        const uint32_t pcie_phandle = 10;
        b.begin_node("pci@30000000");
        b.add_prop_string("compatible", "pci-host-ecam-generic");
        b.add_prop_string("device_type", "pci");
        b.add_prop_u32("#address-cells", 3);
        b.add_prop_u32("#size-cells", 2);
        b.add_prop_u32("#interrupt-cells", 1);
        b.add_prop_u32_array("reg", {0, 0x30000000, 0, 0x10000000});
        b.add_prop_u32_array("bus-range", {0, 255});
        b.add_prop_u32_array("ranges", {0x02000000, 0, 0x40000000, 0, 0x40000000, 0, 0x10000000});
        b.add_prop_u32("phandle", pcie_phandle);
        b.add_prop_u32("interrupt-parent", plic_phandle);
        b.add_prop_u32_array("interrupt-map-mask", {0x3800, 0, 0, 7});
        b.add_prop_u32_array("interrupt-map",
                             {0x0800, 0, 0, 1, plic_phandle, 17, 0x1000, 0, 0, 1, plic_phandle, 18,
                              0x1800, 0, 0, 1, plic_phandle, 19, 0x2000, 0, 0, 1, plic_phandle, 20,
                              0x2800, 0, 0, 1, plic_phandle, 21, 0x3000, 0, 0, 1, plic_phandle, 22,
                              0x3800, 0, 0, 1, plic_phandle, 23});
        b.end_node();
    }

    // rtc (rtc@70000000)
    b.begin_node("rtc@70000000");
    b.add_prop_string("compatible", "google,goldfish-rtc");
    b.add_prop_u32_array("reg", {0, 0x70000000, 0, 0x1000});
    b.add_prop_u32("interrupt-parent", plic_phandle);
    b.add_prop_u32("interrupts", 11);
    b.end_node();

    b.end_node();  // soc

    b.end_node();  // root

    return b.build();
}

}  // namespace simrv::util
