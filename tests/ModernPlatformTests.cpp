/**
 * @file ModernPlatformTests.cpp
 * @brief Unit tests for RISC-V AIA, ACLINT, PCIe ECAM, and VirtIO-PCI subsystems.
 */
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CsrTypes.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/mmio/VirtioMmioBlock.hpp"
#include "simrv/device/mmio/VirtioMmioConsole.hpp"
#include "simrv/device/mmio/VirtioMmioNet.hpp"
#include "simrv/device/mmio/VirtioMmioRng.hpp"
#include "simrv/device/pci/PciDevice.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/pci/VirtioPciBlock.hpp"
#include "simrv/device/pci/VirtioPciConsole.hpp"
#include "simrv/device/pci/VirtioPciGpu.hpp"
#include "simrv/device/pci/VirtioPciInput.hpp"
#include "simrv/device/pci/VirtioPciNet.hpp"
#include "simrv/device/pci/VirtioPciRng.hpp"
#include "simrv/device/pci/VirtioPciSound.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/MmioDevice.hpp"
#include "simrv/xlen/Types.hpp"

namespace {

class DummyDevice : public simrv::memory::MmioDevice {
   public:
    explicit DummyDevice(simrv::core::Machine* m) : simrv::memory::MmioDevice(m) {}
    [[nodiscard]] auto name() const -> const char* override { return "dummy"; }
    [[nodiscard]] auto base_address() const -> Address override { return 0x1000U; }
    [[nodiscard]] auto size() const -> Address override { return 0x100U; }

    [[nodiscard]] auto read32(Address offset) -> uint32_t override {
        return static_cast<uint32_t>(0xABCD0000U + offset);
    }
    void write32(Address offset, uint32_t val) override {
        last_val_ = val + static_cast<uint32_t>(offset);
    }
    uint32_t last_val_{0};
};

class ConcreteMachine : public simrv::core::Machine {
   public:
    ConcreteMachine() = default;
    void execute_cycle() override {}
};

void test_mmio_device_and_dma() {
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.mmem = ram.data();
    machine.s_dram_size = ram.size();

    DummyDevice dev(&machine);
    assert(dev.base_address() == 0x1000U);
    assert(dev.read32(0x04) == 0xABCD0004U);
    assert(dev.read8(0x04) == 0x04U);
    assert(dev.read16(0x04) == 0x0004U);

    // Test DMA
    std::vector<uint8_t> src = {0x11, 0x22, 0x33, 0x44};
    assert(dev.dma_write(simrv::memory::kDramBaseAddress + 0x100, src));

    std::vector<uint8_t> dst(4, 0);
    assert(dev.dma_read(simrv::memory::kDramBaseAddress + 0x100, dst));
    assert(dst == src);
    std::cout << "[PASS] test_mmio_device_and_dma\n";
}

void test_aclint() {
    ConcreteMachine machine;
    machine.cpu.reset();

    simrv::device::AclintMtimer mtimer(&machine);
    simrv::device::AclintMswi mswi(&machine);

    // Initial state: no timer / software interrupts
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Mtip)) == 0);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Msip)) == 0);

    // MSWI test
    mswi.write32(0x00, 1);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Msip)) != 0);
    assert(mswi.read32(0x00) == 1);
    mswi.write32(0x00, 0);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Msip)) == 0);

    // MTIMER test: set mtimecmp = 100, mtime = 150
    mtimer.write64(0x4000, 100);
    mtimer.write64(0x7ff8, 150);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Mtip)) != 0);

    // Clear by setting mtimecmp higher
    mtimer.write64(0x4000, 200);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Mtip)) == 0);
    std::cout << "[PASS] test_aclint\n";
}

