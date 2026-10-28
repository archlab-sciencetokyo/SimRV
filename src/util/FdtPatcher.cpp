#include "simrv/util/FdtPatcher.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::util {
namespace {
constexpr uint32_t kMagic = 0xd00dfeed;
constexpr uint32_t kBeginNode = 1, kEndNode = 2, kProp = 3, kNop = 4, kEnd = 9;
struct Prop {
    std::string name;
    std::vector<uint8_t> data;
};
struct Node {
    std::string name;
    std::vector<Prop> props;
    std::vector<Node> children;
};
auto be32(std::span<const uint8_t> b, size_t p) -> uint32_t {
    return (uint32_t{b[p]} << 24) | (uint32_t{b[p + 1]} << 16) | (uint32_t{b[p + 2]} << 8) |
           b[p + 3];
}
auto be64(std::span<const uint8_t> b, size_t p) -> uint64_t {
    return (uint64_t{be32(b, p)} << 32) | be32(b, p + 4);
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(v >> 24);
    b.push_back(v >> 16);
    b.push_back(v >> 8);
    b.push_back(v);
}
void put64(std::vector<uint8_t>& b, uint64_t v) {
    put32(b, v >> 32);
    put32(b, v);
}
auto align4(size_t n) -> size_t { return (n + 3) & ~size_t{3}; }
auto align8(size_t n) -> size_t { return (n + 7) & ~size_t{7}; }

struct Parser {
    std::span<const uint8_t> data, strings;
    size_t pos = 0;
    auto node() -> Node {
        while (pos + 4 <= data.size() && be32(data, pos) == kNop) {
            pos += 4;
        }
        if (pos + 4 > data.size() || be32(data, pos) != kBeginNode)
            throw std::runtime_error("invalid FDT node token");
        pos += 4;
        const size_t start = pos;
        while (pos < data.size() && data[pos]) ++pos;
        if (pos == data.size()) throw std::runtime_error("unterminated FDT node name");
        Node n;
        n.name.assign(reinterpret_cast<const char*>(data.data() + start), pos - start);
        pos = align4(pos + 1);
        while (pos + 4 <= data.size()) {
            auto tok = be32(data, pos);
            pos += 4;
            if (tok == kNop) continue;
            if (tok == kEndNode) return n;
            if (tok == kBeginNode) {
                pos -= 4;
                n.children.push_back(node());
                continue;
            }
            if (tok != kProp || pos + 8 > data.size())
                throw std::runtime_error("unsupported FDT structure token");
            const auto len = be32(data, pos), nameoff = be32(data, pos + 4);
            pos += 8;
            if (nameoff >= strings.size() || len > data.size() - pos)
                throw std::runtime_error("invalid FDT property bounds");
            size_t end = nameoff;
            while (end < strings.size() && strings[end]) ++end;
            if (end == strings.size()) throw std::runtime_error("invalid FDT property name");
            Prop p;
            p.name.assign(reinterpret_cast<const char*>(strings.data() + nameoff), end - nameoff);
            p.data.assign(data.begin() + pos, data.begin() + pos + len);
            n.props.push_back(std::move(p));
            pos = align4(pos + len);
        }
        throw std::runtime_error("unterminated FDT node");
    }
};
auto prop(const Node& n, std::string_view name) -> const Prop* {
    for (const auto& p : n.props)
        if (p.name == name) return &p;
    return nullptr;
}
auto prop(Node& n, std::string_view name) -> Prop* {
    for (auto& p : n.props)
        if (p.name == name) return &p;
    return nullptr;
}
auto child(Node& n, std::string_view name) -> Node* {
    for (auto& c : n.children)
        if (c.name == name) return &c;
    return nullptr;
}
auto cells(const Node& n, std::string_view name, uint32_t fallback) -> uint32_t {
    const auto* p = prop(n, name);
    return p && p->data.size() == 4 ? be32(p->data, 0) : fallback;
}
auto encode_cells(uint64_t v, uint32_t count) -> std::vector<uint8_t> {
    std::vector<uint8_t> out;
    if (count == 1)
        put32(out, static_cast<uint32_t>(v));
    else
        put64(out, v);
    return out;
}
void set_prop(Node& n, std::string name, std::vector<uint8_t> value) {
    if (auto* p = prop(n, name))
        p->data = std::move(value);
    else
        n.props.push_back({std::move(name), std::move(value)});
}
auto is_memory(Node& n) -> bool {
    if (n.name.starts_with("memory@")) return true;
    auto* type = prop(n, "device_type");
    return type &&
           std::string_view(reinterpret_cast<const char*>(type->data.data()), type->data.size())
               .starts_with("memory");
}
}  // namespace

