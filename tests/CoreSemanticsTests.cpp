#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/core/RegisterFile.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/MmioRouter.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/memory/ReservationTable.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/util/FdtGenerator.hpp"

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

void test_physical_range_validation() {
    using simrv::memory::address_range_contains;
    constexpr Address kBase = 0x80000000;
    constexpr Address kSize = 0x10000000;
    expect(address_range_contains(kBase, kSize, kBase, 8),
           "a complete access at the DRAM base is valid");
    expect(address_range_contains(kBase, kSize, kBase + kSize - 8, 8),
           "an access ending exactly at the DRAM limit is valid");
    expect(!address_range_contains(kBase, kSize, kBase + kSize - 4, 8),
           "an access straddling the DRAM limit is rejected");
#if SIMRV_XLEN == 64
    expect(!address_range_contains(kBase, kSize, UINT64_C(0x180000000), 1),
           "RV64 physical addresses do not alias DRAM through their low 32 bits");
#endif
}

void test_csr_summary_and_presence_rules() {
    using simrv::core::kMstatusSd;
    using simrv::core::MipBit;
    using simrv::core::mstatus_read_value;
    using simrv::core::mstatus_with_sd;
    using simrv::core::MstatusBit;
    using simrv::core::pmp_csr_exists;

    expect((mstatus_with_sd(enum_mask(MstatusBit::Vs)) & kMstatusSd) != 0,
           "mstatus.SD summarizes Dirty vector state");
    expect((mstatus_with_sd(static_cast<CSRValue>(2) << 9U) & kMstatusSd) == 0,
           "mstatus.SD remains clear for Clean vector state");
#if SIMRV_XLEN == 64
    {
        const CSRValue rv32_status =
            mstatus_read_value(enum_mask(MstatusBit::Vs), simrv::core::kMstatusReadMask, 32);
        expect(
            (rv32_status & (CSRValue{1} << 31U)) != 0 && (rv32_status & (CSRValue{1} << 63U)) == 0,
            "an RV64-hosted RV32 status exposes SD at architectural bit 31");
        const CSRValue sign_extended_rv32_cause = ~CSRValue{0} << 31U;
        const CSRValue internal_cause =
            simrv::core::cause_write_value(sign_extended_rv32_cause, 32);
        expect(internal_cause == (CSRValue{1} << 63U),
               "RV32 cause writes discard sign-extension bits before translating interrupt");
    }
#endif
    expect(pmp_csr_exists(0x3A1, 32), "RV32 defines odd-numbered pmpcfg CSRs");
    expect(!pmp_csr_exists(0x3A1, 64), "RV64 reserves odd-numbered pmpcfg CSR addresses");
    expect(pmp_csr_exists(0x3A2, 64), "RV64 defines even-numbered pmpcfg CSRs");
    expect(simrv::core::is_debug_csr(0x7A0),
           "debug-only CSR encodings are identified independently of M-mode privilege");

    const CSRValue m_only = simrv::core::mstatus_writable_mask(false, false, false, false);
    expect((m_only &
            (enum_mask(MstatusBit::Sie) | enum_mask(MstatusBit::Spie) | enum_mask(MstatusBit::Spp) |
             enum_mask(MstatusBit::Mprv) | enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr) |
             enum_mask(MstatusBit::Tvm) | enum_mask(MstatusBit::Tw) | enum_mask(MstatusBit::Tsr) |
             enum_mask(MstatusBit::Fs) | enum_mask(MstatusBit::Vs))) == 0,
           "M-only profiles expose lower-mode and absent extension status as read-only zero");
    expect((simrv::core::mstatus_writable_mask(true, true, true, true) &
            (enum_mask(MstatusBit::Tvm) | enum_mask(MstatusBit::Tw) | enum_mask(MstatusBit::Tsr) |
             enum_mask(MstatusBit::Fs) | enum_mask(MstatusBit::Vs))) != 0,
           "S/U/F/V profiles retain their applicable status controls");
    expect(
        ((simrv::core::mstatus_legalize_mpp(CSRValue{1U} << 11U, false, true) >> 11U) & 0x3U) == 3U,
        "MPP cannot select an unimplemented supervisor mode");
    expect(((simrv::core::mstatus_legalize_mpp(0, false, false) >> 11U) & 0x3U) == 3U,
           "MPP cannot select unimplemented user mode");
    expect(simrv::core::least_supported_mpp(true, true) == 0U &&
               simrv::core::least_supported_mpp(true, false) == 1U &&
               simrv::core::least_supported_mpp(false, false) == 3U,
           "xRET resets MPP to the least implemented privilege mode");
    expect((simrv::core::mip_writable_mask(true) & enum_mask(MipBit::Msip)) == 0 &&
               (simrv::core::mip_writable_mask(true) & enum_mask(MipBit::Mtip)) == 0 &&
               (simrv::core::mip_writable_mask(true) & enum_mask(MipBit::Meip)) == 0,
           "CLINT/PLIC machine pending bits are read-only in mip");
    expect(simrv::core::mip_writable_mask(true) ==
               (enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip) | enum_mask(MipBit::Seip)),
           "M-mode can post the standard supervisor pending interrupts without Sstc");
    expect(simrv::core::interrupt_implemented_mask(false) ==
               (enum_mask(MipBit::Msip) | enum_mask(MipBit::Mtip) | enum_mask(MipBit::Meip)),
           "M-only profiles hardwire supervisor interrupt bits to zero");
    expect(
        (simrv::core::mip_rmw_base(enum_mask(MipBit::Seip), false) & enum_mask(MipBit::Seip)) == 0,
        "an external SEIP signal is excluded from the mip CSR RMW write base");
    expect((simrv::core::mip_rmw_base(0, true) & enum_mask(MipBit::Seip)) != 0,
           "the software SEIP latch participates in the mip CSR RMW write base");
    simrv::core::ArchState pending_state;
    pending_state.seip_external = true;
    pending_state.stip_software = true;
    pending_state.refresh_supervisor_pending();
    expect((pending_state.mip & enum_mask(MipBit::Seip)) != 0 &&
               (pending_state.mip & enum_mask(MipBit::Stip)) != 0,
           "independent external SEIP and software STIP sources become visible in mip");
    pending_state.seip_external = false;
    pending_state.stip_timer = true;
    pending_state.refresh_supervisor_pending();
    expect((pending_state.mip & enum_mask(MipBit::Seip)) == 0 &&
               (pending_state.mip & enum_mask(MipBit::Stip)) != 0,
           "clearing one interrupt source preserves independently asserted pending state");
}