void test_aia_aplic_and_imsic() {
    ConcreteMachine machine;
    machine.cpu.reset();

    simrv::device::Imsic imsic_s(&machine, simrv::device::Imsic::Privilege::Supervisor,
                                 simrv::mmio::kImsicSBaseAddress, simrv::mmio::kImsicSSize);
    simrv::device::Aplic aplic_s(&machine, simrv::device::Aplic::Privilege::Supervisor,
                                 simrv::mmio::kAplicSBaseAddress, simrv::mmio::kAplicSSize,
                                 &imsic_s);

    // Direct mode interrupt testing
    aplic_s.write32(0x0000, 0x100);    // IE=1, DM=0 (Direct mode)
    aplic_s.write32(0x1e00, 1U << 1);  // Enable source 1
    aplic_s.set_irq(1, true);

    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Seip)) != 0);

    // MSI delivery via IMSIC testing
    imsic_s.csr_write(0, 0x70, 1);        // eidelivery = 1
    imsic_s.csr_write(0, 0xC0, 1U << 5);  // enable interrupt ID 5
    imsic_s.trigger_msi(0, 5);

    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Seip)) != 0);
    assert((imsic_s.csr_read(0, 0x44) & 0xffff) == 5);  // topei returns ID 5
    std::cout << "[PASS] test_aia_aplic_and_imsic\n";
}

void test_pcie_and_virtio_pci() {
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.mmem = ram.data();
    machine.s_dram_size = ram.size();

    simrv::device::PcieRootComplex rc(&machine);
    auto blk = std::make_shared<simrv::device::VirtioPciBlock>();
    rc.attach_device(0, 1, 0, blk);

    // Test ECAM discovery of device at Bus 0, Dev 1, Func 0
    // Offset in ECAM: (0 << 20) | (1 << 15) | (0 << 12) = 0x8000
    const uint32_t vendor_device = rc.ecam_read(0x8000 + 0x00, 4);
    assert((vendor_device & 0xffff) == 0x1AF4);  // VirtIO Vendor ID
    assert((vendor_device >> 16) == 0x1042);     // Modern VirtIO-PCI Block ID
    (void)vendor_device;

    // BAR 0 sizing and assignment
    rc.ecam_write(0x8000 + 0x10, 0xffffffffU, 4);
    const uint32_t bar_size_query = rc.ecam_read(0x8000 + 0x10, 4);
    assert((bar_size_query & ~0xfU) == ~(0x1000U - 1U));
    (void)bar_size_query;

    // Assign BAR 0 address in MMIO window: 0x40001000
    rc.ecam_write(0x8000 + 0x10, 0x40001000U, 4);

    // Attach Console (Slot 2), RNG (Slot 3), GPU (Slot 4), Input (Slot 5), Sound (Slot 6)
    auto console = std::make_shared<simrv::device::VirtioPciConsole>();
    auto rng = std::make_shared<simrv::device::VirtioPciRng>();
    auto gpu = std::make_shared<simrv::device::VirtioPciGpu>();
    auto input = std::make_shared<simrv::device::VirtioPciInput>();
    auto sound = std::make_shared<simrv::device::VirtioPciSound>();

    rc.attach_device(0, 2, 0, console);
    rc.attach_device(0, 3, 0, rng);
    rc.attach_device(0, 4, 0, gpu);
    rc.attach_device(0, 5, 0, input);
    rc.attach_device(0, 6, 0, sound);

    // Verify Console at 00:02.0 (ECAM offset 0x10000)
    const uint32_t console_id = rc.ecam_read((2 << 15) + 0x00, 4);
    assert((console_id & 0xffff) == 0x1AF4);
    assert((console_id >> 16) == 0x1043);
    (void)console_id;

    // Verify RNG at 00:03.0 (ECAM offset 0x18000)
    const uint32_t rng_id = rc.ecam_read((3 << 15) + 0x00, 4);
    assert((rng_id & 0xffff) == 0x1AF4);
    assert((rng_id >> 16) == 0x1044);
    (void)rng_id;

    // Verify GPU at 00:04.0 (ECAM offset 0x20000)
    const uint32_t gpu_id = rc.ecam_read((4 << 15) + 0x00, 4);
    assert((gpu_id & 0xffff) == 0x1AF4);
    assert((gpu_id >> 16) == 0x1050);
    (void)gpu_id;

    // Verify Input at 00:05.0 (ECAM offset 0x28000)
    const uint32_t input_id = rc.ecam_read((5 << 15) + 0x00, 4);
    assert((input_id & 0xffff) == 0x1AF4);
    assert((input_id >> 16) == 0x1052);
    (void)input_id;

    // Verify Sound at 00:06.0 (ECAM offset 0x30000)
    const uint32_t sound_id = rc.ecam_read((6 << 15) + 0x00, 4);
    assert((sound_id & 0xffff) == 0x1AF4);
    assert((sound_id >> 16) == 0x1059);
    (void)sound_id;

    // Attach & Verify Net at 00:07.0 (ECAM offset 0x38000)
    auto net = std::make_shared<simrv::device::VirtioPciNet>();
    rc.attach_device(0, 7, 0, net);
    const uint32_t net_id = rc.ecam_read((7 << 15) + 0x00, 4);
    assert((net_id & 0xffff) == 0x1AF4);
    assert((net_id >> 16) == 0x1041);
    (void)net_id;

    std::cout << "[PASS] test_pcie_and_virtio_pci\n";
}

