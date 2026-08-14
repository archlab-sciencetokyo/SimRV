#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/MmioRouter.hpp"

namespace {

auto failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class TestNode final : public simrv::memory::TileLinkNode {
   public:
    TestNode(Address base, Address size, const char* name)
        : base_(base), size_(size), name_(name) {}

    [[nodiscard]] auto name() const -> const char* override { return name_; }
    [[nodiscard]] auto base_address() const -> Address override { return base_; }
    [[nodiscard]] auto size() const -> Address override { return size_; }
    auto handle_request(const simrv::memory::TlChannelA& req, simrv::memory::TlChannelD& resp)
        -> bool override {
        ++requests_;
        resp.data = req.address;
        return true;
    }
    [[nodiscard]] auto requests() const -> unsigned { return requests_; }

   private:
    Address base_;
    Address size_;
    const char* name_;
    unsigned requests_ = 0;
};

class NodeWithHole final : public simrv::memory::TileLinkNode {
   public:
    [[nodiscard]] auto name() const -> const char* override { return "node-with-hole"; }
    [[nodiscard]] auto base_address() const -> Address override { return 0x4000; }
    [[nodiscard]] auto size() const -> Address override { return 0x100; }
    [[nodiscard]] auto contains(Address address) const -> bool override {
        return address >= 0x4000 && address < 0x4100 && !(address >= 0x4010 && address < 0x4020);
    }
    auto handle_request(const simrv::memory::TlChannelA&, simrv::memory::TlChannelD&)
        -> bool override {
        return true;
    }
};

void test_unaligned_host_access() {
    std::array<Byte, 16> bytes{};
    simrv::memory::host_write_fast(bytes.data() + 1, static_cast<Register>(0x8877665544332211ULL),
                                   static_cast<Instruction>(simrv::isa::Funct3::Sd));
    const Word value = simrv::memory::host_read_fast(
        bytes.data() + 1, static_cast<Instruction>(simrv::isa::Funct3::Ld));
    if constexpr (simrv::xlen::kIsXLen64) {
        expect(value == static_cast<Word>(0x8877665544332211ULL),
               "unaligned 64-bit host access preserves all bytes");
    }

    bytes.fill(Byte{});
    simrv::memory::host_write_fast(bytes.data() + 3, static_cast<Register>(0xA1B2C3D4U),
                                   static_cast<Instruction>(simrv::isa::Funct3::Sw));
    expect(simrv::memory::host_read_fast(bytes.data() + 3,
                                         static_cast<Instruction>(simrv::isa::Funct3::Lwu)) ==
               static_cast<Word>(0xA1B2C3D4U),
           "unaligned 32-bit host access preserves all bytes");
}

void test_mmio_ranges() {
    simrv::memory::MmioRouter router;
    TestNode inner(0x1100, 0x100, "inner");
    TestNode containing(0x1000, 0x1000, "containing");
    TestNode adjacent(0x1200, 0x100, "adjacent");
    TestNode empty(0x3000, 0, "empty");
    TestNode wrapping(std::numeric_limits<Address>::max() - 1, 4, "wrapping");
    NodeWithHole node_with_hole;
    TestNode hole_device(0x4010, 0x10, "hole-device");

    expect(router.register_device(&inner), "valid MMIO node registers");
    expect(!router.register_device(&containing), "containing MMIO overlap is rejected");
    expect(router.register_device(&adjacent), "adjacent MMIO ranges do not overlap");
    expect(!router.register_device(&empty), "empty MMIO range is rejected");
    expect(!router.register_device(&wrapping), "wrapping MMIO range is rejected");
    expect(router.register_device(&node_with_hole), "a node with a reserved subrange registers");
    expect(router.register_device(&hole_device), "a device can occupy another node's address hole");

    simrv::memory::TlChannelA request{};
    request.opcode = simrv::memory::TlOpcodeA::Get;
    request.address = 0x11FF;
    request.size = 1;  // two bytes, crossing the end of inner
    simrv::memory::TlChannelD response{};
    expect(router.route_request(request, response),
           "straddling request resolves to its first node");
    expect(response.error, "straddling MMIO request returns a bus error");
    expect(inner.requests() == 0, "straddling request is not delivered to the device");

    request.address = 0x1100;
    request.size = 0;
    request.opcode = simrv::memory::TlOpcodeA::ArithmeticData;
    response = {};
    expect(router.route_request(request, response), "unsupported operation resolves its address");
    expect(response.error, "unsupported MMIO operation returns a bus error");
    expect(inner.requests() == 0, "unsupported operation is not delivered to the device");
}

}  // namespace

int main() {
    test_unaligned_host_access();
    test_mmio_ranges();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "Core semantic tests passed\n";
    return EXIT_SUCCESS;
}