void test_sbi_single_hart_masks() {
    using simrv::sbi::detail::HartMaskSelection;
    using simrv::sbi::detail::select_local_hart;

    expect(select_local_hart(0, 0, 0) == HartMaskSelection::Empty,
           "an empty SBI hart mask selects no hart");
    expect(select_local_hart(1, 0, 0) == HartMaskSelection::Local,
           "bit zero selects local hart zero");
    expect(select_local_hart(static_cast<Word>(0x55), static_cast<Word>(-1), 0) ==
               HartMaskSelection::Local,
           "an all-ones SBI hart-mask base broadcasts regardless of mask");
    expect(select_local_hart(2, 0, 0) == HartMaskSelection::Invalid,
           "a mask naming an unavailable hart is invalid");
    expect(select_local_hart(3, 0, 0) == HartMaskSelection::Invalid,
           "a mask mixing local and unavailable harts is invalid");
    expect(simrv::sbi::detail::is_direct_sbi_ecall(enum_mask(ExceptionCode::SupervisorEcall)),
           "direct SBI accepts supervisor ECALLs");
    expect(!simrv::sbi::detail::is_direct_sbi_ecall(enum_mask(ExceptionCode::MachineEcall)),
           "direct SBI leaves machine ECALLs to the architectural trap handler");
}

void test_vector_length_bytes() {
    simrv::core::RegisterFile regs;
    for (const unsigned vlen : {32U, 64U, 128U, 256U, 512U, 1024U}) {
        regs.vlen = vlen;
        expect(regs.vlen_bytes() == vlen / 8,
               "vlenb reflects the configured architectural vector length");
        expect(regs.vstart_mask() == static_cast<CSRValue>(vlen - 1),
               "vstart represents every index below maximum VLMAX");
    }
}

void test_exception_delegation_mask() {
    const CSRValue mask = simrv::core::kMedelegWritableMask;
    expect((mask & (static_cast<CSRValue>(1) << enum_mask(ExceptionCode::UserEcall))) != 0,
           "U-mode ECALL is delegatable to S-mode");
    expect((mask & (static_cast<CSRValue>(1) << enum_mask(ExceptionCode::SupervisorEcall))) == 0,
           "S-mode ECALL is not exposed as delegatable because it cannot originate below S-mode");
    expect((mask & (static_cast<CSRValue>(1) << enum_mask(ExceptionCode::MachineEcall))) == 0,
           "M-mode ECALL is never delegatable");
    expect((mask & (static_cast<CSRValue>(1) << enum_mask(ExceptionCode::HypervisorEcall))) == 0,
           "hypervisor ECALL is not exposed without H support");
    expect((mask & (static_cast<CSRValue>(1) << 14U)) == 0,
           "reserved exception cause 14 is not delegatable");
}