void test_virtio_mmio_v2() {
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.mmem = ram.data();
    machine.s_dram_size = ram.size();

    simrv::device::VirtioMmioBlock blk(0x10001000, 2, &machine);
    simrv::device::VirtioMmioConsole con(0x10002000, 1, &machine);
    simrv::device::VirtioMmioRng rng(0x10003000, 4, &machine);
    simrv::device::VirtioMmioNet net(0x10007000, 8, &machine);

    // Test Magic (0x74726976) and Version 2
    assert(blk.read32(0x00) == 0x74726976);
    assert(blk.read32(0x04) == 2);
    assert(blk.read32(0x08) == 2);  // Block Device ID

    assert(con.read32(0x00) == 0x74726976);
    assert(con.read32(0x04) == 2);
    assert(con.read32(0x08) == 3);  // Console Device ID

    assert(rng.read32(0x00) == 0x74726976);
    assert(rng.read32(0x04) == 2);
    assert(rng.read32(0x08) == 4);  // RNG Device ID

    assert(net.read32(0x00) == 0x74726976);
    assert(net.read32(0x04) == 2);
    assert(net.read32(0x08) == 1);  // Net Device ID

    // Test Queue config and status transitions
    blk.write32(0x30, 0);            // queue_sel = 0
    assert(blk.read32(0x34) == 64);  // queue_num_max = 64
    blk.write32(0x38, 32);           // queue_num = 32
    blk.write32(0x80, 0x80001000);   // desc_lo
    blk.write32(0x84, 0);            // desc_hi
    blk.write32(0x90, 0x80002000);   // driver_lo
    blk.write32(0x94, 0);            // driver_hi
    blk.write32(0xA0, 0x80003000);   // device_lo
    blk.write32(0xA4, 0);            // device_hi
    blk.write32(0x44, 1);            // queue_ready = 1
    assert(blk.read32(0x44) == 1);

    const auto* q = blk.get_queue_state(0);
    assert(q != nullptr);
    assert(q->num == 32);
    assert(q->desc_addr == 0x80001000);
    assert(q->driver_addr == 0x80002000);
    assert(q->device_addr == 0x80003000);
    assert(q->ready == 1);
    (void)q;

    // Test Device Status
    blk.write32(0x70, 0x0F);  // DRIVER_OK
    assert(blk.device_status() == 0x0F);

    std::cout << "[PASS] test_virtio_mmio_v2\n";
}

}  // namespace

int main() {
    test_mmio_device_and_dma();
    test_aclint();
    test_aia_aplic_and_imsic();
    test_pcie_and_virtio_pci();
    test_virtio_mmio_v2();
    std::cout << "All Modern Platform tests PASSED!\n";
    return 0;
}