auto patch_fdt(std::span<const uint8_t> blob, uint64_t dram_base, uint64_t dram_size,
               uint32_t num_harts, FdtFramebufferInfo* out_fb, uint64_t timebase_hz,
               uint64_t cpu_clock_hz) -> std::expected<std::vector<uint8_t>, std::string> {
    try {
        if (blob.size() < 40 || be32(blob, 0) != kMagic)
            return std::unexpected("invalid device tree header");
        const auto total = be32(blob, 4), struct_off = be32(blob, 8), strings_off = be32(blob, 12),
                   reserve_off = be32(blob, 16), version = be32(blob, 20),
                   boot_cpuid_phys = be32(blob, 28);
        if (total > blob.size() || total < 40 || struct_off >= total || strings_off >= total ||
            reserve_off >= total || version < 17)
            return std::unexpected("unsupported or truncated device tree");
        const auto struct_size = be32(blob, 36), strings_size = be32(blob, 32);
        if (struct_off + struct_size > total || strings_off + strings_size > total)
            return std::unexpected("invalid device tree block sizes");
        Parser parser{blob.subspan(struct_off, struct_size),
                      blob.subspan(strings_off, strings_size)};
        auto root = parser.node();
        if (out_fb != nullptr) {
            *out_fb = {};
            for (const auto& c : root.children) {
                if (c.name.starts_with("framebuffer@") || c.name == "framebuffer") {
                    const auto* wp = prop(c, "width");
                    const auto* hp = prop(c, "height");
                    const auto* sp = prop(c, "stride");
                    const auto* rp = prop(c, "reg");
                    if (wp != nullptr && wp->data.size() >= 4) out_fb->width = be32(wp->data, 0);
                    if (hp != nullptr && hp->data.size() >= 4) out_fb->height = be32(hp->data, 0);
                    if (sp != nullptr && sp->data.size() >= 4) out_fb->stride = be32(sp->data, 0);
                    if (rp != nullptr && rp->data.size() >= 4) {
                        const auto ac = cells(root, "#address-cells", 2);
                        const auto sc = cells(root, "#size-cells", 1);
                        if (ac == 2 && rp->data.size() >= 8 + sc * 4) {
                            out_fb->base = be64(rp->data, 0);
                            out_fb->size = (sc == 2) ? be64(rp->data, 8) : be32(rp->data, 8);
                        } else if (ac == 1 && rp->data.size() >= 4 + sc * 4) {
                            out_fb->base = be32(rp->data, 0);
                            out_fb->size = (sc == 2) ? be64(rp->data, 4) : be32(rp->data, 4);
                        }
                    }
                    break;
                }
            }
        }
        std::vector<Node*> memory_nodes;
        for (auto& c : root.children)
            if (is_memory(c)) memory_nodes.push_back(&c);
        if (memory_nodes.empty()) return std::unexpected("device tree has no memory node");
        if (memory_nodes.size() != 1)
            return std::unexpected(
                "device tree must describe SimRV RAM with exactly one memory node");
        auto* memory = memory_nodes.front();
        memory->name = std::format("memory@{:x}", dram_base);
        set_prop(*memory, "device_type", {'m', 'e', 'm', 'o', 'r', 'y', '\0'});
        const auto ac = cells(root, "#address-cells", 2), sc = cells(root, "#size-cells", 1);
        if ((ac != 1 && ac != 2) || (sc != 1 && sc != 2))
            return std::unexpected("memory node uses unsupported address/size cells");
        if ((ac == 1 && dram_base > UINT32_MAX) || (sc == 1 && dram_size > UINT32_MAX))
            return std::unexpected("configured DRAM range does not fit the device tree cells");
        std::vector<uint8_t> reg = encode_cells(dram_base, ac), size = encode_cells(dram_size, sc);
        reg.insert(reg.end(), size.begin(), size.end());
        set_prop(*memory, "reg", std::move(reg));
        auto* cpus = child(root, "cpus");
        if (!cpus) return std::unexpected("device tree has no /cpus node");
        const auto cpu_ac = cells(*cpus, "#address-cells", 1);
        if (cpu_ac != 1 && cpu_ac != 2)
            return std::unexpected("CPU nodes use unsupported hart ID cells");
        std::set<uint64_t> ids;
        std::vector<Node*> cpu_nodes;
        for (auto& c : cpus->children) {
            if (!c.name.starts_with("cpu@")) continue;
            auto* rp = prop(c, "reg");
            if (!rp || rp->data.size() != cpu_ac * 4)
                return std::unexpected("CPU node is missing a valid reg hart ID");
            const auto id = cpu_ac == 1 ? be32(rp->data, 0) : be64(rp->data, 0);
            if (!ids.insert(id).second)
                return std::unexpected("duplicate CPU hart ID in device tree");
            cpu_nodes.push_back(&c);
        }
        for (uint32_t i = 0; i < num_harts; ++i)
            if (!ids.contains(i))
                return std::unexpected(
                    "device tree must contain CPU nodes for every configured hart (missing hart " +
                    std::to_string(i) + ")");
        if (timebase_hz != 0) {
            std::vector<uint8_t> tb_data;
            put32(tb_data, static_cast<uint32_t>(timebase_hz));
            set_prop(*cpus, "timebase-frequency", std::move(tb_data));
        }
        for (auto* c : cpu_nodes) {
            auto* rp = prop(*c, "reg");
            const auto id = cpu_ac == 1 ? be32(rp->data, 0) : be64(rp->data, 0);
            const std::string status = id < num_harts ? "okay" : "disabled";
            set_prop(*c, "status", std::vector<uint8_t>(status.begin(), status.end()));
            prop(*c, "status")->data.push_back(0);
            if (cpu_clock_hz != 0) {
                std::vector<uint8_t> clk_data;
                put32(clk_data, static_cast<uint32_t>(cpu_clock_hz));
                set_prop(*c, "clock-frequency", std::move(clk_data));
            }
            std::vector<uint8_t> cbom_sz;
            put32(cbom_sz, 64);
            set_prop(*c, "riscv,cbom-block-size", cbom_sz);
            std::vector<uint8_t> cbop_sz;
            put32(cbop_sz, 64);
            set_prop(*c, "riscv,cbop-block-size", std::move(cbop_sz));
            std::vector<uint8_t> cboz_sz;
            put32(cboz_sz, 64);
            set_prop(*c, "riscv,cboz-block-size", std::move(cboz_sz));
            auto* isa_ext_prop = prop(*c, "riscv,isa-extensions");
            if (isa_ext_prop == nullptr) {
                set_prop(*c, "riscv,isa-extensions", {});
                isa_ext_prop = prop(*c, "riscv,isa-extensions");
            }
            if (isa_ext_prop != nullptr) {
                // Advertise only supported additions; this is not the complete RVA23 set.
                static constexpr std::string_view kSupportedGuestExts[] = {
                    "zihintntl", "zkt",    "zvbb",   "zvkt",   "zic64b", "zicclsm", "za64rs", "ziccamoa", "zfhmin",
                    "zvfhmin", "zfa", "zicond", "zicboz",
                    "zicbom",    "zicbop", "zawrs",   "svinval", "svpbmt", "svnapot", "sstc",
                    "sscofpmf", "ssu64xl"};
                for (const auto ext : kSupportedGuestExts) {
                    if (ext == "ssu64xl" && !simrv::xlen::kIsXLen64) continue;
                    bool present = false;
                    size_t offset = 0;
                    while (offset < isa_ext_prop->data.size()) {
                        const auto remaining = isa_ext_prop->data.size() - offset;
                        const auto* token = isa_ext_prop->data.data() + offset;
                        const auto* terminator =
                            static_cast<const uint8_t*>(std::memchr(token, 0, remaining));
                        if (terminator == nullptr) break;
                        const auto length = static_cast<size_t>(terminator - token);
                        const auto* begin = reinterpret_cast<const char*>(token);
                        if (std::string_view(begin, length) == ext) {
                            present = true;
                            break;
                        }
                        offset += length + 1;
                    }
                    if (!present) {
                        isa_ext_prop->data.insert(isa_ext_prop->data.end(), ext.begin(), ext.end());
                        isa_ext_prop->data.push_back('\0');
                    }
                }
            }
        }
        std::set<std::string> property_names;
        auto gather = [&](auto&& self, const Node& n) -> void {
            for (const auto& p : n.props) property_names.insert(p.name);
            for (const auto& c : n.children) self(self, c);
        };
        gather(gather, root);
        std::vector<uint8_t> structure, strings;
        std::map<std::string, uint32_t> offsets;
        for (const auto& name : property_names) {
            offsets[name] = static_cast<uint32_t>(strings.size());
            strings.insert(strings.end(), name.begin(), name.end());
            strings.push_back(0);
        }
        auto emit = [&](auto&& self, const Node& n) -> void {
            put32(structure, kBeginNode);
            structure.insert(structure.end(), n.name.begin(), n.name.end());
            structure.push_back(0);
            structure.resize(align4(structure.size()), 0);
            for (const auto& p : n.props) {
                put32(structure, kProp);
                put32(structure, p.data.size());
                put32(structure, offsets[p.name]);
                structure.insert(structure.end(), p.data.begin(), p.data.end());
                structure.resize(align4(structure.size()), 0);
            }
            for (const auto& c : n.children) self(self, c);
            put32(structure, kEndNode);
        };
        emit(emit, root);
        put32(structure, kEnd);
        size_t r = reserve_off;
        while (r + 16 <= total) {
            auto a = be64(blob, r), s = be64(blob, r + 8);
            r += 16;
            if (a == 0 && s == 0) break;
        }
        if (r > total) return std::unexpected("invalid FDT reservation map");
        const uint32_t new_reserve = 40,
                       new_struct = static_cast<uint32_t>(align4(new_reserve + (r - reserve_off))),
                       new_strings = new_struct + structure.size(),
                       new_total = static_cast<uint32_t>(align8(new_strings + strings.size()));
        std::vector<uint8_t> out(40, 0);
        auto write_header = [&](size_t off, uint32_t v) {
            out[off] = v >> 24;
            out[off + 1] = v >> 16;
            out[off + 2] = v >> 8;
            out[off + 3] = v;
        };
        out.insert(out.end(), blob.begin() + reserve_off, blob.begin() + r);
        out.resize(new_struct, 0);
        out.insert(out.end(), structure.begin(), structure.end());
        out.insert(out.end(), strings.begin(), strings.end());
        out.resize(new_total, 0);
        write_header(0, kMagic);
        write_header(4, new_total);
        write_header(8, new_struct);
        write_header(12, new_strings);
        write_header(16, new_reserve);
        write_header(20, 17);
        write_header(24, 16);
        write_header(28, boot_cpuid_phys);
        write_header(32, strings.size());
        write_header(36, structure.size());
        return out;
    } catch (const std::exception& e) {
        return std::unexpected(std::string("failed to patch device tree: ") + e.what());
    }
}
}  // namespace simrv::util