void test_satp_modes() {
    using simrv::xlen::satp_mode_supported;
    expect(satp_mode_supported(0, 32), "RV32 accepts Bare address translation");
    expect(satp_mode_supported(1, 32), "RV32 accepts Sv32 address translation");
    if constexpr (simrv::xlen::kIsXLen64) {
        constexpr Word kRv32Satp = static_cast<Word>(uint64_t{1} << 31U) | 0x123U;
        expect(simrv::xlen::satp_translation_enabled(kRv32Satp, 32),
               "an RV64 build recognizes Sv32 from the active RV32 SATP layout");
        expect(!simrv::xlen::satp_translation_enabled(kRv32Satp, 64),
               "the same bits remain Bare under the RV64 SATP layout");
        expect(satp_mode_supported(0, 64), "RV64 accepts Bare address translation");
        expect(!satp_mode_supported(1, 64), "RV64 rejects reserved satp MODE=1");
        expect(!satp_mode_supported(9, 64), "RV64 rejects unsupported Sv48 address translation");
    }
}

void test_named_misa_profiles() {
    const CSRValue gc = simrv::isa::misa_profile_bits(simrv::isa::MisaProfile::GC);
    const CSRValue gcbv = simrv::isa::misa_profile_bits(simrv::isa::MisaProfile::GCBV);
    expect(simrv::isa::misa_has_extension(gc, simrv::isa::IsaExtension::C),
           "the named GC profile includes compressed instructions");
    expect(!simrv::isa::misa_has_extension(gc, simrv::isa::IsaExtension::B) &&
               !simrv::isa::misa_has_extension(gc, simrv::isa::IsaExtension::V),
           "the named GC profile does not silently enable B or V");
    expect(simrv::isa::misa_has_extension(gcbv, simrv::isa::IsaExtension::B) &&
               simrv::isa::misa_has_extension(gcbv, simrv::isa::IsaExtension::V),
           "the explicit GCBV profile enables B and V");
}

void test_sv32_page_walk() {
    std::array<Byte, 0x6000> ram{};
    constexpr Address kRoot = 0x1000;
    constexpr Address kNext = 0x2000;
    constexpr Address kPhysical = 0x3000;
    constexpr Address kVirtual = 0x1234;
    constexpr Word kSatp = (static_cast<Word>(1) << 31U) | (kRoot >> 12U);
    constexpr Word kValid = enum_mask(simrv::PteFlag::V);
    constexpr Word kLeafFlags = kValid | enum_mask(simrv::PteFlag::R) |
                                enum_mask(simrv::PteFlag::W) | enum_mask(simrv::PteFlag::A) |
                                enum_mask(simrv::PteFlag::D);

    simrv::memory::ram_write_fast(kRoot, ((kNext >> 12U) << 10U) | kValid,
                                  static_cast<Instruction>(simrv::isa::Funct3::Sw), ram.data());
    simrv::memory::ram_write_fast(kNext + 4, ((kPhysical >> 12U) << 10U) | kLeafFlags,
                                  static_cast<Instruction>(simrv::isa::Funct3::Sw), ram.data());
    simrv::Mmu mmu(ram.data(), 0, ram.size());
    const auto translated =
        mmu.translate(kVirtual, simrv::PteAccess::Read, kPrivSupervisor, 0, kSatp, 32, false);
    expect(translated.has_value() && *translated == 0x3234,
           "Sv32 walks a two-level page table and preserves the page offset");

    constexpr Word kReservedNonLeaf = enum_mask(simrv::PteFlag::D);
    simrv::memory::ram_write_fast(kRoot, ((kNext >> 12U) << 10U) | kValid | kReservedNonLeaf,
                                  static_cast<Instruction>(simrv::isa::Funct3::Sw), ram.data());
    const auto malformed =
        mmu.translate(kVirtual, simrv::PteAccess::Read, kPrivSupervisor, 0, kSatp, 32, false);
    expect(!malformed.has_value() && malformed.error() == enum_mask(ExceptionCode::LoadPageFault),
           "Sv32 faults when a non-leaf PTE sets a reserved D bit");

    constexpr Word kOutsideRamSatp =
        (static_cast<Word>(1) << 31U) | (static_cast<Word>(ram.size()) >> 12U);
    const auto load_access_fault = mmu.translate(kVirtual, simrv::PteAccess::Read, kPrivSupervisor,
                                                 0, kOutsideRamSatp, 32, false);
    expect(!load_access_fault.has_value() &&
               load_access_fault.error() == enum_mask(ExceptionCode::FaultLoad),
           "Sv32 reports a load access fault when the implicit root-PTE read is outside RAM");
    const auto fetch_access_fault = mmu.translate(kVirtual, simrv::PteAccess::Code, kPrivSupervisor,
                                                  0, kOutsideRamSatp, 32, false);
    expect(!fetch_access_fault.has_value() &&
               fetch_access_fault.error() == enum_mask(ExceptionCode::FaultFetch),
           "Sv32 reports an instruction access fault for a failed fetch page-table walk");
}

void test_sv39_reserved_pte_bits() {
    if constexpr (simrv::xlen::kIsXLen64) {
        std::array<Byte, 0x7000> ram{};
        constexpr Address kRoot = 0x1000;
        constexpr Address kLevel1 = 0x2000;
        constexpr Address kLevel0 = 0x3000;
        constexpr Address kPhysical = 0x4000;
        constexpr Address kVirtual = 0x1234;
        constexpr Word kSatp = static_cast<Word>((uint64_t{8} << 60U) | (kRoot >> 12U));
        constexpr Word kValid = enum_mask(simrv::PteFlag::V);
        constexpr Word kLeafFlags =
            kValid | enum_mask(simrv::PteFlag::R) | enum_mask(simrv::PteFlag::A);
        const auto write_pte = [&ram](Address address, Word pte) {
            simrv::memory::ram_write_fast(
                address, pte, static_cast<Instruction>(simrv::isa::Funct3::Sd), ram.data());
        };

        write_pte(kRoot, ((kLevel1 >> 12U) << 10U) | kValid);
        write_pte(kLevel1, ((kLevel0 >> 12U) << 10U) | kValid);
        write_pte(kLevel0 + 8, ((kPhysical >> 12U) << 10U) | kLeafFlags);
        simrv::Mmu mmu(ram.data(), 0, ram.size());
        const auto translated =
            mmu.translate(kVirtual, simrv::PteAccess::Read, kPrivSupervisor, 0, kSatp, 64, false);
        expect(translated.has_value() && *translated == 0x4234,
               "Sv39 walks three levels and preserves the page offset");

        write_pte(kLevel0 + 8,
                  ((kPhysical >> 12U) << 10U) | kLeafFlags | static_cast<Word>(uint64_t{1} << 54U));
        const auto reserved =
            mmu.translate(kVirtual, simrv::PteAccess::Read, kPrivSupervisor, 0, kSatp, 64, false);
        expect(!reserved.has_value() && reserved.error() == enum_mask(ExceptionCode::LoadPageFault),
               "Sv39 faults on reserved PTE bits when Svnapot/Svpbmt are absent");
    }
}

void test_atomic_alignment() {
    using simrv::isa::amo_address_aligned;
    using simrv::isa::amo_width_supported;
    expect(amo_address_aligned(0x1004, simrv::isa::Funct3::Lw),
           "AMO.W accepts four-byte alignment");
    expect(!amo_address_aligned(0x1002, simrv::isa::Funct3::Lw),
           "AMO.W rejects a two-byte-aligned address");
    expect(amo_address_aligned(0x1008, simrv::isa::Funct3::Ld),
           "AMO.D accepts eight-byte alignment");
    expect(!amo_address_aligned(0x1004, simrv::isa::Funct3::Ld),
           "AMO.D rejects four-byte alignment");
    expect(amo_width_supported(simrv::isa::Funct3::Lw),
           "AMO.W exists for both architectural XLENs");
    expect(amo_width_supported(simrv::isa::Funct3::Ld) == simrv::xlen::kIsXLen64,
           "AMO.D exists only for RV64");
}

void test_atomic_decode_legality() {
    constexpr Instruction kAmoOpcode = 0x2FU;
    const auto encode_amo = [](simrv::isa::Funct5Amo funct5, unsigned width, unsigned rs2) {
        return (enum_mask(funct5) << 27U) | (rs2 << 20U) | (1U << 15U) | (width << 12U) |
               (2U << 7U) | kAmoOpcode;
    };

    expect(simrv::pipeline::decoder(encode_amo(simrv::isa::Funct5Amo::Lr, 2, 0)) ==
               simrv::isa::OperationId::LR_W,
           "LR.W with rs2=x0 is a legal encoding");
    expect(simrv::pipeline::decoder(encode_amo(simrv::isa::Funct5Amo::Lr, 2, 1)) ==
               simrv::isa::OperationId::UNKNOWN,
           "LR.W with nonzero rs2 is reserved");
    const auto decoded_lr_d = simrv::pipeline::decoder(encode_amo(simrv::isa::Funct5Amo::Lr, 3, 0));
    expect(decoded_lr_d == (simrv::xlen::kIsXLen64 ? simrv::isa::OperationId::LR_D
                                                   : simrv::isa::OperationId::UNKNOWN),
           "LR.D is legal only for RV64");
}

void test_fp_decode_legality() {
    constexpr Instruction kOpFp = 0x53U;
    const auto encode_op_fp = [](unsigned funct7, unsigned rm, unsigned rs2) {
        return (funct7 << 25U) | (rs2 << 20U) | (1U << 15U) | (rm << 12U) | (2U << 7U) | kOpFp;
    };
    const auto encode_fma = [](unsigned fmt) {
        constexpr Instruction kFmadd = 0x43U;
        return (3U << 27U) | (fmt << 25U) | (2U << 20U) | (1U << 15U) | (2U << 7U) | kFmadd;
    };

    expect(simrv::pipeline::decoder(encode_op_fp(0x2CU, 0, 0)) == simrv::isa::OperationId::FSQRT_S,
           "FSQRT.S requires and accepts rs2=x0");
    expect(simrv::pipeline::decoder(encode_op_fp(0x2CU, 0, 1)) == simrv::isa::OperationId::UNKNOWN,
           "FSQRT.S with nonzero rs2 is reserved");
    expect(simrv::pipeline::decoder(encode_op_fp(0x70U, 1, 1)) == simrv::isa::OperationId::UNKNOWN,
           "FCLASS.S with nonzero rs2 is reserved");
    expect(simrv::pipeline::decoder(encode_fma(2)) == simrv::isa::OperationId::UNKNOWN,
           "reserved fused-operation formats do not alias single precision");

    expect(simrv::pipeline::decoder(encode_op_fp(0x21U, 0, 0)) == simrv::isa::OperationId::FCVT_D_S,
           "FCVT.D.S accepts its specified rm=000 encoding");
    expect(simrv::pipeline::decoder(encode_op_fp(0x21U, 1, 0)) == simrv::isa::OperationId::UNKNOWN,
           "FCVT.D.S rejects a nonzero reserved rm field");
    expect(simrv::isa::required_extension_for_instruction(encode_op_fp(0x20U, 0, 1), false) ==
               simrv::isa::IsaExtension::D,
           "FCVT.S.D requires the D extension despite its single destination format");

    const auto fcvt_l_s = simrv::pipeline::decoder(encode_op_fp(0x60U, 0, 2));
    expect(fcvt_l_s == (simrv::xlen::kIsXLen64 ? simrv::isa::OperationId::FCVT_L_S
                                               : simrv::isa::OperationId::UNKNOWN),
           "64-bit integer FP conversions exist only on RV64");
}

void test_privileged_decode_legality() {
    constexpr Instruction kSystemOpcode = 0x73U;
    const auto encode_priv = [](unsigned funct12, unsigned rd, unsigned rs1) {
        return (funct12 << 20U) | (rs1 << 15U) | (rd << 7U) | kSystemOpcode;
    };

    expect(simrv::pipeline::decoder(encode_priv(0x302, 0, 0)) == simrv::isa::OperationId::MRET,
           "MRET accepts its canonical rd=x0, rs1=x0 encoding");
    expect(simrv::pipeline::decoder(encode_priv(0x302, 1, 0)) == simrv::isa::OperationId::UNKNOWN,
           "MRET rejects reserved nonzero rd");
    expect(simrv::pipeline::decoder(encode_priv(0x105, 0, 1)) == simrv::isa::OperationId::UNKNOWN,
           "WFI rejects reserved nonzero rs1");
    expect(simrv::pipeline::decoder(encode_priv(0x000, 1, 0)) == simrv::isa::OperationId::UNKNOWN,
           "ECALL rejects reserved nonzero rd");
    expect(simrv::pipeline::decoder(encode_priv(0x002, 0, 0)) == simrv::isa::OperationId::UNKNOWN,
           "legacy draft-N URET remains illegal when N is not implemented");

    constexpr Instruction kSfenceVma = (0x09U << 25U) | (2U << 20U) | (1U << 15U) | kSystemOpcode;
    expect(simrv::pipeline::decoder(kSfenceVma) == simrv::isa::OperationId::SFENCE_VMA,
           "SFENCE.VMA permits nonzero address and ASID operands");
    expect(simrv::pipeline::decoder(kSfenceVma | (1U << 7U)) == simrv::isa::OperationId::UNKNOWN,
           "SFENCE.VMA rejects reserved nonzero rd");
}

void test_csr_privilege_presence() {
    expect(!simrv::core::csr_access_permitted(kPrivMachine, false, false, 0x100, false),
           "M-mode cannot read an unimplemented supervisor CSR");
    expect(simrv::core::csr_access_permitted(kPrivMachine, true, true, 0x100, false),
           "M-mode can read an implemented supervisor CSR");
    expect(!simrv::core::csr_access_permitted(kPrivSupervisor, true, true, 0x300, false),
           "S-mode cannot access machine CSRs");
    expect(!simrv::core::csr_access_permitted(kPrivMachine, true, true, 0xC00, true),
           "read-only CSR encodings reject writes even from M-mode");
    expect(!simrv::core::csr_access_permitted(kPrivMachine, false, false, 0x302, false) &&
               !simrv::core::csr_access_permitted(kPrivMachine, false, false, 0x303, false),
           "trap delegation CSRs do not exist without S-mode");
    expect(!simrv::core::csr_access_permitted(kPrivMachine, false, false, 0x306, false),
           "mcounteren does not exist without U-mode");
    expect(simrv::core::is_zero_hpm_csr(0xB83, 32) && !simrv::core::is_zero_hpm_csr(0xB83, 64) &&
               simrv::core::is_zero_hpm_csr(0xB03, 64),
           "HPM high halves exist only for RV32 while low halves exist for both XLENs");
}

void test_interrupt_priority() {
    using simrv::core::MipBit;
    const Word all_standard = enum_mask(MipBit::Meip) | enum_mask(MipBit::Msip) |
                              enum_mask(MipBit::Mtip) | enum_mask(MipBit::Seip) |
                              enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip);
    expect(select_highest_priority_interrupt(all_standard) == 11U,
           "MEI has highest standard interrupt priority");
    expect(select_highest_priority_interrupt(all_standard & ~enum_mask(MipBit::Meip)) == 3U,
           "MSI precedes MTI and supervisor interrupts");
    expect(select_highest_priority_interrupt(enum_mask(MipBit::Seip) | enum_mask(MipBit::Ssip) |
                                             enum_mask(MipBit::Stip)) == 9U,
           "SEI precedes SSI and STI");
}

void test_epc_ialign_mask() {
    const CSRValue with_c = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::C);
    expect(simrv::isa::epc_read_value(0x103, with_c) == 0x102,
           "xepc masks bit zero when compressed instructions set IALIGN=16");
    expect(simrv::isa::epc_read_value(0x103, 0) == 0x100,
           "xepc masks bits one and zero when IALIGN=32");
}

void test_rv32_cause_translation() {
    constexpr CSRValue visible_interrupt = CSRValue{1} << 31U;
    const CSRValue internal = simrv::core::cause_write_value(visible_interrupt | 7U, 32);
    if constexpr (simrv::xlen::kIsXLen64) {
        expect((internal & static_cast<CSRValue>(uint64_t{1} << 63U)) != 0,
               "RV64 internal state relocates an RV32 architectural interrupt flag");
    } else {
        expect((internal & visible_interrupt) != 0,
               "native RV32 state preserves the architectural interrupt flag");
    }
    expect(simrv::core::cause_read_value(internal, 32) == (visible_interrupt | 7U),
           "RV32 cause write/read translation round-trips interrupt and cause code");
}

void test_lower_privilege_xlen_initialization() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::ArchState state;
        state.misa = static_cast<CSRValue>(uint64_t{1U} << 62U) |
                     simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::I) |
                     simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::S) |
                     simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::U);
        state.initialize_lower_xlen_fields();
        expect(((static_cast<uint64_t>(state.mstatus) >> 32U) & 0x3U) == 1U &&
                   ((static_cast<uint64_t>(state.mstatus) >> 34U) & 0x3U) == 1U,
               "an RV32 machine personality initializes UXL and SXL to RV32");
        state.priv = kPrivSupervisor;
        state.update_xlen();
        expect(state.regs.xlen == 32,
               "entering supervisor mode does not widen an RV32 machine personality");

        state.misa = static_cast<CSRValue>(uint64_t{2U} << 62U) |
                     simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::I) |
                     simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::S) |
                     simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::U);
        state.initialize_lower_xlen_fields();
        expect(((static_cast<uint64_t>(state.mstatus) >> 32U) & 0x3U) == 2U &&
                   ((static_cast<uint64_t>(state.mstatus) >> 34U) & 0x3U) == 2U,
               "an RV64 machine personality initializes UXL and SXL to RV64");

        state.mstatus = (state.mstatus & ~static_cast<CSRValue>(uint64_t{0x3U} << 34U)) |
                        static_cast<CSRValue>(uint64_t{1U} << 34U);
        expect(state.xlen_for_privilege(kPrivMachine) == 64 &&
                   state.xlen_for_privilege(kPrivSupervisor) == 32,
               "effective privilege can select an XLEN narrower than M-mode");

        state.misa = static_cast<CSRValue>(uint64_t{2U} << 62U) |
                     simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::I);
        state.initialize_lower_xlen_fields();
        expect(((static_cast<uint64_t>(state.mstatus) >> 32U) & 0xFU) == 0U,
               "M-only RV64 profiles expose absent UXL and SXL fields as read-only zero");
        expect(((state.mstatus & enum_mask(simrv::core::MstatusBit::Mpp)) >> 11U) == 3U,
               "M-only profiles retain a legal MPP value");
    }
}

void test_smp_reservation_table() {
    simrv::memory::ReservationTable table;

    table.set_reservation(0, 0x80001000);
    expect(table.check_reservation(0, 0x80001000), "Hart 0 holds reservation at 0x80001000");
    expect(table.check_reservation(0, 0x80001020),
           "Hart 0 holds reservation within same 64-byte granule");
    expect(!table.check_reservation(0, 0x80001040),
           "Hart 0 does not hold reservation in different granule");
    expect(!table.check_reservation(1, 0x80001000), "Hart 1 does not hold Hart 0's reservation");

    table.invalidate_matching(0x80001000, 0);
    expect(table.check_reservation(0, 0x80001000),
           "Store from Hart 0 does not invalidate its own reservation before SC");

    table.invalidate_matching(0x80001010, 1);
    expect(!table.check_reservation(0, 0x80001000),
           "Store from Hart 1 invalidates Hart 0's reservation");
    expect(!table.check_and_clear_reservation(0, 0x80001000),
           "SC on invalidated reservation fails");

    table.set_reservation(0, 0x80002000);
    table.set_reservation(1, 0x80003000);
    expect(table.check_and_clear_reservation(0, 0x80002000),
           "SC on valid reservation succeeds");
    expect(!table.check_reservation(0, 0x80002000),
           "Reservation is cleared after successful SC");
    expect(table.check_reservation(1, 0x80003000), "Hart 1's reservation remains intact");

    table.clear_all();
    expect(!table.check_reservation(1, 0x80003000),
           "clear_all invalidates all active reservations");
}

void test_dynamic_fdt_generator() {
    simrv::util::FdtConfig config{
        .num_harts = 4,
        .dram_base = 0x80000000,
        .dram_size = 512ULL * 1024ULL * 1024ULL,
        .xlen = 64,
    };
    auto fdt = simrv::util::FdtGenerator::generate(config);
    expect(fdt.size() > 40, "FDT binary is non-empty and has header");
    // Check FDT magic (0xd00dfeed)
    expect(fdt[0] == 0xd0 && fdt[1] == 0x0d && fdt[2] == 0xfe && fdt[3] == 0xed,
           "FDT magic matches 0xd00dfeed in big-endian");
}

void test_pmp_semantics() {
    simrv::core::ArchState state{};
    state.priv = PrivilegeLevel::Supervisor;

    // By default, no PMP entries are active, so S-mode accesses pass
    expect(simrv::core::pmp::check_access(state, 0x80000000, 4, simrv::core::PmpAccessType::Read),
           "Default empty PMP allows S-mode read access");

    // Configure PMP entry 0: NAPOT 64KB region at 0x80000000 with R/W (no X)
    // 64KB = 2^16 bytes. NAPOT encoding for 2^(t+3): 16 = t+3 -> t = 13 trailing ones in pmpaddr
    const uint64_t napot_64k = (0x80000000ULL >> 2) | ((1ULL << 13) - 1);
    state.pmpaddr[0] = static_cast<Address>(napot_64k);
    state.pmpcfg[0] = simrv::core::pmp::kPmpModeNapot | simrv::core::pmp::kPmpR |
                      simrv::core::pmp::kPmpW;

    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Read),
           "PMP entry 0 permits read within NAPOT range");
    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Write),
           "PMP entry 0 permits write within NAPOT range");
    expect(!simrv::core::pmp::check_access(state, 0x80001000, 4,
                                           simrv::core::PmpAccessType::Execute),
           "PMP entry 0 denies execute without X flag");
    expect(!simrv::core::pmp::check_access(state, 0x80020000, 4, simrv::core::PmpAccessType::Read),
           "PMP access outside configured regions is denied in S-mode");

    // M-mode accesses bypass unlocked PMP entries
    state.priv = PrivilegeLevel::Machine;
    expect(simrv::core::pmp::check_access(state, 0x80020000, 4, simrv::core::PmpAccessType::Read),
           "M-mode bypasses unlocked PMP entries");
}

void test_tilelink_c_coherence_semantics() {
    simrv::cache::DCache dcache;
    simrv::cache::ICache icache;

    std::array<Byte, simrv::cache::DCache::kLineBytes> sample_data{};
    sample_data[0] = static_cast<Byte>(0xAA);
    sample_data[1] = static_cast<Byte>(0xBB);
    sample_data[2] = static_cast<Byte>(0xCC);
    sample_data[3] = static_cast<Byte>(0xDD);

    const Address test_addr = 0x80002000;
    dcache.insert(test_addr, sample_data.data(), simrv::memory::CoherenceState::Branch, false);

    Word read_val = 0;
    expect(dcache.read(test_addr, read_val, 2), "D-Cache read hits in Branch state");
    expect((read_val & 0xFFFFFFFFU) == 0xDDCCBBAAU, "D-Cache read returned expected data");

    // Write in Branch state should return false (requiring AcquirePerm upgrade)
    expect(!dcache.write(test_addr, 0x12345678, 2), "D-Cache write misses in Branch state (upgrade required)");

    // Insert in Trunk state (exclusive/modified)
    dcache.insert(test_addr, sample_data.data(), simrv::memory::CoherenceState::Trunk, false);
    expect(dcache.write(test_addr, 0x12345678, 2), "D-Cache write hits in Trunk state");

    // Probe with invalidation (target: None)
    simrv::memory::TlChannelB probe_req{};
    probe_req.opcode = simrv::memory::TlOpcodeB::ProbeBlock;
    probe_req.param = static_cast<uint8_t>(simrv::memory::CoherenceState::None);
    probe_req.address = test_addr;

    simrv::memory::TlChannelC probe_resp{};
    std::array<Byte, simrv::cache::DCache::kLineBytes> dirty_buf{};
    expect(dcache.handle_probe(probe_req, probe_resp, dirty_buf), "D-Cache probe hits and handles request");
    expect(probe_resp.opcode == simrv::memory::TlOpcodeC::ProbeAckData, "Dirty line returns ProbeAckData");
    expect(!dcache.read(test_addr, read_val, 2), "D-Cache line is invalid after probe to None");

    // Test MESI Trunk -> Branch downgrade probe with dirty writeback
    dcache.insert(test_addr, sample_data.data(), simrv::memory::CoherenceState::Trunk, true);
    simrv::memory::TlChannelB downgrade_req{};
    downgrade_req.opcode = simrv::memory::TlOpcodeB::ProbeBlock;
    downgrade_req.param = static_cast<uint8_t>(simrv::memory::CoherenceState::Branch);
    downgrade_req.address = test_addr;

    simrv::memory::TlChannelC downgrade_resp{};
    std::array<Byte, simrv::cache::DCache::kLineBytes> dirty_wb{};
    expect(dcache.handle_probe(downgrade_req, downgrade_resp, dirty_wb), "D-Cache probe handles Branch downgrade");
    expect(downgrade_resp.opcode == simrv::memory::TlOpcodeC::ProbeAckData, "Dirty line returns ProbeAckData on downgrade");
    expect(dcache.read(test_addr, read_val, 2), "D-Cache line is still valid for reads in Branch state");

    // Test I-Cache probe invalidation
    icache.insert(test_addr, sample_data.data(), simrv::memory::CoherenceState::Branch, false);
    uint32_t inst_val = 0;
    expect(icache.read(test_addr, inst_val), "I-Cache read hits on inserted line");
    simrv::memory::TlChannelC ic_resp{};
    expect(icache.handle_probe(probe_req, ic_resp), "I-Cache probe hits and handles request");
    expect(!icache.read(test_addr, inst_val), "I-Cache line is invalid after probe to None");
}

}  // namespace

int main() {
    test_unaligned_host_access();
    test_mmio_ranges();
    test_physical_range_validation();
    test_csr_summary_and_presence_rules();
    test_sbi_single_hart_masks();
    test_vector_length_bytes();
    test_exception_delegation_mask();
    test_satp_modes();
    test_named_misa_profiles();
    test_sv32_page_walk();
    test_sv39_reserved_pte_bits();
    test_atomic_alignment();
    test_atomic_decode_legality();
    test_fp_decode_legality();
    test_privileged_decode_legality();
    test_csr_privilege_presence();
    test_interrupt_priority();
    test_epc_ialign_mask();
    test_rv32_cause_translation();
    test_lower_privilege_xlen_initialization();
    test_smp_reservation_table();
    test_dynamic_fdt_generator();
    test_pmp_semantics();
    test_tilelink_c_coherence_semantics();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "Core semantic tests passed\n";
    return EXIT_SUCCESS;
}
