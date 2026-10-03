#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "simrv/cache/L2Cache.hpp"
#include "simrv/cache/L3Cache.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/DecodeCache.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/core/RegisterFile.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/MmioRouter.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/memory/ReservationTable.hpp"
#include "simrv/memory/TileLinkProtocolChecker.hpp"
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
        resp.data = req.address.raw();
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

    bytes.fill(Byte{0xCC});
    simrv::memory::host_write_fast(bytes.data() + 1, static_cast<Register>(0xFEDCBA9876543210ULL),
                                   3);
    const size_t widest_store = simrv::xlen::kIsXLen64 ? 8 : 4;
    expect(bytes[1] == Byte{0x10} && bytes[2] == Byte{0x32} && bytes[3] == Byte{0x54} &&
               bytes[4] == Byte{0x76},
           "fast stores preserve little-endian byte order");
    expect(bytes[1 + widest_store] == Byte{0xCC},
           "width-3 fast stores do not write beyond the XLEN width");

    const std::array<Byte, 8> signed_values = {Byte{0x80}, Byte{0x00}, Byte{0x80}, Byte{0x00},
                                               Byte{0x00}, Byte{0x80}, Byte{0x00}, Byte{0x00}};
    expect(simrv::memory::host_read_fast(signed_values.data(), 0) ==
               static_cast<Word>(static_cast<SignedWord>(-128)),
           "LB sign-extends");
    expect(simrv::memory::host_read_fast(signed_values.data(), 4) == static_cast<Word>(0x80),
           "LBU zero-extends");
    expect(simrv::memory::host_read_fast(signed_values.data() + 1, 1) ==
               static_cast<Word>(static_cast<SignedWord>(-32768)),
           "LH sign-extends");
    expect(simrv::memory::host_read_fast(signed_values.data() + 1, 5) == static_cast<Word>(0x8000),
           "LHU zero-extends");
    const std::array<Byte, 4> signed_word = {Byte{0x00}, Byte{0x00}, Byte{0x00}, Byte{0x80}};
    expect(simrv::memory::host_read_fast(signed_word.data(), 2) ==
               static_cast<Word>(static_cast<SignedWord>(static_cast<int32_t>(0x80000000U))),
           "LW sign-extends");
    expect(simrv::memory::host_read_fast(signed_word.data(), 6) == static_cast<Word>(0x80000000U),
           "LWU preserves the unsigned word value");
}

void test_decode_cache_compact_round_robin() {
    simrv::core::DecodeCache cache;
    simrv::pipeline::DecodedInstruction decoded{};
    decoded.cpc = VirtAddr{0x80000000};
    decoded.imm = -16;
    decoded.ir = 0xFE208EE3;
    decoded.ir_org = decoded.ir;
    decoded.op_id = simrv::isa::BEQ;
    decoded.opcode = simrv::isa::Opcode::Branch;
    decoded.rd = static_cast<RegId>(3);
    decoded.rs1 = static_cast<RegId>(1);
    decoded.rs2 = static_cast<RegId>(2);
    decoded.funct3 = simrv::isa::Funct3::Beq;
    decoded.funct7 = static_cast<Funct7>((decoded.ir >> 25) & 0x7F);
    decoded.funct12 = static_cast<Funct12>(decoded.ir >> 20);
    decoded.funct5 = static_cast<simrv::isa::Funct5Amo>((decoded.ir >> 27) & 0x1F);

    simrv::core::CachedOp first{};
    first.copy_from(decoded);
    cache.insert(decoded.cpc, first);
    auto* hit = cache.lookup(decoded.cpc);
    expect(hit != nullptr && hit->len() == 4, "decode cache returns an inserted operation");
    simrv::pipeline::DecodedInstruction restored{};
    hit->copy_to(restored);
    expect(restored.cpc == decoded.cpc, "cpc match");
    expect(restored.imm == decoded.imm, "imm match");
    expect(restored.ir == decoded.ir, "ir match");
    expect(restored.ir_org == decoded.ir_org, "ir_org match");
    expect(restored.cinsn == decoded.cinsn, "cinsn match");
    expect(restored.op_id == decoded.op_id, "op_id match");
    expect(restored.opcode == decoded.opcode, "opcode match");
    expect(restored.rd == decoded.rd, "rd match");
    expect(restored.rs1 == decoded.rs1, "rs1 match");
    expect(restored.rs2 == decoded.rs2, "rs2 match");
    expect(restored.funct3 == decoded.funct3, "funct3 match");
    expect(restored.funct5 == decoded.funct5, "funct5 match");
    expect(restored.funct7 == decoded.funct7, "funct7 match");
    expect(restored.funct12 == decoded.funct12, "funct12 match");
    expect(restored.pending_tval == 0, "pending_tval match");
    expect(!restored.pending_exception.has_value(), "pending_exception match");

    const Register first_pc = decoded.cpc.raw();
    Register second_pc = first_pc + 2;
    while (simrv::core::DecodeCache::calc_set(second_pc) !=
           simrv::core::DecodeCache::calc_set(first_pc)) {
        second_pc += 2;
    }
    Register third_pc = second_pc + 2;
    while (simrv::core::DecodeCache::calc_set(third_pc) !=
           simrv::core::DecodeCache::calc_set(first_pc)) {
        third_pc += 2;
    }
    simrv::core::CachedOp compressed = first;
    compressed.cinsn = 1;
    cache.insert(second_pc, compressed);
    expect(cache.lookup(second_pc) != nullptr && cache.lookup(second_pc)->len() == 2,
           "decode cache records compressed instruction length");
    expect(cache.lookup(first_pc) != nullptr, "lookup hits do not disturb replacement state");
    cache.insert(third_pc, first);
    expect(cache.lookup(first_pc) == nullptr && cache.lookup(second_pc) != nullptr &&
               cache.lookup(third_pc) != nullptr,
           "full decode sets replace entries in insertion round-robin order");
    cache.flush();
    expect(cache.lookup(second_pc) == nullptr && cache.lookup(third_pc) == nullptr,
           "decode cache flush invalidates every way");

    static_assert(sizeof(simrv::core::CachedOp) <= (sizeof(Address) == 8 ? 32 : 24),
                  "CachedOp must maintain a 32-byte (RV64) / 24-byte (RV32) compact footprint");
    static_assert(sizeof(simrv::core::DecodeCache::CacheSet) <= 64,
                  "CacheSet must match single 64-byte host cache line");
    static_assert(sizeof(simrv::core::DecodeCache) <= 128 * 1024,
                  "DecodeCache total footprint must be 128KB");

    // Fast memory precomputed attributes
    simrv::pipeline::DecodedInstruction load_inst{};
    load_inst.traits.is_mem_load = true;
    load_inst.funct3 = simrv::isa::Funct3::Lw;
    simrv::core::CachedOp load_op{};
    load_op.copy_from(load_inst);
    expect(load_op.mem_class == simrv::core::FastMemClass::IntLoadSigned,
           "LW classified as IntLoadSigned");
    expect(load_op.mem_size == 4, "LW mem_size is 4");
    expect(load_op.align_mask() == 3, "LW align_mask is 3");

    simrv::pipeline::DecodedInstruction lbu_inst{};
    lbu_inst.traits.is_mem_load = true;
    lbu_inst.funct3 = simrv::isa::Funct3::Lbu;
    simrv::core::CachedOp lbu_op{};
    lbu_op.copy_from(lbu_inst);
    expect(lbu_op.mem_class == simrv::core::FastMemClass::IntLoadUnsigned,
           "LBU classified as IntLoadUnsigned");
    expect(lbu_op.mem_size == 1, "LBU mem_size is 1");
    expect(lbu_op.align_mask() == 0, "LBU align_mask is 0");

    simrv::pipeline::DecodedInstruction store_inst{};
    store_inst.traits.is_mem_store = true;
    store_inst.funct3 = simrv::isa::Funct3::Sd;
    simrv::core::CachedOp store_op{};
    store_op.copy_from(store_inst);
    expect(store_op.mem_class == simrv::core::FastMemClass::IntStore, "SD classified as IntStore");
    expect(store_op.mem_size == 8, "SD mem_size is 8");
    expect(store_op.align_mask() == 7, "SD align_mask is 7");

    simrv::pipeline::DecodedInstruction fld_inst{};
    fld_inst.traits.is_mem_load = true;
    fld_inst.traits.writes_fp = true;
    fld_inst.funct3 = simrv::isa::Funct3::Fld;
    simrv::core::CachedOp fld_op{};
    fld_op.copy_from(fld_inst);
    expect(fld_op.mem_class == simrv::core::FastMemClass::FpLoad, "FLD classified as FpLoad");
    expect(fld_op.mem_size == 8, "FLD mem_size is 8");
    expect(fld_op.align_mask() == 7, "FLD align_mask is 7");
}

void test_hosted_rv32_cached_shift_width() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::Machine machine;
        auto& cpu = machine.hart(HartId{0});
        auto& state = cpu.state();
        state.misa = (state.misa & ~(CSRValue{3} << 62U)) | (CSRValue{1} << 62U);
        state.initialize_lower_xlen_fields();
        expect(state.regs.xlen == 32, "RV64 build enters the RV32 guest personality");

        simrv::core::CachedOp shift{};
        shift.op_id = simrv::isa::SRL;
        shift.rd = RegId::A0;
        shift.rs1 = RegId::A1;
        shift.rs2 = RegId::A2;
        state.regs.write(RegId::A1, 0x80000000U);
        state.regs.write(RegId::A2, 33U);
        cpu.execute_cached_op_fast<false, false>(machine, shift);
        expect(state.regs.read(RegId::A0) == 0x40000000U,
               "hosted RV32 cached SRL truncates its operand and masks shift amounts to five bits");
    }
}

void test_compressed_instruction_decode_and_flush() {
    // 1. Decompression correctness for representative compressed instructions
    // C.ADDI4SPN: 0x0000 -> quadrant 0, op 0, nzuimm=0 -> illegal/reserved (returns 0)
    expect(simrv::pipeline::decompressInstruction(0x0000, true) == 0,
           "C.ADDI4SPN with imm=0 decompresses to 0 (illegal/reserved)");
    // C.NOP: 0x0001 -> addi x0, x0, 0 (0x00000013)
    expect(simrv::pipeline::decompressInstruction(0x0001, true) == 0x00000013,
           "C.NOP decompresses to canonical ADDI x0, x0, 0");
    // C.ADDI: addi a0, a0, 1 -> 0x0505
    const auto decomp_addi = simrv::pipeline::decompressInstruction(0x0505, true);
    expect((decomp_addi & 0x7F) == 0x13, "C.ADDI decompresses to an OP-IMM instruction");

    // 2. Decoder accessor c_rs1_p returns prime register
    simrv::pipeline::Decoder dec_c(0x0505);
    expect(dec_c.is_compressed(), "Decoder reports compressed instruction");
    expect(dec_c.c_rs1_p() == dec_c.c_rs1_rd_p(), "c_rs1_p() matches c_rs1_rd_p()");

    // 3. mstatus FS/VS modification flushes decode cache via CPU::TLB_flush()
    simrv::core::Machine machine;
    auto& cpu = machine.hart(HartId{0});
    auto& csr = cpu.csr_file;

    // Cache an operation at PC 0x1000
    simrv::pipeline::DecodedInstruction test_op{};
    test_op.cpc = VirtAddr{0x1000};
    test_op.ir = 0x00000013;
    test_op.ir_org = 0x0001;
    test_op.cinsn = 1;
    test_op.op_id = simrv::isa::ADDI;
    simrv::core::CachedOp cop{};
    cop.copy_from(test_op);
    cpu.decode_cache.insert(test_op.cpc, cop);
    expect(cpu.decode_cache.lookup(test_op.cpc) != nullptr, "cached op present before CSR write");

    // Write to mstatus altering FS field: must trigger TLB_flush which clears decode_cache
    const CSRValue curr_mstatus = cpu.state().mstatus;
    const CSRValue new_mstatus = curr_mstatus ^ enum_mask(simrv::core::MstatusBit::Fs);
    auto res = csr.write(csr_addr(simrv::core::Csr::Mstatus), new_mstatus);
    (void)res;
    expect(cpu.decode_cache.lookup(test_op.cpc) == nullptr,
           "mstatus FS toggle flushes decode cache to invalidate cached execution legality");
}

void test_selective_tlb_and_decode_cache_flush() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(HartId{0});

    // Setup entries for page A (0x1000) and page B (0x2000)
    constexpr Address kPageA = 0x1000;
    constexpr Address kPageB = 0x2000;
    const Address vpnA = kPageA >> 12;
    const Address vpnB = kPageB >> 12;
    const size_t idxA = simrv::core::CPU::soft_tlb_index(vpnA);
    const size_t idxB = simrv::core::CPU::soft_tlb_index(vpnB);

    cpu.soft_tlb_read[idxA].set(vpnA, 0, PrivilegeLevel::Supervisor, cpu.soft_tlb_epoch, kPageA,
                                nullptr);
    cpu.soft_tlb_write[idxA].set(vpnA, 0, PrivilegeLevel::Supervisor, cpu.soft_tlb_epoch, kPageA,
                                 nullptr);
    cpu.soft_tlb_inst[idxA].set(vpnA, 0, PrivilegeLevel::Supervisor, cpu.soft_tlb_epoch, kPageA,
                                nullptr);

    cpu.soft_tlb_read[idxB].set(vpnB, 0, PrivilegeLevel::Supervisor, cpu.soft_tlb_epoch, kPageB,
                                nullptr);
    cpu.soft_tlb_write[idxB].set(vpnB, 0, PrivilegeLevel::Supervisor, cpu.soft_tlb_epoch, kPageB,
                                 nullptr);
    cpu.soft_tlb_inst[idxB].set(vpnB, 0, PrivilegeLevel::Supervisor, cpu.soft_tlb_epoch, kPageB,
                                nullptr);

    simrv::core::CachedOp copA{};
    copA.cpc = VirtAddr{kPageA + 4};
    copA.op_id = simrv::isa::ADDI;
    copA.valid = true;
    cpu.decode_cache.insert(copA.cpc, copA);

    simrv::core::CachedOp copB{};
    copB.cpc = VirtAddr{kPageB + 4};
    copB.op_id = simrv::isa::ADDI;
    copB.valid = true;
    cpu.decode_cache.insert(copB.cpc, copB);

    expect(cpu.soft_tlb_read[idxA].valid(cpu.soft_tlb_epoch),
           "soft TLB read A is valid before flush");
    expect(cpu.soft_tlb_read[idxB].valid(cpu.soft_tlb_epoch),
           "soft TLB read B is valid before flush");
    expect(cpu.decode_cache.lookup(copA.cpc) != nullptr, "decode cache A is valid before flush");
    expect(cpu.decode_cache.lookup(copB.cpc) != nullptr, "decode cache B is valid before flush");

    // Perform selective flush for Page A (match_all_vaddr = false, match_all_asid = true)
    cpu.TLB_flush(false, kPageA, true, 0);

    // Page A should be invalidated in soft TLB and decode cache
    expect(!cpu.soft_tlb_read[idxA].valid(cpu.soft_tlb_epoch),
           "selective flush invalidates soft TLB read A");
    expect(!cpu.soft_tlb_write[idxA].valid(cpu.soft_tlb_epoch),
           "selective flush invalidates soft TLB write A");
    expect(!cpu.soft_tlb_inst[idxA].valid(cpu.soft_tlb_epoch),
           "selective flush invalidates soft TLB inst A");
    expect(cpu.decode_cache.lookup(copA.cpc) == nullptr,
           "selective flush invalidates decode cache for Page A");

    // Page B should STILL be valid!
    expect(cpu.soft_tlb_read[idxB].valid(cpu.soft_tlb_epoch),
           "selective flush preserves soft TLB read B");
    expect(cpu.soft_tlb_write[idxB].valid(cpu.soft_tlb_epoch),
           "selective flush preserves soft TLB write B");
    expect(cpu.soft_tlb_inst[idxB].valid(cpu.soft_tlb_epoch),
           "selective flush preserves soft TLB inst B");
    expect(cpu.decode_cache.lookup(copB.cpc) != nullptr,
           "selective flush preserves decode cache for Page B");

    // Perform global TLB flush
    cpu.TLB_flush();
    expect(!cpu.soft_tlb_read[idxB].valid(cpu.soft_tlb_epoch),
           "global flush invalidates soft TLB read B");
    expect(cpu.decode_cache.lookup(copB.cpc) == nullptr,
           "global flush invalidates decode cache for Page B");
}

void test_tlb_and_decode_cache_deduplication() {
    // 1. TLB insert in-place update and empty way preference
    simrv::core::Tlb tlb;
    tlb.flush();
    constexpr Address kVaddr = 0x80001000;
    constexpr Address kPaddr1 = 0x10000000;
    constexpr Address kPaddr2 = 0x20000000;
    const auto set = simrv::core::Tlb::calc_set(kVaddr);

    // Initial insert into empty set -> uses way 0
    tlb.insert_data_r(kVaddr, kPaddr1, 0, PrivilegeLevel::Supervisor);
    expect(tlb.data_r[set][0].valid && tlb.data_r[set][0].p_addr == kPaddr1,
           "initial TLB insert populates first way");
    expect(!tlb.data_r[set][1].valid, "second way remains empty");

    // Re-inserting the same vpage updates in place, preserving second way empty
    tlb.insert_data_r(kVaddr, kPaddr2, 0, PrivilegeLevel::Supervisor);
    expect(tlb.data_r[set][0].valid && tlb.data_r[set][0].p_addr == kPaddr2,
           "duplicate TLB insert updates in place without allocating new way");
    expect(!tlb.data_r[set][1].valid, "second way remains empty after re-insert");

    // 2. Decode cache in-place update
    simrv::core::DecodeCache decode_cache;
    decode_cache.flush();
    simrv::core::CachedOp op1{};
    op1.cpc = VirtAddr{kVaddr};
    op1.op_id = simrv::isa::ADDI;
    op1.imm = 10;
    decode_cache.insert(kVaddr, op1);

    auto* hit1 = decode_cache.lookup(kVaddr);
    expect(hit1 != nullptr && hit1->imm == 10, "decode cache lookup finds initial op");

    // Re-inserting the same PC updates the existing way without duplicating
    simrv::core::CachedOp op2 = op1;
    op2.imm = 20;
    decode_cache.insert(kVaddr, op2);

    auto* hit2 = decode_cache.lookup(kVaddr);
    expect(hit2 != nullptr && hit2->imm == 20, "decode cache re-insert updates in place");
}

void test_runtime_ram_view() {
    std::array<Byte, 32> bytes{};
    constexpr Address kUpperDramBase = simrv::memory::kDramBaseAddress + simrv::memory::kDramSize;
    const simrv::memory::RamView ram(bytes.data(), kUpperDramBase, bytes.size());
    const Address address = kUpperDramBase + 8;
    simrv::memory::ram_write_fast(address, static_cast<Word>(0xA1B2C3D4U),
                                  static_cast<Instruction>(simrv::isa::Funct3::Sw), ram);
    expect(simrv::memory::ram_read_fast(address, static_cast<Instruction>(simrv::isa::Funct3::Lwu),
                                        ram) == static_cast<Word>(0xA1B2C3D4U),
           "runtime RAM view maps addresses above the build-time DRAM extent without aliasing");
    expect(bytes[8] == Byte{0xD4}, "runtime RAM view uses the configured DRAM base as its offset");
    expect(!ram.contains(kUpperDramBase + bytes.size(), 1),
           "runtime RAM view rejects its end address");

    std::array<Byte, 8192> pages{};
    const simrv::memory::RamView page_view(pages.data(), kUpperDramBase, pages.size());
    expect(page_view.contains(kUpperDramBase + 4096, 4096),
           "runtime RAM view accepts a complete page above the legacy DRAM extent");
    expect(!page_view.contains(kUpperDramBase + 4097, 4096),
           "runtime RAM view rejects a page that would cross the configured DRAM end");
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
    expect(response.denied, "straddling MMIO request returns a denied response");
    expect(inner.requests() == 0, "straddling request is not delivered to the device");

    request.address = 0x1100;
    request.size = 0;
    request.opcode = simrv::memory::TlOpcodeA::ArithmeticData;
    response = {};
    expect(router.route_request(request, response), "unsupported operation resolves its address");
    expect(response.denied, "unsupported MMIO operation returns a denied response");
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

void test_sbi_hart_masks() {
    using simrv::sbi::detail::is_hart_selected;

    expect(!is_hart_selected(0, 0, 0), "an empty SBI hart mask selects no hart");
    expect(is_hart_selected(1, 0, 0), "bit zero selects hart zero");
    expect(!is_hart_selected(1, 0, 1), "bit zero does not select hart one");
    expect(is_hart_selected(static_cast<Word>(0x55), static_cast<Word>(-1), 0),
           "an all-ones SBI hart-mask base broadcasts to hart zero");
    expect(is_hart_selected(static_cast<Word>(0x55), static_cast<Word>(-1), 4),
           "an all-ones SBI hart-mask base broadcasts to any hart");
    expect(is_hart_selected(2, 0, 1), "bit one with base zero selects hart one");
    expect(is_hart_selected(0b11, 0, 0) && is_hart_selected(0b11, 0, 1),
           "a mask with multiple bits selects both harts");
    expect(is_hart_selected(1, 4, 4), "base 4 with bit 0 selects hart 4");
    expect(!is_hart_selected(1, 4, 3), "hart below base is not selected");
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

void test_vector_exception_propagation_and_status() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    cpu.state().misa = simrv::isa::kMisaDefault;
    cpu.state().mstatus = 0;
    cpu.state().vstart = 1;

    const Instruction ir = 0x00000057;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VREDSUM_VS, ir);
    expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction,
           "requires_zero_vstart raises IllegalInstruction on active context");
    expect(cpu.active_context().pending_tval == ir,
           "requires_zero_vstart reports instruction as pending_tval");

    cpu.active_context().pending_exception.reset();
    cpu.state().vstart = 0;
    cpu.state().mstatus = 0;
    constexpr Instruction kLegalVadd = (1U << 25U) | (1U << 7U) | 0x57U;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VADD_VV, kLegalVadd);
    expect((cpu.state().mstatus & enum_mask(simrv::core::MstatusBit::Vs)) != 0,
           "successful vector instruction sets mstatus.VS Dirty");

    constexpr Instruction kUnhandledVector = 0x02000057;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::UNKNOWN, kUnhandledVector);
    expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction,
           "an unhandled vector operation traps instead of retiring as a no-op");
    expect(cpu.active_context().pending_tval == kUnhandledVector,
           "an unhandled vector operation reports the faulting instruction");

    cpu.active_context().pending_exception.reset();
    cpu.state().vtype = static_cast<CSRValue>(1) << (cpu.state().regs.xlen - 1U);
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VADD_VV, ir);
    expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction,
           "non-configuration vector instructions trap while vtype.vill is set");
}

void test_vector_compute_register_group_legality() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    cpu.state().misa = simrv::isa::kMisaDefault;
    cpu.state().vl = 0;

    const auto instruction = [](RegId destination, RegId source1, RegId source2) {
        return static_cast<Instruction>((1U << 25U) | (std::to_underlying(source2) << 20U) |
                                        (std::to_underlying(source1) << 15U) |
                                        (std::to_underlying(destination) << 7U) | 0x57U);
    };
    const auto vreg = [](uint32_t index) { return static_cast<RegId>(index); };
    const auto is_illegal = [&](simrv::isa::OperationId op_id, RegId destination, RegId source1,
                                RegId source2) {
        cpu.active_context().pending_exception.reset();
        const Instruction ir = instruction(destination, source1, source2);
        simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(), op_id, ir);
        return cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction;
    };

    cpu.state().vtype = 3;  // e8, m8
    expect(is_illegal(simrv::isa::OperationId::VWADD_VV, vreg(0), vreg(16), vreg(8)),
           "widening arithmetic rejects destination EMUL=16");

    cpu.state().vtype = 1;  // e8, m2
    expect(is_illegal(simrv::isa::OperationId::VADD_VV, vreg(3), vreg(8), vreg(10)),
           "ordinary vector results require LMUL-aligned destinations");
    expect(is_illegal(simrv::isa::OperationId::VADD_VV, vreg(4), vreg(9), vreg(10)),
           "ordinary vector operations require LMUL-aligned vs1 groups");
    expect(is_illegal(simrv::isa::OperationId::VADD_VV, vreg(4), vreg(8), vreg(11)),
           "ordinary vector operations require LMUL-aligned vs2 groups");
    expect(!is_illegal(simrv::isa::OperationId::VADD_VV, vreg(4), vreg(8), vreg(10)),
           "ordinary vector operations accept aligned groups");
    expect(!is_illegal(simrv::isa::OperationId::VMV_S_X, vreg(3), vreg(1), vreg(0)),
           "scalar element moves can name an odd vector register at LMUL=2");
    expect(is_illegal(simrv::isa::OperationId::VREDSUM_VS, vreg(3), vreg(5), vreg(7)),
           "a reduction still requires an LMUL-aligned vs2 vector group");
    expect(!is_illegal(simrv::isa::OperationId::VREDSUM_VS, vreg(3), vreg(5), vreg(8)),
           "reduction scalar result and seed can use odd registers at LMUL=2");
    expect(is_illegal(simrv::isa::OperationId::VWREDSUM_VS, vreg(3), vreg(8), vreg(8)),
           "widening reductions cannot read one register at two element widths");
    expect(!is_illegal(simrv::isa::OperationId::VWREDSUM_VS, vreg(3), vreg(5), vreg(8)),
           "widening reductions accept independent scalar and vector inputs");
    expect(is_illegal(simrv::isa::OperationId::VMSEQ_VV, vreg(3), vreg(8), vreg(9)),
           "mask compares require an LMUL-aligned vs2 data source");
    expect(is_illegal(simrv::isa::OperationId::VMSEQ_VV, vreg(3), vreg(9), vreg(8)),
           "mask compares require an LMUL-aligned vs1 data source");
    expect(!is_illegal(simrv::isa::OperationId::VMSEQ_VV, vreg(3), vreg(6), vreg(8)),
           "mask compare destinations remain single registers at LMUL=2");
    expect(is_illegal(simrv::isa::OperationId::VMSEQ_VV, vreg(9), vreg(6), vreg(8)),
           "mask destination cannot overlap a non-low part of a data source group");
    expect(!is_illegal(simrv::isa::OperationId::VMSEQ_VV, vreg(8), vreg(6), vreg(8)),
           "mask destination may overlap the low register of a data source group");
    expect(!is_illegal(simrv::isa::OperationId::VMAND_MM, vreg(3), vreg(5), vreg(7)),
           "mask-logical operands ignore LMUL alignment");
    expect(is_illegal(simrv::isa::OperationId::VMSBF_M, vreg(3), vreg(0), vreg(3)),
           "mask prefix destination cannot overlap its source mask");
    expect(is_illegal(simrv::isa::OperationId::VIOTA_M, vreg(3), vreg(0), vreg(8)),
           "viota data destination requires LMUL alignment");
    expect(is_illegal(simrv::isa::OperationId::VIOTA_M, vreg(8), vreg(0), vreg(9)),
           "viota data destination cannot overlap its source mask");
    expect(is_illegal(simrv::isa::OperationId::VWADD_VV, vreg(2), vreg(12), vreg(8)),
           "widening arithmetic requires the widened destination alignment");
    expect(!is_illegal(simrv::isa::OperationId::VWADD_VV, vreg(4), vreg(12), vreg(8)),
           "aligned widening arithmetic groups are accepted");

    cpu.state().vtype = 0;  // e8, m1
    expect(!is_illegal(simrv::isa::OperationId::VWADD_VV, vreg(0), vreg(4), vreg(1)),
           "widening destination may overlap the high end of a source group");
    expect(is_illegal(simrv::isa::OperationId::VWADD_VV, vreg(0), vreg(4), vreg(0)),
           "widening destination may not overlap the low end of a source group");
    expect(!is_illegal(simrv::isa::OperationId::VNSRL_WX, vreg(2), vreg(4), vreg(2)),
           "narrowing destination may overlap the low end of its wide source");
    expect(is_illegal(simrv::isa::OperationId::VNSRL_WX, vreg(3), vreg(4), vreg(2)),
           "narrowing destination may not overlap the high end of its wide source");
    expect(is_illegal(simrv::isa::OperationId::VWMACC_VV, vreg(0), vreg(4), vreg(1)),
           "widening accumulates reject reading overlapping sources at different EEWs");

    cpu.state().vtype = (2U << 3U) | 2U;  // e32, m4
    expect(!is_illegal(simrv::isa::OperationId::VZEXT_VF2, vreg(4), vreg(0), vreg(6)),
           "extension source may overlap the high end of its destination group");
    expect(is_illegal(simrv::isa::OperationId::VZEXT_VF2, vreg(4), vreg(0), vreg(4)),
           "extension source may not overlap the low end of its destination group");

    cpu.state().vtype = 0;  // e8, m1
    expect(is_illegal(simrv::isa::OperationId::VSEXT_VF2, vreg(2), vreg(0), vreg(4)),
           "extension rejects source EEW below the architectural minimum");

    cpu.state().vtype = 3U << 3U;  // e64, m1
    expect(is_illegal(simrv::isa::OperationId::VNCLIP_WX, vreg(2), vreg(0), vreg(4)),
           "narrowing rejects a 128-bit wide source on an ELEN=64 implementation");

    cpu.state().vtype = 1;  // e8, m2
    expect(is_illegal(simrv::isa::OperationId::VSLIDEUP_VI, vreg(4), vreg(1), vreg(4)),
           "slide-up rejects overlapping LMUL-sized destination and source groups");
    expect(!is_illegal(simrv::isa::OperationId::VSLIDEUP_VI, vreg(4), vreg(1), vreg(8)),
           "slide-up accepts aligned non-overlapping groups");
    expect(is_illegal(simrv::isa::OperationId::VRGATHER_VV, vreg(4), vreg(8), vreg(4)),
           "gather rejects overlap with its data source group");
    expect(is_illegal(simrv::isa::OperationId::VCOMPRESS_VM, vreg(4), vreg(5), vreg(8)),
           "compress rejects destination overlap with its mask source");

    cpu.state().vtype = 3;  // e8, m8
    expect(is_illegal(simrv::isa::OperationId::VRGATHEREI16_VV, vreg(0), vreg(16), vreg(8)),
           "vrgatherei16 rejects an index EMUL greater than eight registers");

    cpu.state().vtype = 0;  // e8; whole-register moves ignore LMUL.
    expect(is_illegal(simrv::isa::OperationId::VMV4R_V, vreg(2), vreg(0), vreg(8)),
           "whole-register moves require destination alignment to NREG");
    expect(is_illegal(simrv::isa::OperationId::VMV4R_V, vreg(4), vreg(0), vreg(6)),
           "whole-register moves require source alignment to NREG");
    expect(!is_illegal(simrv::isa::OperationId::VMV4R_V, vreg(4), vreg(0), vreg(8)),
           "whole-register moves accept aligned source and destination groups");
    cpu.state().vstart = 4U * cpu.state().regs.vlen / 8U;
    expect(is_illegal(simrv::isa::OperationId::VMV4R_V, vreg(4), vreg(0), vreg(8)),
           "whole-register moves reject vstart at or beyond their effective length");
    cpu.state().vtype = 3U << 3U;  // e64, m1: VLMAX=VLEN/64.
    cpu.state().vstart = cpu.state().regs.vlen / 64U;
    expect(is_illegal(simrv::isa::OperationId::VADD_VV, vreg(4), vreg(8), vreg(12)) &&
               cpu.state().vstart == cpu.state().regs.vlen / 64U,
           "ordinary vector instructions reject vstart outside the current VLMAX");
    expect(!is_illegal(simrv::isa::OperationId::VMV4R_V, vreg(4), vreg(0), vreg(8)),
           "whole-register moves use their own effective length when vstart exceeds VLMAX");
    cpu.state().vtype = 0;
    cpu.state().vstart = 0;

    const Instruction masked_v0 = instruction(vreg(0), vreg(4), vreg(2)) & ~(1U << 25U);
    cpu.active_context().pending_exception.reset();
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VADD_VV, masked_v0);
    expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction,
           "a masked vector result may not overlap its v0 predicate source");

    cpu.active_context().pending_exception.reset();
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VMSEQ_VV, masked_v0);
    expect(!cpu.active_context().pending_exception.has_value(),
           "a mask-producing instruction may write v0 while masked");

    cpu.active_context().pending_exception.reset();
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VREDSUM_VS, masked_v0);
    expect(!cpu.active_context().pending_exception.has_value(),
           "a reduction scalar result may write v0 while masked");

    cpu.active_context().pending_exception.reset();
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VLE8_V, masked_v0);
    expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction,
           "a masked vector load may not overwrite its v0 predicate source");
}

void test_vector_mask_and_fault_only_first_memory() {
    simrv::core::Machine machine;
    std::array<Byte, 4096> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    const auto ram = machine.ram_view();
    constexpr auto vd = static_cast<RegId>(4);
    constexpr auto base_reg = static_cast<RegId>(5);
    const auto memory_ir = [](RegId vector_reg, RegId rs1) {
        return static_cast<Instruction>((std::to_underlying(vector_reg) << 7U) |
                                        (std::to_underlying(rs1) << 15U) | (1U << 25U));
    };

    cpu.state().vl = 10;
    regs.write(base_reg, ram.base());
    ram.data()[0] = Byte{0xA5};
    ram.data()[1] = Byte{0x03};
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VLM_V, memory_ir(vd, base_reg));
    expect(regs.read_vector(vd).u8[0] == 0xA5 && regs.read_vector(vd).u8[1] == 0x03,
           "vlm.v transfers ceil(vl/8) packed mask bytes");

    regs.write(base_reg, ram.base() + 2);
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VSM_V, memory_ir(vd, base_reg));
    expect(ram.data()[2] == Byte{0xA5} && ram.data()[3] == Byte{0x03},
           "vsm.v stores ceil(vl/8) packed mask bytes");

    constexpr auto stride_reg = static_cast<RegId>(6);
    constexpr auto index_reg = static_cast<RegId>(12);
    const auto segment_ir = [](RegId vector_reg, RegId rs1, RegId rs2) {
        return static_cast<Instruction>(
            (1U << 29U) | (1U << 25U) | (std::to_underlying(rs2) << 20U) |
            (std::to_underlying(rs1) << 15U) | (std::to_underlying(vector_reg) << 7U));
    };
    cpu.state().vl = 2;
    regs.write(base_reg, ram.base() + 16);
    regs.write(stride_reg, 4);
    backing[16] = Byte{0x10};
    backing[17] = Byte{0x11};
    backing[20] = Byte{0x20};
    backing[21] = Byte{0x21};
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VLSE8_V,
                                                segment_ir(vd, base_reg, stride_reg));
    expect(regs.read_vector(vd).u8[0] == 0x10 && regs.read_vector(vd).u8[1] == 0x20 &&
               regs.read_vector(static_cast<RegId>(5)).u8[0] == 0x11 &&
               regs.read_vector(static_cast<RegId>(5)).u8[1] == 0x21,
           "two-field strided segment loads populate consecutive vector registers");

    regs.read_vector(index_reg).u8[0] = 0;
    regs.read_vector(index_reg).u8[1] = 4;
    backing[16] = Byte{0x30};
    backing[17] = Byte{0x31};
    backing[20] = Byte{0x40};
    backing[21] = Byte{0x41};
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VLUXEI8_V,
                                                segment_ir(vd, base_reg, index_reg));
    expect(regs.read_vector(vd).u8[0] == 0x30 && regs.read_vector(vd).u8[1] == 0x40 &&
               regs.read_vector(static_cast<RegId>(5)).u8[0] == 0x31 &&
               regs.read_vector(static_cast<RegId>(5)).u8[1] == 0x41,
           "two-field indexed segment loads populate consecutive vector registers");

    cpu.state().vtype = 1;  // SEW=8, LMUL=2: each segment field occupies two registers.
    backing[16] = Byte{0x50};
    backing[17] = Byte{0x51};
    backing[20] = Byte{0x60};
    backing[21] = Byte{0x61};
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VLSE8_V,
                                                segment_ir(vd, base_reg, stride_reg));
    expect(regs.read_vector(vd).u8[0] == 0x50 && regs.read_vector(vd).u8[1] == 0x60 &&
               regs.read_vector(static_cast<RegId>(6)).u8[0] == 0x51 &&
               regs.read_vector(static_cast<RegId>(6)).u8[1] == 0x61,
           "segment fields advance by EMUL registers when LMUL is greater than one");

    const auto expect_illegal_memory = [&](simrv::isa::OperationId operation, Instruction ir,
                                           CSRValue vtype, const char* message) {
        cpu.active_context().pending_exception.reset();
        cpu.state().vtype = vtype;
        simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(), operation, ir);
        expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction,
               message);
    };
    expect_illegal_memory(simrv::isa::OperationId::VLE8_V,
                          memory_ir(static_cast<RegId>(3), base_reg), 1,
                          "LMUL=2 memory groups require even base vector registers");
    expect_illegal_memory(simrv::isa::OperationId::VLE64_V, memory_ir(vd, base_reg),
                          (2U << 3U) | 3U,
                          "memory instructions reject an EEW-derived EMUL greater than eight");
    expect_illegal_memory(simrv::isa::OperationId::VLSE8_V,
                          segment_ir(static_cast<RegId>(28), base_reg, stride_reg) | (2U << 29U), 1,
                          "segment memory groups cannot extend beyond vector register 31");
    expect_illegal_memory(
        simrv::isa::OperationId::VLUXEI32_V,
        memory_ir(vd, base_reg) | (std::to_underlying(static_cast<RegId>(2)) << 20U), 0,
        "indexed memory offsets obey their EEW-derived EMUL alignment");
    expect_illegal_memory(simrv::isa::OperationId::VLUXEI8_V,
                          segment_ir(vd, base_reg, static_cast<RegId>(5)), 0,
                          "indexed segment-load destinations cannot overlap their index group");
    cpu.state().vstart = cpu.state().regs.vlen / 64U;
    expect_illegal_memory(
        simrv::isa::OperationId::VL1RE64_V, memory_ir(vd, base_reg), 0,
        "whole-register loads reject vstart at their EEW-derived effective length");
    expect(cpu.state().vstart == cpu.state().regs.vlen / 64U,
           "an illegal whole-register load preserves vstart");
    cpu.state().vstart = cpu.state().regs.vlen / 8U;
    expect_illegal_memory(simrv::isa::OperationId::VS1R_V, memory_ir(vd, base_reg), 0,
                          "whole-register stores reject vstart at their byte effective length");
    cpu.active_context().pending_exception.reset();
    cpu.state().vstart = cpu.state().regs.vlen / 64U - 1U;
    regs.read_vector(vd).u8[0] = 0xD5;
    regs.read_vector(vd).u8[24] = 0;
    backing[40] = Byte{0xA7};
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VL1RE64_V, memory_ir(vd, base_reg));
    expect(!cpu.active_context().pending_exception.has_value() && cpu.state().vstart == 0 &&
               regs.read_vector(vd).u8[0] == 0xD5 && regs.read_vector(vd).u8[24] == 0xA7,
           "whole-register loads resume at the final EEW element without changing prestart bytes");
    cpu.state().vstart = 1;
    regs.write(base_reg, ram.base());
    regs.read_vector(vd).u8[0] = 0xD5;
    regs.read_vector(vd).u8[1] = 0xD6;
    ram.data()[2] = Byte{0x31};
    ram.data()[3] = Byte{0x42};
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VL1RE16_V, memory_ir(vd, base_reg));
    expect(!cpu.active_context().pending_exception.has_value() && cpu.state().vstart == 0 &&
               regs.read_vector(vd).u8[0] == 0xD5 && regs.read_vector(vd).u8[1] == 0xD6 &&
               regs.read_vector(vd).u8[2] == 0x31 && regs.read_vector(vd).u8[3] == 0x42,
           "whole-register EEW=16 loads resume at a halfword boundary");
    cpu.state().vtype = 3U << 3U;  // e64, m1: whole EEW=8 load has a larger effective length.
    cpu.state().vstart = cpu.state().regs.vlen / 64U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VL1RE8_V, memory_ir(vd, base_reg));
    expect(!cpu.active_context().pending_exception.has_value() && cpu.state().vstart == 0,
           "whole-register loads can start beyond current VLMAX when below their own EVL");
    cpu.state().vtype = static_cast<CSRValue>(1) << (cpu.state().regs.xlen - 1U);
    cpu.state().vl = 0;
    cpu.state().vstart = 0;
    regs.write(base_reg, ram.base());
    ram.data()[0] = Byte{0xC7};
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VL1RE8_V, memory_ir(vd, base_reg));
    expect(
        !cpu.active_context().pending_exception.has_value() && regs.read_vector(vd).u8[0] == 0xC7,
        "whole-register loads work with vill set and vl zero");
    regs.write(base_reg, ram.base() + 64);
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VS1R_V, memory_ir(vd, base_reg));
    expect(!cpu.active_context().pending_exception.has_value() && ram.data()[64] == Byte{0xC7},
           "whole-register stores work with vill set and vl zero");
    cpu.state().vtype = 0;
    if constexpr (!simrv::xlen::kIsXLen64) {
        const Instruction indexed64_ir =
            memory_ir(vd, base_reg) | (std::to_underlying(static_cast<RegId>(2)) << 20U);
        expect_illegal_memory(simrv::isa::OperationId::VLUXEI64_V, indexed64_ir, 3U << 3U,
                              "RV32 V rejects 64-bit indexed vector loads");
        expect_illegal_memory(simrv::isa::OperationId::VSUXEI64_V, indexed64_ir, 3U << 3U,
                              "RV32 V rejects 64-bit indexed vector stores");
    }

    cpu.active_context().pending_exception.reset();
    cpu.state().vtype = 0;

    const Address final_two_bytes = ram.base() + ram.size() - 2;
    ram.data()[ram.size() - 2] = Byte{0x11};
    ram.data()[ram.size() - 1] = Byte{0x22};
    regs.write(base_reg, final_two_bytes);
    cpu.state().vl = 4;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VLE8FF_V, memory_ir(vd, base_reg));
    expect(!cpu.active_context().pending_exception.has_value() && cpu.state().vl == 2 &&
               regs.read_vector(vd).u8[0] == 0x11 && regs.read_vector(vd).u8[1] == 0x22,
           "vle8ff.v suppresses a noninitial fault and trims vl to the loaded prefix");

    if constexpr (!simrv::xlen::kIsXLen64) {
        const Address unmapped = ram.base() + ram.size();
        regs.write(base_reg, unmapped);
        cpu.state().vtype = 3U << 3U;  // SEW=64
        cpu.state().vl = 1;
        cpu.state().vstart = 0;
        simrv::execute::ExecuteUnit::execute_vector(
            cpu, machine.memory(), simrv::isa::OperationId::VLE64_V, memory_ir(vd, base_reg));
        expect(cpu.active_context().pending_exception.has_value() &&
                   cpu.active_context().pending_tval == unmapped && cpu.state().vstart == 0,
               "RV32 vle64 reports the first faulting 32-bit subaccess");

        cpu.active_context().pending_exception.reset();
        cpu.active_context().pending_tval = 0;
        simrv::execute::ExecuteUnit::execute_vector(
            cpu, machine.memory(), simrv::isa::OperationId::VSE64_V, memory_ir(vd, base_reg));
        expect(cpu.active_context().pending_exception.has_value() &&
                   cpu.active_context().pending_tval == unmapped && cpu.state().vstart == 0,
               "RV32 vse64 reports the first faulting 32-bit subaccess");
    }
}

void test_vector_bulk_memory_operations() {
    simrv::core::Machine machine;
    std::array<Byte, 8192> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    const auto ram = machine.ram_view();

    constexpr auto vd = static_cast<RegId>(2);
    constexpr auto vs = static_cast<RegId>(4);
    constexpr auto base_reg = static_cast<RegId>(10);
    const auto memory_ir = [](RegId vector_reg, RegId rs1) {
        return static_cast<Instruction>((std::to_underlying(rs1) << 15U) |
                                        (std::to_underlying(vector_reg) << 7U) | (1U << 25U));
    };

    // 1. Bulk vle8 / vse8 within a single page
    cpu.state().vtype = 0;  // SEW=8, LMUL=1
    cpu.state().vl = 16;
    cpu.state().vstart = 0;
    regs.write(base_reg, ram.base() + 0x100);
    for (uint32_t i = 0; i < 16; ++i) {
        ram.data()[0x100 + i] = Byte{static_cast<uint8_t>(0xA0 + i)};
    }
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VLE8_V, memory_ir(vd, base_reg));
    for (uint32_t i = 0; i < 16; ++i) {
        expect(regs.read_vector(vd).u8[i] == static_cast<uint8_t>(0xA0 + i),
               "bulk vle8 transfers elements accurately");
    }

    // Modify vector register vd and write back via bulk vse8
    for (uint32_t i = 0; i < 16; ++i) {
        regs.read_vector(vd).u8[i] = static_cast<uint8_t>(0x50 + i);
    }
    regs.write(base_reg, ram.base() + 0x200);
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VSE8_V, memory_ir(vd, base_reg));
    for (uint32_t i = 0; i < 16; ++i) {
        expect(ram.data()[0x200 + i] == Byte{static_cast<uint8_t>(0x50 + i)},
               "bulk vse8 stores elements accurately to RAM");
    }

    // 2. Cross-page fallback test: buffer spanning 4096-byte boundary (e.g. 0xFF8..0x1008)
    regs.write(base_reg, ram.base() + 0xFF8);
    for (uint32_t i = 0; i < 16; ++i) {
        ram.data()[0xFF8 + i] = Byte{static_cast<uint8_t>(0x70 + i)};
    }
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VLE8_V, memory_ir(vs, base_reg));
    for (uint32_t i = 0; i < 16; ++i) {
        expect(regs.read_vector(vs).u8[i] == static_cast<uint8_t>(0x70 + i),
               "page-crossing vle8 falls back seamlessly and transfers correctly");
    }

    // 3. Whole-register load/store bulk test
    regs.write(base_reg, ram.base() + 0x300);
    for (uint32_t i = 0; i < regs.vlen_bytes(); ++i) {
        ram.data()[0x300 + i] = Byte{static_cast<uint8_t>(0x10 + (i & 0x7F))};
    }
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VL1RE8_V, memory_ir(vd, base_reg));
    for (uint32_t i = 0; i < regs.vlen_bytes(); ++i) {
        expect(regs.read_vector(vd).u8[i] == static_cast<uint8_t>(0x10 + (i & 0x7F)),
               "bulk vl1re8 accurately transfers whole register");
    }
}

void test_vector_integer_remainder_and_reverse_subtract() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    constexpr auto vd = static_cast<RegId>(1);
    constexpr auto vs2 = static_cast<RegId>(2);
    constexpr auto vs1 = static_cast<RegId>(3);
    const auto vector_ir = [](RegId rd, RegId rs1, RegId rs2) {
        return static_cast<Instruction>((std::to_underlying(rd) << 7U) |
                                        (std::to_underlying(rs1) << 15U) |
                                        (std::to_underlying(rs2) << 20U) | (1U << 25U));
    };

    cpu.state().vstart = 0;
    cpu.state().vl = 2;
    cpu.state().vtype = 0;
    regs.write(vs1, 10);
    regs.read_vector(vs2).u8[0] = 3;
    regs.read_vector(vs2).u8[1] = 12;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VRSUB_VX, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 7 && regs.read_vector(vd).u8[1] == 254,
           "vrsub.vx computes scalar minus vector elements with SEW wrapping");

    regs.read_vector(vs2).u8[0] = 0xF9;  // -7
    regs.read_vector(vs2).u8[1] = 0x80;  // signed minimum
    regs.read_vector(vs2).u8[2] = 9;
    regs.read_vector(vs1).u8[0] = 3;
    regs.read_vector(vs1).u8[1] = 0xFF;  // -1
    regs.read_vector(vs1).u8[2] = 0;
    cpu.state().vl = 3;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VREM_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 0xFF,
           "vrem preserves the dividend sign for a negative remainder");
    expect(regs.read_vector(vd).u8[1] == 0,
           "vrem returns zero for signed minimum divided by negative one");
    expect(regs.read_vector(vd).u8[2] == 9, "vrem returns the dividend on division by zero");

    regs.read_vector(vs2).u8[0] = 250;
    regs.read_vector(vs2).u8[1] = 128;
    regs.read_vector(vs2).u8[2] = 9;
    regs.read_vector(vs1).u8[0] = 7;
    regs.read_vector(vs1).u8[1] = 255;
    regs.read_vector(vs1).u8[2] = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VREMU_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 5 && regs.read_vector(vd).u8[1] == 128,
           "vremu computes unsigned element remainders");
    expect(regs.read_vector(vd).u8[2] == 9, "vremu returns the dividend on division by zero");

    cpu.state().vl = 1;
    cpu.state().vtype = 3U << 3U;  // SEW=64
    regs.read_vector(vs2).u64[0] = std::numeric_limits<uint64_t>::max();
    regs.read_vector(vs1).u64[0] = 2;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VMULHU_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == 1,
           "vmulhu returns the high half of a 64-bit unsigned product");

    regs.read_vector(vs2).u64[0] = static_cast<uint64_t>(-2);
    regs.read_vector(vs1).u64[0] = 3;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VMULH_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == std::numeric_limits<uint64_t>::max(),
           "vmulh returns the sign-extended high half of a signed product");

    regs.read_vector(vs2).u64[0] = static_cast<uint64_t>(-2);
    regs.read_vector(vs1).u64[0] = std::numeric_limits<uint64_t>::max();
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VMULHSU_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == std::numeric_limits<uint64_t>::max() - 1,
           "vmulhsu returns the high half of a signed-by-unsigned 64-bit product");

    cpu.state().vtype = 0;  // SEW=8
    regs.read_vector(vd).u8[0] = 5;
    regs.read_vector(vs1).u8[0] = 3;
    regs.read_vector(vs2).u8[0] = 20;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VNMSUB_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 5,
           "vnmsub.vv computes vs2 minus the product of vs1 and the old destination");

    cpu.state().vl = 2;
    regs.read_vector(vs1).u8[0] = 0xF0;
    regs.read_vector(vs2).u8[0] = 0x0F;
    regs.read_vector(vs2).u8[1] = 0xAA;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VREDXOR_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 0x55,
           "vredxor folds active elements into the scalar seed");

    regs.read_vector(vs1).u8[0] = 10;
    regs.read_vector(vs2).u8[0] = static_cast<uint8_t>(-5);
    regs.read_vector(vs2).u8[1] = 20;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VREDMIN_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == static_cast<uint8_t>(-5),
           "vredmin compares reduction elements as signed SEW values");

    regs.read_vector(vs1).u8[0] = 1;
    regs.read_vector(vs2).u8[0] = 250;
    regs.read_vector(vs2).u8[1] = 20;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VREDMAXU_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 250,
           "vredmaxu compares reduction elements as unsigned SEW values");
}

void test_vector_gather() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    constexpr auto vd = static_cast<RegId>(4);
    constexpr auto vs2 = static_cast<RegId>(2);
    constexpr auto index = static_cast<RegId>(3);
    const auto vector_ir = [](RegId rd, RegId rs1, RegId rs2) {
        return static_cast<Instruction>((std::to_underlying(rd) << 7U) |
                                        (std::to_underlying(rs1) << 15U) |
                                        (std::to_underlying(rs2) << 20U) | (1U << 25U));
    };

    regs.vlen = 128;
    cpu.state().vstart = 0;
    cpu.state().vl = 4;
    cpu.state().vtype = 0;  // SEW=8, LMUL=1, VLMAX=VLEN/8.
    for (uint32_t i = 0; i < regs.vlen_bytes(); ++i) {
        regs.read_vector(vs2).u8[i] = static_cast<uint8_t>(0x40U + i);
    }
    regs.read_vector(index).u8[0] = 3;
    regs.read_vector(index).u8[1] = 2;
    regs.read_vector(index).u8[2] = 1;
    regs.read_vector(index).u8[3] = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VRGATHER_VV, vector_ir(vd, index, vs2));
    expect(regs.read_vector(vd).u8[0] == 0x43 && regs.read_vector(vd).u8[1] == 0x42 &&
               regs.read_vector(vd).u8[2] == 0x41 && regs.read_vector(vd).u8[3] == 0x40,
           "vrgather.vv reads an independent index group");

    constexpr auto vd_e16 = static_cast<RegId>(4);
    constexpr auto indices_e16 = static_cast<RegId>(6);
    regs.read_vector(indices_e16).u16[0] = 1;
    regs.read_vector(indices_e16).u16[1] = 15;
    regs.read_vector(indices_e16).u16[2] = 16;
    regs.read_vector(indices_e16).u16[3] = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VRGATHEREI16_VV,
                                                vector_ir(vd_e16, indices_e16, vs2));
    expect(regs.read_vector(vd_e16).u8[0] == 0x41 && regs.read_vector(vd_e16).u8[1] == 0x4F &&
               regs.read_vector(vd_e16).u8[2] == 0 && regs.read_vector(vd_e16).u8[3] == 0x40,
           "vrgatherei16.vv uses 16-bit indices and zeros indices outside VLMAX");

    constexpr auto vd_imm = static_cast<RegId>(6);
    constexpr auto uimm31 = static_cast<RegId>(31);
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VRGATHER_VI,
                                                vector_ir(vd_imm, uimm31, vs2));
    expect(regs.read_vector(vd_imm).u8[0] == 0 && regs.read_vector(vd_imm).u8[3] == 0,
           "vrgather.vi treats its five-bit immediate as unsigned and applies the VLMAX bound");

    constexpr auto slide_offset = static_cast<RegId>(5);
    regs.write(slide_offset, 6);
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VSLIDEDOWN_VX,
                                                vector_ir(vd, slide_offset, vs2));
    expect(regs.read_vector(vd).u8[0] == 0x46 && regs.read_vector(vd).u8[3] == 0x49,
           "vslidedown.vx reads source elements beyond vl but below VLMAX");

    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VSLIDEDOWN_VI,
                                                vector_ir(vd, static_cast<RegId>(15), vs2));
    expect(regs.read_vector(vd).u8[0] == 0x4F && regs.read_vector(vd).u8[1] == 0 &&
               regs.read_vector(vd).u8[3] == 0,
           "vslidedown.vi zeros source indices at or beyond VLMAX");

    if (sizeof(Register) > 4) {
        regs.write(slide_offset, static_cast<Register>(uint64_t{1} << 32U));
        regs.read_vector(vd).u8[0] = 0xA5;
        simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                    simrv::isa::OperationId::VSLIDEDOWN_VX,
                                                    vector_ir(vd, slide_offset, vs2));
        expect(regs.read_vector(vd).u8[0] == 0,
               "vslidedown.vx does not truncate an RV64 offset to 32 bits");
        regs.read_vector(vd).u8[0] = 0xA5;
        simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                    simrv::isa::OperationId::VSLIDEUP_VX,
                                                    vector_ir(vd, slide_offset, vs2));
        expect(regs.read_vector(vd).u8[0] == 0xA5,
               "vslideup.vx does not truncate an RV64 offset to 32 bits");
    }
}

void test_vector_configuration_large_avl() {
    if constexpr (!simrv::xlen::kIsXLen64) return;

    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    constexpr auto rd = static_cast<RegId>(1);
    constexpr auto avl_reg = static_cast<RegId>(2);
    constexpr auto vtype_reg = static_cast<RegId>(3);
    const Instruction ir = (std::to_underlying(rd) << 7U) | (std::to_underlying(avl_reg) << 15U);

    regs.vlen = 128;
    regs.write(avl_reg, static_cast<Register>((uint64_t{1} << 32U) + 3U));
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VSETVLI, ir);
    expect(cpu.state().vl == 16 && regs.read(rd) == 16,
           "vsetvli uses the full RV64 AVL before limiting it to VLMAX");

    regs.write(vtype_reg, 0);
    const Instruction register_vtype_ir = ir | (std::to_underlying(vtype_reg) << 20U);
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VSETVL, register_vtype_ir);
    expect(cpu.state().vl == 16 && regs.read(rd) == 16,
           "vsetvl uses the full RV64 AVL before limiting it to VLMAX");
}

void test_vector_configuration_preserved_vl() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    cpu.state().vtype = 0;  // e8, m1
    cpu.state().vl = 5;
    cpu.state().vstart = 2;

    const Instruction same_vlmax = (1U << 20U) | (1U << 23U);  // e16, m2
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VSETVLI, same_vlmax);
    expect(!cpu.active_context().pending_exception.has_value() && cpu.state().vtype == 9 &&
               cpu.state().vl == 5 && cpu.state().vstart == 0,
           "vsetvli x0, x0 preserves vl when the SEW/LMUL ratio keeps VLMAX unchanged");

    cpu.state().vstart = 2;
    const Instruction changed_vlmax = 1U << 23U;  // e16, m1
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VSETVLI, changed_vlmax);
    expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction &&
               cpu.state().vtype == 9 && cpu.state().vl == 5 && cpu.state().vstart == 2,
           "vsetvli x0, x0 rejects a changed VLMAX without changing vector state");
}

void test_vector_scalar_move_element_zero() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    constexpr auto vd = static_cast<RegId>(4);
    constexpr auto scalar = static_cast<RegId>(2);
    const Instruction ir =
        (std::to_underlying(vd) << 7U) | (std::to_underlying(scalar) << 15U) | (1U << 25U);

    cpu.state().vtype = 3U << 3U;  // SEW=64
    regs.write(scalar, std::numeric_limits<Register>::max());
    regs.read_vector(vd).u64[0] = 0x123456789ABCDEF0ULL;
    cpu.state().vl = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VMV_S_X, ir);
    expect(regs.read_vector(vd).u64[0] == 0x123456789ABCDEF0ULL,
           "vmv.s.x leaves element zero unchanged when vl is zero");

    cpu.state().vl = 2;
    cpu.state().vstart = 1;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VMV_S_X, ir);
    expect(regs.read_vector(vd).u64[0] == 0x123456789ABCDEF0ULL,
           "vmv.s.x leaves prestart element zero unchanged");

    cpu.state().vstart = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VMV_S_X, ir);
    expect(regs.read_vector(vd).u64[0] == std::numeric_limits<uint64_t>::max(),
           "vmv.s.x sign-extends an RV32 scalar to SEW=64");

    cpu.state().misa = simrv::isa::kMisaDefault;
    cpu.state().mstatus |= enum_mask(simrv::core::MstatusBit::Fs);
    regs.write_fp(scalar, 0x1122334455667788ULL);
    regs.read_vector(vd).u64[0] = 0x123456789ABCDEF0ULL;
    cpu.state().vl = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFMV_S_F, ir);
    expect(regs.read_vector(vd).u64[0] == 0x123456789ABCDEF0ULL,
           "vfmv.s.f leaves element zero unchanged when vl is zero");
    cpu.state().vl = 1;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFMV_S_F, ir);
    expect(regs.read_vector(vd).u64[0] == 0x1122334455667788ULL,
           "vfmv.s.f transfers the scalar when element zero is active");

    cpu.state().vtype = 2U << 3U;                  // SEW=32
    regs.write_fp(scalar, 0x000000003F800000ULL);  // Unboxed 1.0f.
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFMV_S_F, ir);
    expect(regs.read_vector(vd).u32[0] == simrv::xlen::kF32Qnan,
           "vfmv.s.f substitutes canonical NaN for an unboxed scalar");

    cpu.state().misa &= ~simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::D);
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFMV_S_F, ir);
    expect(regs.read_vector(vd).u32[0] == 0x3F800000U,
           "vfmv.s.f does not require NaN boxing when FLEN is 32");
}

void test_vector_scalar_extension() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    constexpr auto vd = static_cast<RegId>(4);
    constexpr auto vs2 = static_cast<RegId>(6);
    constexpr auto scalar = static_cast<RegId>(2);
    const auto vector_ir = [](RegId rd, RegId rs1, RegId rs2, bool vm = true) {
        return static_cast<Instruction>(
            (std::to_underlying(rd) << 7U) | (std::to_underlying(rs1) << 15U) |
            (std::to_underlying(rs2) << 20U) | (static_cast<uint32_t>(vm) << 25U));
    };

    cpu.state().vtype = 3U << 3U;  // SEW=64
    cpu.state().vl = 2;
    regs.write(scalar, std::numeric_limits<Register>::max());
    regs.read_vector(vs2).u64[0] = 1;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VADD_VX, vector_ir(vd, scalar, vs2));
    expect(regs.read_vector(vd).u64[0] == 0,
           "vadd.vx sign-extends the scalar when SEW exceeds XLEN");

    regs.read_vector(vs2).u64[0] = std::numeric_limits<uint64_t>::max();
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VMSEQ_VX,
                                                vector_ir(static_cast<RegId>(8), scalar, vs2));
    expect((regs.read_vector(static_cast<RegId>(8)).u8[0] & 1U) != 0,
           "vmseq.vx compares against the sign-extended scalar");

    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VMV_V_X,
                                                vector_ir(vd, scalar, RegId::Zero));
    expect(regs.read_vector(vd).u64[0] == std::numeric_limits<uint64_t>::max(),
           "vmv.v.x sign-extends the scalar when SEW exceeds XLEN");

    regs.read_vector(vs2).u64[0] = 0;
    regs.read_vector(RegId::Zero).u8[0] = 1;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VMERGE_VXM,
                                                vector_ir(vd, scalar, vs2, false));
    expect(regs.read_vector(vd).u64[0] == std::numeric_limits<uint64_t>::max(),
           "vmerge.vxm sign-extends the selected scalar");

    cpu.state().misa = simrv::isa::kMisaDefault;
    cpu.state().mstatus |= enum_mask(simrv::core::MstatusBit::Fs);
    regs.write_fp(scalar, 0x1122334455667788ULL);
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFMV_V_F,
                                                vector_ir(vd, scalar, RegId::Zero));
    expect(regs.read_vector(vd).u64[0] == 0x1122334455667788ULL &&
               regs.read_vector(vd).u64[1] == 0x1122334455667788ULL,
           "vfmv.v.f retains the full 64-bit floating-point scalar on RV32");

    regs.read_vector(RegId::Zero).u8[0] = 1;
    regs.read_vector(vs2).u64[0] = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFMERGE_VFM,
                                                vector_ir(vd, scalar, vs2, false));
    expect(regs.read_vector(vd).u64[0] == 0x1122334455667788ULL,
           "vfmerge.vfm retains the full 64-bit floating-point scalar on RV32");
}

void test_vector_f_only_scalar_fp() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    constexpr auto vd = static_cast<RegId>(4);
    constexpr auto vs2 = static_cast<RegId>(6);
    constexpr auto scalar = static_cast<RegId>(2);
    const Instruction ir = (std::to_underlying(vd) << 7U) | (std::to_underlying(scalar) << 15U) |
                           (std::to_underlying(vs2) << 20U) | (1U << 25U);

    cpu.state().misa = simrv::isa::kMisaDefault;
    cpu.state().mstatus |= enum_mask(simrv::core::MstatusBit::Fs);
    cpu.state().vtype = 2U << 3U;  // SEW=32
    cpu.state().vl = 1;
    cpu.state().fcsr = 0;
    regs.read_vector(vs2).f32[0] = 2.0F;
    regs.write_fp(scalar, std::bit_cast<uint32_t>(1.5F));

    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFADD_VF, ir);
    expect(std::isnan(regs.read_vector(vd).f32[0]),
           "vfadd.vf treats an unboxed scalar as NaN when D is present");

    cpu.state().misa &= ~simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::D);
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFADD_VF, ir);
    expect(regs.read_vector(vd).f32[0] == 3.5F,
           "vfadd.vf accepts an unboxed scalar when FLEN is 32");

    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFMUL_VF, ir);
    expect(regs.read_vector(vd).f32[0] == 3.0F,
           "vfmul.vf uses the F-only scalar across the shared FP path");

    regs.read_vector(vs2).f32[0] = 1.5F;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VMFEQ_VF, ir);
    expect((regs.read_vector(vd).u8[0] & 1U) != 0,
           "vmfeq.vf compares against the F-only scalar without NaN-boxing");
}

void test_vector_fixed_point_average() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    constexpr auto vd = static_cast<RegId>(1);
    constexpr auto vs2 = static_cast<RegId>(2);
    constexpr auto vs1 = static_cast<RegId>(3);
    const auto vector_ir = [](RegId rd, RegId rs1, RegId rs2) {
        return static_cast<Instruction>((std::to_underlying(rd) << 7U) |
                                        (std::to_underlying(rs1) << 15U) |
                                        (std::to_underlying(rs2) << 20U) | (1U << 25U));
    };

    cpu.state().vl = 1;
    cpu.state().vtype = 0;
    cpu.state().vstart = 0;
    regs.read_vector(vs2).u8[0] = 2;
    regs.read_vector(vs1).u8[0] = 3;
    cpu.state().vxrm = 1;  // rne
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VAADDU_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 2,
           "vaaddu.vv rounds an exact half-way result to even under rne");

    regs.write(vs1, 3);
    cpu.state().vxrm = 0;  // rnu
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VAADDU_VX, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 3,
           "vaaddu.vx rounds an exact half-way result upward under rnu");

    regs.read_vector(vs2).u8[0] = 0;
    regs.read_vector(vs1).u8[0] = 255;
    cpu.state().vxrm = 1;  // rne: -127.5 -> -128, then wrap to unsigned SEW.
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VASUBU_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 128,
           "vasubu.vv computes in infinite precision before rounding and wrapping");

    regs.read_vector(vs2).u8[0] = 127;
    regs.read_vector(vs1).u8[0] = 128;  // signed -128
    cpu.state().vxrm = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VASUB_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u8[0] == 128,
           "vasub.vv ignores rounded-result overflow and wraps at SEW");
}

void test_vector_fixed_point_rounding_extremes() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    constexpr auto vd = static_cast<RegId>(4);
    constexpr auto vs2 = static_cast<RegId>(8);
    constexpr auto shift_reg = static_cast<RegId>(2);
    const Instruction ir = (std::to_underlying(vd) << 7U) | (std::to_underlying(shift_reg) << 15U) |
                           (std::to_underlying(vs2) << 20U) | (1U << 25U);

    cpu.state().vl = 1;
    cpu.state().vtype = 3U << 3U;  // SEW=64
    cpu.state().vxrm = 0;          // rnu
    regs.write(shift_reg, 1);
    regs.read_vector(vs2).u64[0] = std::numeric_limits<uint64_t>::max();
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VSSRL_VX, ir);
    expect(regs.read_vector(vd).u64[0] == (uint64_t{1} << 63U),
           "vssrl.vx rounds a maximum 64-bit value without overflow");

    regs.read_vector(vs2).u64[0] = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VSSRA_VX, ir);
    expect(regs.read_vector(vd).u64[0] == (uint64_t{1} << 62U),
           "vssra.vx rounds a maximum signed 64-bit value without overflow");

    cpu.state().vtype = 2U << 3U;  // SEW=32, source EEW=64
    cpu.state().vxsat = 0;
    regs.read_vector(vs2).u64[0] = std::numeric_limits<uint64_t>::max();
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VNCLIPU_WX, ir);
    expect(regs.read_vector(vd).u32[0] == std::numeric_limits<uint32_t>::max() &&
               cpu.state().vxsat == 1,
           "vnclipu.wx rounds before saturating a maximum source value");
}

void test_vector_floating_arithmetic() {
    simrv::core::Machine machine;
    auto& cpu = machine.hart(0);
    auto& regs = cpu.state().regs;
    constexpr auto vd = static_cast<RegId>(4);
    constexpr auto vs2 = static_cast<RegId>(2);
    constexpr auto vs1 = static_cast<RegId>(6);
    const auto vector_ir = [](RegId rd, RegId rs1, RegId rs2) {
        return static_cast<Instruction>((std::to_underlying(rd) << 7U) |
                                        (std::to_underlying(rs1) << 15U) |
                                        (std::to_underlying(rs2) << 20U) | (1U << 25U));
    };

    cpu.state().misa = simrv::isa::kMisaDefault;
    cpu.state().mstatus |= enum_mask(simrv::core::MstatusBit::Fs);
    cpu.state().vtype = 2U << 3U;  // SEW=32
    cpu.state().vl = 1;
    cpu.state().vstart = 0;
    cpu.state().fcsr = 0;
    regs.read_vector(vs2).f32[0] = 7.0F;
    regs.read_vector(vs1).f32[0] = 2.0F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSUB_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).f32[0] == 5.0F, "vfsub.vv subtracts vector operands in order");

    regs.write_fp(vs1, UINT64_C(0xFFFFFFFF00000000) | std::bit_cast<uint32_t>(10.0F));
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFRSUB_VF, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).f32[0] == 3.0F,
           "vfrsub.vf subtracts each vector element from the scalar");

    regs.read_vector(vs2).f32[0] = 9.0F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSQRT_V, vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).f32[0] == 3.0F, "vfsqrt.v computes an element-wise square root");

    regs.read_vector(vs2).f32[0] = 2.0F;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSQRT_V, vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3FB504F3) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfsqrt.v RMM rounds binary32 directly and accrues inexact");
    cpu.state().vtype = 3U << 3U;  // SEW=64
    regs.read_vector(vs2).f64[0] = 2.0;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSQRT_V, vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF6A09E667F3BCD) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfsqrt.v RMM checks a binary64 square-root midpoint exactly");
    cpu.state().vtype = 2U << 3U;  // SEW=32
    cpu.state().fcsr = 0;

    regs.read_vector(vs2).f32[0] = 1.0F;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFREC7_V, vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3F7F0000),
           "vfrec7.v produces the specified seven-bit reciprocal estimate");

    regs.read_vector(vs2).u32[0] = UINT32_C(0x00718ABC);
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFREC7_V, vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x7E900000),
           "vfrec7.v normalizes subnormal inputs according to the architectural lookup");

    regs.read_vector(vs2).f32[0] = 4.0F;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFRSQRT7_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3EFF0000),
           "vfrsqrt7.v produces the specified seven-bit reciprocal-square-root estimate");

    regs.read_vector(vs2).f32[0] = -1.0F;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFRSQRT7_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x7FC00000) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nv)) != 0,
           "vfrsqrt7.v returns canonical NaN and raises invalid for a negative operand");

    regs.read_vector(vs2).f32[0] = 0.0F;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFREC7_V, vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x7F800000) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Dz)) != 0,
           "vfrec7.v returns infinity and raises divide-by-zero for positive zero");

    regs.read_vector(vs2).u32[0] = UINT32_C(0x80000000);
    regs.read_vector(vs2).u32[1] = UINT32_C(0x7F800001);
    cpu.state().vl = 2;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFCLASS_V, vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == (1U << 3U) && regs.read_vector(vd).u32[1] == (1U << 8U),
           "vfclass.v distinguishes negative zero and signaling NaN");
    expect((cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nv)) == 0,
           "vfclass.v does not raise invalid for a signaling NaN");
    cpu.state().vl = 1;

    regs.read_vector(vs2).f32[0] = 2.5F;
    cpu.state().fcsr = 0;  // RNE
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFCVT_X_F_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).i32[0] == 2 &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfcvt.x.f.v rounds ties to even and accrues inexact");

    regs.read_vector(vs2).f32[0] = -1.75F;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFCVT_RTZ_X_F_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(!cpu.active_context().pending_exception.has_value() && regs.read_vector(vd).i32[0] == -1,
           "vfcvt.rtz.x.f.v truncates toward zero");

    regs.read_vector(vs2).u32[0] = UINT32_C(0x7FC00000);
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFCVT_XU_F_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_MAX &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nv)) != 0 &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) == 0,
           "vfcvt.xu.f.v saturates NaN and raises invalid without inexact");

    regs.read_vector(vs2).i32[0] = -7;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFCVT_F_X_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).f32[0] == -7.0F,
           "vfcvt.f.x.v converts signed integer vector elements");

    regs.read_vector(vs2).i32[0] = 16'777'217;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFCVT_F_X_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x4B800001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfcvt.f.x.v RMM rounds an integer halfway between FP32 values away from zero");
    regs.read_vector(vs2).i32[0] = 16'777'219;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFCVT_F_X_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x4B800002),
           "vfcvt.f.x.v RMM preserves a tie already rounded away by RNE");

    cpu.state().vtype = 3U << 3U;                              // SEW=64
    regs.read_vector(vs2).i64[0] = INT64_C(9007199254740993);  // 2^53 + 1
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFCVT_F_X_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x4340000000000001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfcvt.f.x.v RMM rounds a 64-bit integer tie to FP64 away from zero");
    cpu.state().vtype = 2U << 3U;  // SEW=32, source EEW=64 for narrowing
    regs.read_vector(vs2).i64[0] = (INT64_C(1) << 53U) + (INT64_C(1) << 29U);
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFNCVT_F_X_W,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x5A000001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfncvt.f.x.w RMM rounds a 64-bit integer tie to FP32 away from zero");
    regs.read_vector(vs2).i64[0] = (INT64_C(1) << 53U) + (INT64_C(1) << 29U) - 1;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFNCVT_F_X_W,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x5A000000),
           "vfncvt.f.x.w retains low integer bits when testing an FP32 midpoint");
    cpu.state().fcsr = 0;

    regs.read_vector(vs2).i32[0] = -9;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFWCVT_F_X_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(!cpu.active_context().pending_exception.has_value(),
           "legal widening conversion does not raise an exception");
    expect(regs.read_vector(vd).f64[0] == -9.0,
           "vfwcvt.f.x.v widens signed integer elements to floating point");

    regs.read_vector(vs2).f32[0] = -3.5F;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFWCVT_X_F_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).i64[0] == -4,
           "vfwcvt.x.f.v widens a rounded floating-point value to a signed integer");

    regs.read_vector(vs2).f64[0] = 1.5;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFNCVT_F_F_W,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).f32[0] == 1.5F,
           "vfncvt.f.f.w narrows a double-width floating-point element");

    regs.read_vector(vs2).f64[0] = 1.0 + std::ldexp(1.0, -24);
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFNCVT_F_F_W,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3F800001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfncvt.f.f.w RMM rounds a binary64-to-binary32 tie away from zero");

    regs.read_vector(vs2).f64[0] = 1.0 + std::ldexp(1.0, -24);
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFNCVT_ROD_F_F_W,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3F800001),
           "vfncvt.rod.f.f.w forces an inexact result's low bit odd");

    cpu.state().fcsr = 7U << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFCLASS_V, vector_ir(vd, RegId::Zero, vs2));
    expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction,
           "every RVV floating-point instruction reserves an invalid frm value");
    cpu.active_context().pending_exception.reset();

    cpu.state().misa &= ~simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::D);
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFWCVT_F_X_V,
                                                vector_ir(vd, RegId::Zero, vs2));
    expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction,
           "SEW=32 widening FP operations require D for their 64-bit floating-point element");
    cpu.active_context().pending_exception.reset();
    cpu.state().misa = simrv::isa::kMisaDefault;

    cpu.state().vl = 2;
    cpu.state().fcsr = 0;
    regs.write_fp(vs1, simrv::xlen::kF32BoxerBits | std::bit_cast<uint32_t>(4.25F));
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMV_V_F, vector_ir(vd, vs1, RegId::Zero));
    expect(regs.read_vector(vd).f32[0] == 4.25F && regs.read_vector(vd).f32[1] == 4.25F,
           "vfmv.v.f splats a scalar floating-point value across active elements");

    cpu.state().vl = 3;
    regs.read_vector(vs2).f32[0] = 1.0F;
    regs.read_vector(vs2).f32[1] = 2.0F;
    regs.read_vector(vs2).f32[2] = 3.0F;
    regs.write_fp(vs1, simrv::xlen::kF32BoxerBits | std::bit_cast<uint32_t>(9.0F));
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSLIDE1UP_VF, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).f32[0] == 9.0F && regs.read_vector(vd).f32[1] == 1.0F &&
               regs.read_vector(vd).f32[2] == 2.0F,
           "vfslide1up.vf inserts the scalar and shifts lower source elements upward");

    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSLIDE1DOWN_VF, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).f32[0] == 2.0F && regs.read_vector(vd).f32[1] == 3.0F &&
               regs.read_vector(vd).f32[2] == 9.0F,
           "vfslide1down.vf shifts higher source elements down and appends the scalar");

    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSLIDE1UP_VF, vector_ir(vs2, vs1, vs2));
    expect(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction,
           "vfslide1up.vf rejects overlapping destination and source groups");
    cpu.active_context().pending_exception.reset();
    cpu.state().vl = 1;

    regs.read_vector(vs2).f32[0] = 1.0F;
    regs.read_vector(vs1).f32[0] = 0.0F;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFDIV_VV, vector_ir(vd, vs1, vs2));
    expect(std::isinf(regs.read_vector(vd).f32[0]) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Dz)) != 0,
           "vfdiv.vv produces infinity and accrues divide-by-zero in fflags");

    regs.read_vector(vs2).u32[0] = UINT32_C(0x7FC12345);
    regs.write_fp(vs1, simrv::xlen::kF32BoxerBits | std::bit_cast<uint32_t>(1.0F));
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFADD_VF, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == simrv::xlen::kF32Qnan,
           "vfadd.vf canonicalizes a NaN payload from a vector operand");

    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    regs.read_vector(vs2).f32[0] = 1.0F;
    regs.read_vector(vs1).f32[0] = 0x1p-24F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFADD_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3F800001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfadd.vv RMM rounds a binary32 tie away from zero and raises inexact");

    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    regs.write_fp(vs1, simrv::xlen::kF32BoxerBits | std::bit_cast<uint32_t>(-0x1p-24F));
    regs.read_vector(vs2).f32[0] = -1.0F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFADD_VF, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0xBF800001),
           "vfadd.vf RMM rounds a negative binary32 tie away from zero");

    regs.read_vector(vs2).f32[0] = 1.0F;
    regs.read_vector(vs1).f32[0] = 0x1.8p-24F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSUB_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3F7FFFFF),
           "vfsub.vv RMM rounds a positive tie away from zero");

    regs.read_vector(vs2).f32[0] = 1.5F;
    regs.read_vector(vs1).u32[0] = UINT32_C(0x3F800003);
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMUL_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3FC00005),
           "vfmul.vv RMM rounds a binary32 product tie away from zero");

    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    regs.read_vector(vs2).u32[0] = UINT32_C(0x7F7FFFFF);
    regs.read_vector(vs1).f32[0] = 0.0F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFADD_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x7F7FFFFF) &&
               (cpu.state().fcsr & UINT32_C(0x1F)) == 0,
           "an exact RMM result at the binary32 maximum does not raise flags");

    regs.read_vector(vs2).u32[0] = UINT32_C(0x00000001);
    regs.read_vector(vs1).f32[0] = 2.0F;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFDIV_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x00000001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Uf)) != 0 &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfdiv.vv RMM rounds a half-subnormal binary32 quotient away from zero");
    regs.read_vector(vs2).f32[0] = 2.0F;
    regs.write_fp(vs1, simrv::xlen::kF32BoxerBits | UINT32_C(0x00000001));
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFRDIV_VF, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x00000001),
           "vfrdiv.vf RMM applies the same half-subnormal rule to reverse division");

    cpu.state().vtype = 3U << 3U;  // SEW=64
    regs.read_vector(vs2).f64[0] = 1.0;
    regs.read_vector(vs1).f64[0] = 0x1p-53;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFADD_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF0000000000001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfadd.vv RMM rounds a binary64 tie away from zero");
    regs.write_fp(vs1, std::bit_cast<uint64_t>(0x1p-53));
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFADD_VF, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF0000000000001),
           "vfadd.vf RMM applies the binary64 tie rule to scalar operands");
    regs.read_vector(vs1).f64[0] = 0x1.fffffffffffffp-54;  // Just below the tie.
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFADD_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF0000000000000),
           "vfadd.vv RMM distinguishes a binary64 near-tie from an exact tie");
    regs.read_vector(vs2).f64[0] = -1.0;
    regs.read_vector(vs1).f64[0] = 0x1p-53;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSUB_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0xBFF0000000000001),
           "vfsub.vv RMM rounds a negative binary64 tie away from zero");
    regs.read_vector(vs2).f64[0] = 1.5;
    regs.read_vector(vs1).u64[0] = UINT64_C(0x3FF0000000000003);
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMUL_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF8000000000005) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfmul.vv RMM rounds an exact binary64 product tie away from zero");
    regs.read_vector(vs2).f64[0] = 0x1p-1022;
    regs.read_vector(vs1).f64[0] = 0x1p-53;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMUL_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x0000000000000001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Uf)) != 0 &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfmul.vv RMM rounds a half-subnormal product away from zero");
    regs.read_vector(vs2).u64[0] = UINT64_C(0x0000000000000001);
    regs.read_vector(vs1).f64[0] = 2.0;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFDIV_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x0000000000000001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Uf)) != 0 &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfdiv.vv RMM rounds a half-subnormal quotient away from zero");
    regs.read_vector(vs1).f64[0] = 1.0 + 0x1p-52;
    regs.read_vector(vs2).f64[0] = 1.0 - 0x1p-52;
    regs.read_vector(vd).f64[0] = 0x1p-53 + 0x1p-104;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMACC_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF0000000000001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfmacc.vv RMM rounds an exact fused binary64 tie away from zero");
    regs.read_vector(vd).f64[0] = 0x1p-53 + 0x1p-105;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMACC_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF0000000000000),
           "vfmacc.vv RMM preserves the low fused bit below a binary64 midpoint");
    cpu.state().vtype = 2U << 3U;  // SEW=32

    regs.read_vector(vs1).f32[0] = 1.0F + 0x1p-23F;
    regs.read_vector(vs2).f32[0] = 1.0F - 0x1p-23F;
    regs.read_vector(vd).f32[0] = 0x1p-24F + 0x1p-46F;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMACC_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3F800001),
           "vfmacc.vv RMM rounds an exact fused binary32 tie away from zero");
    regs.read_vector(vd).f32[0] = 0x1p-24F + 0x1p-47F;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMACC_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3F800000),
           "vfmacc.vv RMM distinguishes a fused binary32 near-tie");
    cpu.state().fcsr = 0;

    constexpr std::array kFusedCases = {
        std::pair{simrv::isa::OperationId::VFMADD_VV, 23.0F},
        std::pair{simrv::isa::OperationId::VFNMADD_VV, -23.0F},
        std::pair{simrv::isa::OperationId::VFMSUB_VV, 17.0F},
        std::pair{simrv::isa::OperationId::VFNMSUB_VV, -17.0F},
        std::pair{simrv::isa::OperationId::VFMACC_VV, 16.0F},
        std::pair{simrv::isa::OperationId::VFNMACC_VV, -16.0F},
        std::pair{simrv::isa::OperationId::VFMSAC_VV, -4.0F},
        std::pair{simrv::isa::OperationId::VFNMSAC_VV, 4.0F},
    };
    for (const auto& [operation, expected] : kFusedCases) {
        regs.read_vector(vd).f32[0] = 10.0F;
        regs.read_vector(vs1).f32[0] = 2.0F;
        regs.read_vector(vs2).f32[0] = 3.0F;
        simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(), operation,
                                                    vector_ir(vd, vs1, vs2));
        expect(regs.read_vector(vd).f32[0] == expected,
               "vector fused floating-point variants use their specified operand signs/order");
    }

    regs.read_vector(vs2).f32[0] = 3.0F;
    regs.write_fp(vs1, UINT64_C(0xFFFFFFFF00000000) | std::bit_cast<uint32_t>(2.0F));
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMADD_VF, vector_ir(vs2, vs1, vs2));
    expect(regs.read_vector(vs2).f32[0] == 9.0F,
           "vfmadd.vf snapshots an overlapping destination multiplicand");

    cpu.state().vl = 3;
    regs.read_vector(vs1).f32[0] = 10.0F;
    regs.read_vector(vs2).f32[0] = 1.0F;
    regs.read_vector(vs2).f32[1] = 2.0F;
    regs.read_vector(vs2).f32[2] = 3.0F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFREDOSUM_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).f32[0] == 16.0F,
           "vfredosum.vs accumulates active elements in order from vs1[0]");

    cpu.state().vl = 1;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    regs.read_vector(vs1).f32[0] = 1.0F;
    regs.read_vector(vs2).f32[0] = 0x1p-24F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFREDOSUM_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x3F800001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nx)) != 0,
           "vfredosum.vs applies RMM to a binary32 halfway sum");
    regs.read_vector(vs1).f32[0] = -1.0F;
    regs.read_vector(vs2).f32[0] = -0x1p-24F;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFREDUSUM_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0xBF800001),
           "vfredusum.vs applies RMM to a negative binary32 halfway sum");
    cpu.state().vtype = 3U << 3U;  // SEW=64
    regs.read_vector(vs1).f64[0] = 1.0;
    regs.read_vector(vs2).f64[0] = 0x1p-53;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFREDOSUM_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF0000000000001),
           "vfredosum.vs applies RMM to a binary64 halfway sum");
    cpu.state().vtype = 2U << 3U;  // SEW=32
    cpu.state().fcsr = 0;

    regs.read_vector(vs1).u32[0] = UINT32_C(0x00000000);
    regs.read_vector(vs2).u32[0] = UINT32_C(0x80000000);
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFREDMIN_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x80000000),
           "vfredmin.vs selects negative zero");

    regs.read_vector(vs1).u32[0] = UINT32_C(0x7F800001);
    regs.read_vector(RegId::Zero).u8[0] = 0;
    cpu.state().fcsr = 0;
    const Instruction masked_ir = vector_ir(vd, vs1, vs2) & ~(Instruction{1} << 25U);
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFREDMAX_VS, masked_ir);
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x7F800001) &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nv)) == 0,
           "an empty FP reduction copies vs1[0] without canonicalizing or raising exceptions");

    cpu.state().vl = 0;
    regs.read_vector(vd).u32[0] = UINT32_C(0x12345678);
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFREDMAX_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x12345678),
           "a zero-length floating-point reduction leaves its destination unchanged");
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VREDSUM_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x12345678),
           "a zero-length integer reduction leaves its destination unchanged");
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFWREDOSUM_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x12345678),
           "a zero-length widening FP reduction leaves its destination unchanged");
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VWREDSUM_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x12345678),
           "a zero-length widening integer reduction leaves its destination unchanged");

    cpu.state().vl = 3;
    regs.read_vector(vs1).f64[0] = 10.0;
    regs.read_vector(vs2).f32[0] = 1.0F;
    regs.read_vector(vs2).f32[1] = 2.0F;
    regs.read_vector(vs2).f32[2] = 3.0F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFWREDOSUM_VS, vector_ir(vs2, vs1, vs2));
    expect(regs.read_vector(vs2).f64[0] == 16.0,
           "vfwredosum.vs widens each source and tolerates destination/source overlap");

    cpu.state().vl = 1;
    regs.read_vector(vs1).f64[0] = 1.0;
    regs.read_vector(vs2).f32[0] = 0x1p-53F;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFWREDOSUM_VS, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF0000000000001),
           "vfwredosum.vs applies RMM to a widened binary64 halfway sum");
    cpu.state().vl = 3;
    cpu.state().fcsr = 0;

    regs.read_vector(vs2).f32[0] = 1.5F;
    regs.read_vector(vs1).f32[0] = 2.25F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFWADD_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).f64[0] == 3.75, "vfwadd.vv widens both inputs before addition");

    regs.read_vector(vs2).f64[0] = 10.0;
    regs.read_vector(vs1).f32[0] = 2.0F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFWSUB_WV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).f64[0] == 8.0,
           "vfwsub.wv subtracts a widened narrow operand from a wide operand");

    constexpr auto overlapping_source = static_cast<RegId>(3);
    regs.read_vector(overlapping_source).f32[0] = 3.0F;
    regs.write_fp(vs1, UINT64_C(0xFFFFFFFF00000000) | std::bit_cast<uint32_t>(2.0F));
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFWMUL_VF,
                                                vector_ir(vs2, vs1, overlapping_source));
    expect(regs.read_vector(vs2).f64[0] == 6.0,
           "vfwmul.vf accepts the permitted high-end destination/source overlap");

    constexpr std::array kWideningFusedCases = {
        std::pair{simrv::isa::OperationId::VFWMACC_VV, 16.0},
        std::pair{simrv::isa::OperationId::VFWNMACC_VV, -16.0},
        std::pair{simrv::isa::OperationId::VFWMSAC_VV, -4.0},
        std::pair{simrv::isa::OperationId::VFWNMSAC_VV, 4.0},
    };
    for (const auto& [operation, expected] : kWideningFusedCases) {
        regs.read_vector(vd).f64[0] = 10.0;
        regs.read_vector(vs1).f32[0] = 2.0F;
        regs.read_vector(vs2).f32[0] = 3.0F;
        simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(), operation,
                                                    vector_ir(vd, vs1, vs2));
        expect(regs.read_vector(vd).f64[0] == expected,
               "widening fused accumulates use their specified product and accumulator signs");
    }
    cpu.state().vl = 1;

    regs.read_vector(vd).f64[0] = 0x1p-53;
    regs.read_vector(vs1).f32[0] = 1.0F;
    regs.read_vector(vs2).f32[0] = 1.0F;
    cpu.state().fcsr = enum_mask(simrv::isa::RoundingMode::Rmm) << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFWMACC_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u64[0] == UINT64_C(0x3FF0000000000001),
           "vfwmacc.vv applies RMM to the fused binary64 accumulator");
    cpu.state().fcsr = 0;

    regs.read_vector(vs2).f32[0] = 3.0F;
    regs.read_vector(vs1).f32[0] = 2.0F;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFSGNJN_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).f32[0] == -3.0F,
           "vfsgnjn.vv copies magnitude and negates the second operand sign");

    regs.read_vector(vs2).u32[0] = UINT32_C(0x00000000);
    regs.read_vector(vs1).u32[0] = UINT32_C(0x80000000);
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMIN_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x80000000),
           "vfmin.vv selects negative zero when its operands compare equal");
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMAX_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).u32[0] == UINT32_C(0x00000000),
           "vfmax.vv selects positive zero when its operands compare equal");

    regs.read_vector(vs2).u32[0] = UINT32_C(0x7F800001);  // signaling NaN
    regs.read_vector(vs1).f32[0] = 4.0F;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFMIN_VV, vector_ir(vd, vs1, vs2));
    expect(regs.read_vector(vd).f32[0] == 4.0F &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nv)) != 0,
           "vfmin.vv returns the numeric operand and raises invalid for a signaling NaN");

    regs.read_vector(vs2).u32[0] = simrv::xlen::kF32Qnan;
    regs.read_vector(vs1).f32[0] = 2.0F;
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VMFEQ_VV, vector_ir(RegId::Zero, vs1, vs2));
    expect((regs.read_vector(RegId::Zero).u8[0] & 1U) == 0 &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nv)) == 0,
           "vmfeq.vv is quiet for a quiet NaN and writes a false mask bit");

    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VMFLT_VV, vector_ir(RegId::Zero, vs1, vs2));
    expect((regs.read_vector(RegId::Zero).u8[0] & 1U) == 0 &&
               (cpu.state().fcsr & enum_mask(simrv::isa::FflagsBit::Nv)) != 0,
           "vmflt.vv raises invalid for an unordered comparison");
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
    const CSRValue im = simrv::isa::misa_profile_bits(simrv::isa::MisaProfile::IM);
    const CSRValue ima = simrv::isa::misa_profile_bits(simrv::isa::MisaProfile::IMA);
    const CSRValue gc = simrv::isa::misa_profile_bits(simrv::isa::MisaProfile::GC);
    const CSRValue gcbv = simrv::isa::misa_profile_bits(simrv::isa::MisaProfile::GCBV);

    expect(simrv::isa::misa_has_extension(im, simrv::isa::IsaExtension::I) &&
               simrv::isa::misa_has_extension(im, simrv::isa::IsaExtension::M),
           "the named IM profile includes I and M");
    expect(!simrv::isa::misa_has_extension(im, simrv::isa::IsaExtension::A) &&
               !simrv::isa::misa_has_extension(im, simrv::isa::IsaExtension::C),
           "the named IM profile does not include A or C");

    expect(simrv::isa::misa_has_extension(ima, simrv::isa::IsaExtension::I) &&
               simrv::isa::misa_has_extension(ima, simrv::isa::IsaExtension::M) &&
               simrv::isa::misa_has_extension(ima, simrv::isa::IsaExtension::A) &&
               simrv::isa::misa_has_extension(ima, simrv::isa::IsaExtension::S) &&
               simrv::isa::misa_has_extension(ima, simrv::isa::IsaExtension::U),
           "the named IMA profile includes I, M, A, S, U");
    expect(!simrv::isa::misa_has_extension(ima, simrv::isa::IsaExtension::C) &&
               !simrv::isa::misa_has_extension(ima, simrv::isa::IsaExtension::F),
           "the named IMA profile does not include C or F");

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
    const simrv::memory::RamView ram_view(ram.data(), 0, ram.size());
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
                                  static_cast<Instruction>(simrv::isa::Funct3::Sw), ram_view);
    simrv::memory::ram_write_fast(kNext + 4, ((kPhysical >> 12U) << 10U) | kLeafFlags,
                                  static_cast<Instruction>(simrv::isa::Funct3::Sw), ram_view);
    simrv::Mmu mmu(ram.data(), 0, ram.size());
    const auto translated =
        mmu.translate(kVirtual, simrv::PteAccess::Read, kPrivSupervisor, 0, kSatp, 32, false);
    expect(translated.has_value() && *translated == 0x3234,
           "Sv32 walks a two-level page table and preserves the page offset");

    constexpr Word kReservedNonLeaf = enum_mask(simrv::PteFlag::D);
    simrv::memory::ram_write_fast(kRoot, ((kNext >> 12U) << 10U) | kValid | kReservedNonLeaf,
                                  static_cast<Instruction>(simrv::isa::Funct3::Sw), ram_view);
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
        const simrv::memory::RamView ram_view(ram.data(), 0, ram.size());
        constexpr Address kRoot = 0x1000;
        constexpr Address kLevel1 = 0x2000;
        constexpr Address kLevel0 = 0x3000;
        constexpr Address kPhysical = 0x4000;
        constexpr Address kVirtual = 0x1234;
        constexpr Word kSatp = static_cast<Word>((uint64_t{8} << 60U) | (kRoot >> 12U));
        constexpr Word kValid = enum_mask(simrv::PteFlag::V);
        constexpr Word kLeafFlags =
            kValid | enum_mask(simrv::PteFlag::R) | enum_mask(simrv::PteFlag::A);
        const auto write_pte = [ram_view](Address address, Word pte) {
            simrv::memory::ram_write_fast(
                address, pte, static_cast<Instruction>(simrv::isa::Funct3::Sd), ram_view);
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
    const auto encode_fma = [](unsigned opcode, unsigned fmt) {
        return (3U << 27U) | (fmt << 25U) | (2U << 20U) | (1U << 15U) | (2U << 7U) | opcode;
    };

    expect(simrv::pipeline::decoder(encode_op_fp(0x2CU, 0, 0)) == simrv::isa::OperationId::FSQRT_S,
           "FSQRT.S requires and accepts rs2=x0");
    expect(simrv::pipeline::decoder(encode_op_fp(0x2CU, 0, 1)) == simrv::isa::OperationId::UNKNOWN,
           "FSQRT.S with nonzero rs2 is reserved");
    expect(simrv::pipeline::decoder(encode_op_fp(0x70U, 1, 1)) == simrv::isa::OperationId::UNKNOWN,
           "FCLASS.S with nonzero rs2 is reserved");
    constexpr std::array<unsigned, 4> kFmaOpcodes = {0x43U, 0x47U, 0x4BU, 0x4FU};
    constexpr std::array<std::array<simrv::isa::OperationId, 2>, 4> kFmaOperations = {{
        {simrv::isa::OperationId::FMADD_S, simrv::isa::OperationId::FMADD_D},
        {simrv::isa::OperationId::FMSUB_S, simrv::isa::OperationId::FMSUB_D},
        {simrv::isa::OperationId::FNMSUB_S, simrv::isa::OperationId::FNMSUB_D},
        {simrv::isa::OperationId::FNMADD_S, simrv::isa::OperationId::FNMADD_D},
    }};
    for (size_t operation = 0; operation < kFmaOpcodes.size(); ++operation) {
        for (unsigned format = 0; format < 2; ++format) {
            expect(simrv::pipeline::decoder(encode_fma(kFmaOpcodes[operation], format)) ==
                       kFmaOperations[operation][format],
                   "checked fused-operation table covers every legal opcode and format");
        }
    }
    expect(
        simrv::pipeline::decoder(encode_fma(kFmaOpcodes[0], 2)) == simrv::isa::OperationId::UNKNOWN,
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

void test_vector_decode_tables() {
    constexpr Instruction kOpV = 0x57U;
    const auto encode_op_v = [](unsigned funct6, unsigned funct3, bool vm, unsigned rs1 = 1,
                                unsigned vs2 = 2) {
        return (funct6 << 26U) | (static_cast<unsigned>(vm) << 25U) | (vs2 << 20U) | (rs1 << 15U) |
               (funct3 << 12U) | (3U << 7U) | kOpV;
    };
    const auto encode_vector_memory = [](bool store, unsigned width, unsigned umop, bool vm = true,
                                         unsigned nf = 0) {
        constexpr unsigned kVectorLoad = 0x07;
        constexpr unsigned kVectorStore = 0x27;
        return (nf << 29U) | (static_cast<unsigned>(vm) << 25U) | (umop << 20U) | (1U << 15U) |
               (width << 12U) | (3U << 7U) | (store ? kVectorStore : kVectorLoad);
    };

    expect(simrv::pipeline::decoder(encode_vector_memory(false, 0, 11)) ==
               simrv::isa::OperationId::VLM_V,
           "vector memory decoder recognizes the canonical mask load");
    expect(simrv::pipeline::decoder(encode_vector_memory(true, 0, 11)) ==
               simrv::isa::OperationId::VSM_V,
           "vector memory decoder recognizes the canonical mask store");
    expect(simrv::pipeline::decoder(encode_vector_memory(false, 6, 16, true, 2)) ==
               simrv::isa::OperationId::VLE32FF_V,
           "vector memory decoder preserves nf for fault-only-first segment loads");
    expect(simrv::pipeline::decoder(encode_vector_memory(false, 0, 11, false)) ==
               simrv::isa::OperationId::UNKNOWN,
           "vector memory decoder rejects masked mask-load encodings");
    expect(simrv::pipeline::decoder(encode_vector_memory(false, 0, 0) | (1U << 28U)) ==
               simrv::isa::OperationId::UNKNOWN,
           "vector memory decoder rejects reserved nonzero mew encodings");
    expect(simrv::pipeline::decoder(encode_vector_memory(false, 0, 8, false)) ==
               simrv::isa::OperationId::UNKNOWN,
           "whole-register vector loads require vm=1");
    const auto indexed64_load = encode_vector_memory(false, 7, 2) | (1U << 26U);
    const auto indexed64_store = encode_vector_memory(true, 7, 2) | (1U << 26U);
    if constexpr (simrv::xlen::kIsXLen64) {
        expect(simrv::pipeline::decoder(indexed64_load) == simrv::isa::OperationId::VLUXEI64_V &&
                   simrv::pipeline::decoder(indexed64_store) == simrv::isa::OperationId::VSUXEI64_V,
               "RV64 decodes 64-bit indexed vector memory operations");
    } else {
        expect(simrv::pipeline::decoder(indexed64_load) == simrv::isa::OperationId::UNKNOWN &&
                   simrv::pipeline::decoder(indexed64_store) == simrv::isa::OperationId::UNKNOWN,
               "RV32 rejects 64-bit indexed vector memory encodings");
    }

    expect(
        simrv::pipeline::decoder(encode_op_v(0x0D, 6, true)) == simrv::isa::OperationId::VCLMULH_VX,
        "common vector table decodes a funct6/funct3 operation");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x11, 0, true)) == simrv::isa::OperationId::VMADC_VV,
        "unmasked vector table selects the vm=1 operation");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x11, 0, false)) == simrv::isa::OperationId::VMADC_VVM,
        "masked vector table selects the vm=0 operation");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x22, 2, true)) == simrv::isa::OperationId::VREMU_VV,
        "vector table decodes unsigned remainder");
    expect(simrv::pipeline::decoder(encode_op_v(0x23, 6, true)) == simrv::isa::OperationId::VREM_VX,
           "vector table decodes signed scalar remainder");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x24, 2, true)) == simrv::isa::OperationId::VMULHU_VV,
        "vector table decodes unsigned high-half multiply");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x26, 6, true)) == simrv::isa::OperationId::VMULHSU_VX,
        "vector table decodes signed-unsigned high-half multiply");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x27, 2, true)) == simrv::isa::OperationId::VMULH_VV,
        "vector table decodes signed high-half multiply");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x01, 2, true)) == simrv::isa::OperationId::VREDAND_VS,
        "vector table decodes bitwise reductions alongside Zvbb entries");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x07, 2, true)) == simrv::isa::OperationId::VREDMAX_VS,
        "vector table decodes signed maximum reduction");
    expect(simrv::pipeline::decoder(encode_op_v(0x0C, 0, true)) ==
               simrv::isa::OperationId::VRGATHER_VV,
           "vector table decodes vector-index gather alongside Zvbc entries");
    expect(simrv::pipeline::decoder(encode_op_v(0x0E, 0, true)) ==
               simrv::isa::OperationId::VRGATHEREI16_VV,
           "vector table decodes 16-bit-index gather alongside slide-up entries");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x08, 2, true)) == simrv::isa::OperationId::VAADDU_VV,
        "vector table decodes unsigned averaging add");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x0B, 6, true)) == simrv::isa::OperationId::VASUB_VX,
        "vector table decodes signed scalar averaging subtract");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x2B, 2, true)) == simrv::isa::OperationId::VNMSUB_VV,
        "vector table names the negative multiply-subtract encoding correctly");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x24, 1, true)) == simrv::isa::OperationId::VFMUL_VV,
        "vector table decodes floating-point multiply alongside integer multiply-high");
    expect(simrv::pipeline::decoder(encode_op_v(0x13, 1, true, 0)) ==
               simrv::isa::OperationId::VFSQRT_V,
           "vector special decoder accepts the canonical vfsqrt.v encoding");
    expect(simrv::pipeline::decoder(encode_op_v(0x13, 1, true, 16)) ==
               simrv::isa::OperationId::VFCLASS_V,
           "vector special decoder accepts the vfclass.v unary encoding");
    expect(simrv::pipeline::decoder(encode_op_v(0x13, 1, true, 4)) ==
               simrv::isa::OperationId::VFRSQRT7_V,
           "vector special decoder accepts the vfrsqrt7.v unary encoding");
    expect(simrv::pipeline::decoder(encode_op_v(0x13, 1, true, 5)) ==
               simrv::isa::OperationId::VFREC7_V,
           "vector special decoder accepts the vfrec7.v unary encoding");
    expect(simrv::pipeline::decoder(encode_op_v(0x12, 1, true, 0)) ==
               simrv::isa::OperationId::VFCVT_XU_F_V,
           "vector special decoder accepts vfcvt.xu.f.v");
    expect(simrv::pipeline::decoder(encode_op_v(0x12, 1, true, 7)) ==
               simrv::isa::OperationId::VFCVT_RTZ_X_F_V,
           "vector special decoder accepts vfcvt.rtz.x.f.v");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x12, 1, true, 4)) == simrv::isa::OperationId::UNKNOWN,
        "vector special decoder rejects reserved same-width conversion selectors");
    expect(simrv::pipeline::decoder(encode_op_v(0x12, 1, true, 8)) ==
               simrv::isa::OperationId::VFWCVT_XU_F_V,
           "vector special decoder accepts the first widening conversion selector");
    expect(simrv::pipeline::decoder(encode_op_v(0x12, 1, true, 15)) ==
               simrv::isa::OperationId::VFWCVT_RTZ_X_F_V,
           "vector special decoder accepts the final widening conversion selector");
    expect(simrv::pipeline::decoder(encode_op_v(0x12, 1, true, 16)) ==
               simrv::isa::OperationId::VFNCVT_XU_F_W,
           "vector special decoder accepts the first narrowing conversion selector");
    expect(simrv::pipeline::decoder(encode_op_v(0x12, 1, true, 23)) ==
               simrv::isa::OperationId::VFNCVT_RTZ_X_F_W,
           "vector special decoder accepts the final narrowing conversion selector");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x13, 1, true, 1)) == simrv::isa::OperationId::UNKNOWN,
        "vector special decoder rejects vfsqrt.v with reserved nonzero vs1");
    expect(simrv::pipeline::decoder(encode_op_v(0x17, 5, true, 1, 0)) ==
               simrv::isa::OperationId::VFMV_V_F,
           "vector special decoder accepts vfmv.v.f with vs2=v0");
    expect(simrv::pipeline::decoder(encode_op_v(0x17, 5, true, 1, 2)) ==
               simrv::isa::OperationId::UNKNOWN,
           "vector special decoder rejects vfmv.v.f with a nonzero vs2 field");
    for (const auto funct3 : {0U, 3U, 4U}) {
        expect(simrv::pipeline::decoder(encode_op_v(0x17, funct3, true, 1, 0)) !=
                   simrv::isa::OperationId::UNKNOWN,
               "vector move decoder accepts the reserved-zero vs2 encoding");
        expect(simrv::pipeline::decoder(encode_op_v(0x17, funct3, true, 1, 2)) ==
                   simrv::isa::OperationId::UNKNOWN,
               "vector move decoder rejects a nonzero vs2 field");
    }
    expect(
        simrv::pipeline::decoder(encode_op_v(0x09, 5, true)) == simrv::isa::OperationId::VFSGNJN_VF,
        "vector table decodes scalar floating-point sign injection");
    expect(simrv::pipeline::decoder(encode_op_v(0x0E, 5, true)) ==
               simrv::isa::OperationId::VFSLIDE1UP_VF,
           "vector table decodes floating-point slide-one-up");
    expect(simrv::pipeline::decoder(encode_op_v(0x0F, 5, true)) ==
               simrv::isa::OperationId::VFSLIDE1DOWN_VF,
           "vector table decodes floating-point slide-one-down");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x04, 1, true)) == simrv::isa::OperationId::VFMIN_VV,
        "vector table decodes vector floating-point minimum");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x06, 5, true)) == simrv::isa::OperationId::VFMAX_VF,
        "vector table decodes scalar floating-point maximum");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x0B, 1, true)) == simrv::isa::OperationId::UNKNOWN &&
            simrv::pipeline::decoder(encode_op_v(0x0D, 5, true)) ==
                simrv::isa::OperationId::UNKNOWN,
        "vector decoder rejects the former nonstandard min/max encodings");
    expect(simrv::pipeline::decoder(encode_op_v(0x01, 1, true)) ==
               simrv::isa::OperationId::VFREDUSUM_VS,
           "vector table decodes unordered floating-point sum reduction");
    expect(simrv::pipeline::decoder(encode_op_v(0x07, 1, true)) ==
               simrv::isa::OperationId::VFREDMAX_VS,
           "vector table decodes floating-point maximum reduction");
    expect(simrv::pipeline::decoder(encode_op_v(0x31, 1, true)) ==
               simrv::isa::OperationId::VFWREDUSUM_VS,
           "vector table decodes widening unordered floating-point sum reduction");
    expect(simrv::pipeline::decoder(encode_op_v(0x33, 1, true)) ==
               simrv::isa::OperationId::VFWREDOSUM_VS,
           "vector table decodes widening ordered floating-point sum reduction");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x30, 1, true)) == simrv::isa::OperationId::VFWADD_VV,
        "vector table decodes widening floating-point addition");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x36, 5, true)) == simrv::isa::OperationId::VFWSUB_WF,
        "vector table decodes wide-scalar floating-point subtraction");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x38, 1, true)) == simrv::isa::OperationId::VFWMUL_VV,
        "vector table decodes widening floating-point multiplication");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x3C, 1, true)) == simrv::isa::OperationId::VFWMACC_VV,
        "vector table decodes widening fused multiply-accumulate");
    expect(simrv::pipeline::decoder(encode_op_v(0x3F, 5, true)) ==
               simrv::isa::OperationId::VFWNMSAC_VF,
           "vector table decodes scalar widening negative fused multiply-subtract-accumulate");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x28, 1, true)) == simrv::isa::OperationId::VFMADD_VV,
        "vector table decodes destructive floating-point fused multiply-add");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x2F, 5, true)) == simrv::isa::OperationId::VFNMSAC_VF,
        "vector table decodes scalar negative fused multiply-subtract-accumulate");
    expect(
        simrv::pipeline::decoder(encode_op_v(0x1B, 1, true)) == simrv::isa::OperationId::VMFLT_VV,
        "vector table decodes ordered floating-point mask comparison");

    constexpr CSRValue kV = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::V);
    constexpr CSRValue kB = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::B);
    constexpr CSRValue kC = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::C);
    constexpr CSRValue kF = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::F);
    constexpr CSRValue kD = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::D);
    constexpr CSRValue kI = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::I);
    expect(simrv::isa::instruction_enabled_by_misa(kV, simrv::isa::OperationId::VADD_VV),
           "base vector arithmetic requires the V MISA bit");
    expect(!simrv::isa::instruction_enabled_by_misa(kV, simrv::isa::OperationId::VANDN_VV),
           "vector Zvbb operations are disabled when the B subset bit is absent");
    expect(simrv::isa::instruction_enabled_by_misa(kV | kB, simrv::isa::OperationId::VANDN_VV),
           "vector Zvbb operations require both V and the represented B subset");
    expect(!simrv::isa::instruction_enabled_by_misa(kB, simrv::isa::OperationId::VCLMUL_VV),
           "vector Zvbc operations still require the base V extension");
    expect(simrv::isa::instruction_enabled_by_misa(kV | kB, simrv::isa::OperationId::VCLMUL_VV),
           "vector Zvbc operations resolve both represented extension requirements");
    expect(!simrv::isa::instruction_enabled_by_misa(kV, simrv::isa::OperationId::VFADD_VV) &&
               simrv::isa::instruction_enabled_by_misa(kV | kF, simrv::isa::OperationId::VFADD_VV),
           "vector floating-point operations require both V and F");
    expect(!simrv::isa::instruction_enabled_by_misa(kD, simrv::isa::OperationId::FADD_D) &&
               simrv::isa::instruction_enabled_by_misa(kD | kF, simrv::isa::OperationId::FADD_D),
           "double-precision operations require the architectural F dependency");
    expect(
        !simrv::isa::instruction_enabled_by_misa(kC, simrv::isa::OperationId::ADDI, true) &&
            simrv::isa::instruction_enabled_by_misa(kC | kI, simrv::isa::OperationId::ADDI, true),
        "compressed instructions retain the decoded base-operation requirement");
    expect(simrv::pipeline::decoder(encode_op_v(0x10, 2, true, 16)) ==
               simrv::isa::OperationId::VCPOP_M,
           "secondary rs1 vector encoding remains explicit");
    expect(simrv::pipeline::decoder(encode_op_v(0x10, 2, true, 31)) ==
               simrv::isa::OperationId::UNKNOWN,
           "reserved secondary vector encoding remains unknown");
    constexpr std::array<simrv::isa::OperationId, 8> kWholeRegisterMoves = {
        simrv::isa::OperationId::VMV1R_V, simrv::isa::OperationId::VMV2R_V,
        simrv::isa::OperationId::UNKNOWN, simrv::isa::OperationId::VMV4R_V,
        simrv::isa::OperationId::UNKNOWN, simrv::isa::OperationId::UNKNOWN,
        simrv::isa::OperationId::UNKNOWN, simrv::isa::OperationId::VMV8R_V};
    for (unsigned simm5 = 0; simm5 < kWholeRegisterMoves.size(); ++simm5) {
        expect(simrv::pipeline::decoder(encode_op_v(0x27, 3, true, simm5)) ==
                   kWholeRegisterMoves[simm5],
               "whole-register vector move accepts only legal register group sizes");
        expect(simrv::pipeline::decoder(encode_op_v(0x27, 3, false, simm5)) ==
                   simrv::isa::OperationId::UNKNOWN,
               "masked whole-register vector moves are reserved");
    }
    expect(simrv::pipeline::decoder(encode_op_v(0x08, 0, true)) == simrv::isa::OperationId::UNKNOWN,
           "unassigned vector table entries decode as unknown");
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
    expect(!table.may_have_reservations(), "New reservation table has a lock-free empty state");

    table.set_reservation(0, 0x80001000);
    expect(table.may_have_reservations(), "Setting LR marks possible reservations");
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
    expect(table.check_and_clear_reservation(0, 0x80002000), "SC on valid reservation succeeds");
    expect(!table.check_reservation(0, 0x80002000), "Reservation is cleared after successful SC");
    expect(table.check_reservation(1, 0x80003000), "Hart 1's reservation remains intact");

    table.clear_all();
    expect(!table.may_have_reservations(), "clear_all restores the lock-free empty state");
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
    const std::string fdt_text(fdt.begin(), fdt.end());
    expect(fdt_text.find("earlycon=uart8250,mmio,0x10000000,115200n8") != std::string::npos,
           "Linux defaults to the standard 16550 MMIO early console, not legacy SBI console");
    expect(fdt_text.find("earlycon=sbi") == std::string::npos,
           "generated FDT omits the legacy SBI early console selector");
    expect(fdt_text.find("enable-method") != std::string::npos &&
               fdt_text.find("riscv,sbi") != std::string::npos,
           "every generated CPU advertises SBI HSM startup for Linux SMP");
    expect(fdt_text.find("pci@30000000") != std::string::npos,
           "PCIe platform FDT advertises the PCIe root complex");
    expect(fdt_text.find("virtio@10001000") == std::string::npos,
           "PCIe platform FDT does not advertise inactive virtio-MMIO devices");

    config.enable_pcie = false;
    config.enable_mmio = true;
    auto mmio_fdt = simrv::util::FdtGenerator::generate(config);
    const std::string mmio_fdt_text(mmio_fdt.begin(), mmio_fdt.end());
    expect(mmio_fdt_text.find("virtio@10001000") != std::string::npos,
           "MMIO platform FDT advertises the virtio-MMIO disk");
    expect(mmio_fdt_text.find("pci@30000000") == std::string::npos,
           "MMIO platform FDT does not advertise an inactive PCIe root complex");

    config.soc = simrv::core::SoCConfig::virt_mmio();
    config.soc.disable_unlisted_devices = true;
    config.soc.devices = {
        {simrv::core::SoCDeviceKind::Uart, "debug-uart", 0x20000000, 0x200, 17},
        {simrv::core::SoCDeviceKind::VirtioMmioNet, "net1", 0x20001000, 0x2000, 19},
    };
    auto custom_fdt = simrv::util::FdtGenerator::generate(config);
    const std::string custom_fdt_text(custom_fdt.begin(), custom_fdt.end());
    expect(custom_fdt_text.find("serial@20000000") != std::string::npos,
           "custom SoC UART address is reflected in the generated FDT");
    expect(custom_fdt_text.find("virtio@20001000") != std::string::npos,
           "custom SoC VirtIO address is reflected in the generated FDT");
    expect(custom_fdt_text.find("virtio@10001000") == std::string::npos,
           "explicit SoC device policy omits unlisted VirtIO devices from the FDT");
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
    state.pmpcfg[0] =
        simrv::core::pmp::kPmpModeNapot | simrv::core::pmp::kPmpR | simrv::core::pmp::kPmpW;
    state.refresh_pmp_status();

    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Read),
           "PMP entry 0 permits read within NAPOT range");
    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Write),
           "PMP entry 0 permits write within NAPOT range");
    expect(
        !simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Execute),
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
    dcache.insert(test_addr, sample_data.data(), simrv::memory::MesiState::Shared);
    expect(!dcache.last_access_was_hit() && dcache.hit_count() == 0,
           "cache insertion preserves miss accounting and highlight state");

    Word read_val = 0;
    expect(dcache.read(test_addr, read_val, 2), "D-Cache read hits in Branch state");
    expect((read_val & 0xFFFFFFFFU) == 0xDDCCBBAAU, "D-Cache read returned expected data");

    // Write in Branch state should return false (requiring AcquirePerm upgrade)
    expect(!dcache.write(test_addr, 0x12345678, 2),
           "D-Cache write misses in Branch state (upgrade required)");

    // Insert in Trunk state (exclusive/modified)
    dcache.insert(test_addr, sample_data.data(), simrv::memory::MesiState::Exclusive);
    expect(dcache.write(test_addr, 0x12345678, 2), "D-Cache write hits in Trunk state");

    // Probe with invalidation (target: None)
    simrv::memory::TlChannelB probe_req{};
    probe_req.opcode = simrv::memory::TlOpcodeB::ProbeBlock;
    probe_req.cap = simrv::memory::TlCap::ToN;
    probe_req.address = test_addr;

    simrv::memory::TlChannelC probe_resp{};
    std::array<Byte, simrv::cache::DCache::kLineBytes> dirty_buf{};
    expect(dcache.handle_probe(probe_req, probe_resp, dirty_buf),
           "D-Cache probe hits and handles request");
    expect(probe_resp.opcode == simrv::memory::TlOpcodeC::ProbeAckData,
           "Dirty line returns ProbeAckData");
    expect(!dcache.read(test_addr, read_val, 2), "D-Cache line is invalid after probe to None");

    // Test MESI Trunk -> Branch downgrade probe with dirty writeback
    dcache.insert(test_addr, sample_data.data(), simrv::memory::MesiState::Modified);
    simrv::memory::TlChannelB downgrade_req{};
    downgrade_req.opcode = simrv::memory::TlOpcodeB::ProbeBlock;
    downgrade_req.cap = simrv::memory::TlCap::ToB;
    downgrade_req.address = test_addr;

    simrv::memory::TlChannelC downgrade_resp{};
    std::array<Byte, simrv::cache::DCache::kLineBytes> dirty_wb{};
    expect(dcache.handle_probe(downgrade_req, downgrade_resp, dirty_wb),
           "D-Cache probe handles Branch downgrade");
    expect(downgrade_resp.opcode == simrv::memory::TlOpcodeC::ProbeAckData,
           "Dirty line returns ProbeAckData on downgrade");
    expect(dcache.read(test_addr, read_val, 2),
           "D-Cache line is still valid for reads in Branch state");

    // Test I-Cache probe invalidation
    icache.insert(test_addr, sample_data.data(), simrv::memory::MesiState::Shared);
    uint32_t inst_val = 0;
    expect(icache.read(test_addr, inst_val), "I-Cache read hits on inserted line");
    simrv::memory::TlChannelC ic_resp{};
    expect(icache.handle_probe(probe_req, ic_resp), "I-Cache probe hits and handles request");
    expect(!icache.read(test_addr, inst_val), "I-Cache line is invalid after probe to None");
}

void test_tilelink_c_protocol_checker() {
    using namespace simrv::memory;
    TileLinkProtocolChecker checker;

    TlChannelA get{
        .opcode = TlOpcodeA::Get, .size = 2, .source = 7, .address = 0x80000004, .mask = 0xf0};
    expect(checker.accept_a(get).has_value(), "checker accepts aligned masked Get");
    expect(!checker.accept_a(get).has_value(), "checker rejects live source reuse");
    TlChannelD get_response{
        .opcode = TlOpcodeD::AccessAckData, .size = 2, .source = 7, .data = 0x1234};
    expect(checker.accept_d(get_response).has_value(), "checker matches AccessAckData to Get");

    TlChannelA bad_mask = get;
    bad_mask.source = 8;
    bad_mask.mask = 0x0f;
    expect(!checker.accept_a(bad_mask).has_value(), "checker rejects a lane-inconsistent mask");

    TlChannelA acquire{.opcode = TlOpcodeA::AcquireBlock,
                       .grow = TlGrow::NtoB,
                       .size = kTlBlockSize,
                       .source = 9,
                       .hart = 0,
                       .address = 0x80000020};
    expect(checker.accept_a(acquire).has_value(), "checker accepts one-block AcquireBlock");
    TlChannelD grant{.opcode = TlOpcodeD::GrantData,
                     .cap = TlCap::ToT,
                     .size = kTlBlockSize,
                     .source = 9,
                     .sink = 3};
    expect(checker.accept_d(grant).has_value(), "checker accepts a matching unique GrantData");
    expect(checker.outstanding_sinks() == 1, "grant sink remains live until GrantAck");
    expect(checker.accept_e(TlChannelE{.sink = 3}).has_value(), "checker accepts GrantAck");
    expect(!checker.accept_e(TlChannelE{.sink = 3}).has_value(),
           "checker rejects duplicate GrantAck");

    expect(permission_for(MesiState::Invalid) == TlPermission::None,
           "MESI Invalid maps to TileLink None");
    expect(permission_for(MesiState::Shared) == TlPermission::Branch,
           "MESI Shared maps to TileLink Branch");
    expect(permission_for(MesiState::Exclusive) == TlPermission::Trunk &&
               permission_for(MesiState::Modified) == TlPermission::Trunk,
           "MESI Exclusive and Modified map to TileLink Trunk");

    // Channel B Probe checking
    TlChannelB probe_good{.opcode = TlOpcodeB::ProbeBlock,
                          .cap = TlCap::ToN,
                          .size = kTlBlockSize,
                          .source = 10,
                          .address = 0x80000040};
    expect(checker.accept_b(probe_good).has_value(), "checker accepts aligned block ProbeBlock");
    expect(checker.outstanding_probes() == 1, "checker tracks outstanding probe");

    TlChannelB probe_bad_align = probe_good;
    probe_bad_align.address = 0x80000043;
    expect(!checker.accept_b(probe_bad_align).has_value(),
           "checker rejects unaligned probe address");

    // Channel C ProbeAck completes probe
    TlChannelC probe_ack{.opcode = TlOpcodeC::ProbeAckData,
                         .report = TlReport::TtoN,
                         .size = kTlBlockSize,
                         .source = 10,
                         .address = 0x80000040};
    expect(checker.accept_c(probe_ack).has_value(), "checker accepts ProbeAckData");
    expect(checker.outstanding_probes() == 0, "ProbeAck clears outstanding probe");

    // Channel C Release and Channel D ReleaseAck
    TlChannelC release_req{.opcode = TlOpcodeC::ReleaseData,
                           .report = TlReport::TtoN,
                           .size = kTlBlockSize,
                           .source = 12,
                           .address = 0x80000060};
    expect(checker.accept_c(release_req).has_value(), "checker accepts valid ReleaseData");
    expect(checker.outstanding_releases() == 1, "checker tracks outstanding release");
    TlChannelD rel_ack{.opcode = TlOpcodeD::ReleaseAck, .size = kTlBlockSize, .source = 12};
    expect(checker.accept_d(rel_ack).has_value(), "checker matches ReleaseAck to Release");
    expect(checker.outstanding_releases() == 0, "ReleaseAck clears outstanding release");

    // String conversion verification
    expect(to_string(TlOpcodeA::AcquireBlock) == "AcquireBlock", "to_string(AcquireBlock)");
    expect(to_string(TlOpcodeA::AcquirePerm) == "AcquirePerm", "to_string(AcquirePerm)");
    expect(to_string(TlOpcodeB::ProbeBlock) == "ProbeBlock", "to_string(ProbeBlock)");
    expect(to_string(TlOpcodeC::ProbeAckData) == "ProbeAckData", "to_string(ProbeAckData)");
    expect(to_string(TlOpcodeD::GrantData) == "GrantData", "to_string(GrantData)");
    expect(to_string(TlOpcodeE::GrantAck) == "GrantAck", "to_string(GrantAck)");
    expect(to_string(MesiState::Modified) == "Modified", "to_string(Modified)");
    expect(to_string(TlGrow::BtoT) == "BtoT", "to_string(BtoT)");
    expect(to_string(TlCap::ToT) == "ToT", "to_string(ToT)");
    expect(to_string(TlReport::TtoN) == "TtoN", "to_string(TtoN)");
}

void test_strong_semantic_types() {
    // Compile-time zero memory overhead verification
    static_assert(sizeof(PhysAddr) == sizeof(Word));
    static_assert(sizeof(VirtAddr) == sizeof(Word));
    static_assert(sizeof(HartId) == sizeof(uint32_t));
    static_assert(sizeof(CsrNumber) == sizeof(uint16_t));
    static_assert(StrongAddress<PhysAddr>);
    static_assert(StrongAddress<VirtAddr>);
    static_assert(!StrongAddress<Word>);

    // PhysAddr arithmetic and bitwise testing
    PhysAddr p1{0x80000000ULL};
    expect(p1.raw() == 0x80000000ULL, "PhysAddr raw value matches");
    expect(p1 == 0x80000000ULL, "PhysAddr equality with integer literal");

    PhysAddr p2 = p1 + 0x100U;
    expect(p2.val == 0x80000100ULL, "PhysAddr addition with unsigned integer");
    expect((p2 - p1) == 0x100, "PhysAddr subtraction yields SignedWord offset");

    PhysAddr p3 = p2 & ~0xFFFULL;
    expect(p3 == p1, "PhysAddr bitwise AND with mask");

    // VirtAddr testing
    VirtAddr v1{0x10000ULL};
    VirtAddr v2 = v1 + 0x20U;
    expect(v2.raw() == 0x10020ULL, "VirtAddr addition");
    expect((v2 - v1) == 0x20, "VirtAddr subtraction difference");
    expect(v1 < v2, "VirtAddr comparison operators");

    // HartId testing
    HartId h1{3U};
    expect(h1.value() == 3U, "HartId value getter");
    expect((1ULL << h1) == (1ULL << 3U), "HartId bitwise shift operator");
    expect(h1 == 3U, "HartId equality with integer");

    // CsrNumber testing
    CsrNumber mstatus_csr{0x300U};
    expect(!mstatus_csr.is_read_only(), "mstatus CSR (0x300) is writable");
    expect(mstatus_csr.privilege_level() == PrivilegeLevel::Machine,
           "mstatus CSR privilege level is Machine");

    CsrNumber cycle_csr{0xC00U};
    expect(cycle_csr.is_read_only(), "cycle CSR (0xC00) is read-only");
    expect(cycle_csr.privilege_level() == PrivilegeLevel::User,
           "cycle CSR privilege level is User");

    // Register Identifier concept and classifications
    static_assert(simrv::xlen::RegisterIdentifier<RegId>, "RegId satisfies RegisterIdentifier");
    static_assert(simrv::xlen::RegisterIdentifier<FpRegId>, "FpRegId satisfies RegisterIdentifier");
    static_assert(simrv::xlen::RegisterIdentifier<VecRegId>,
                  "VecRegId satisfies RegisterIdentifier");
    expect(std::to_underlying(VecRegId::V5) == 5, "VecRegId underlying value");

    // MemoryGeometry with PhysAddr
    simrv::core::MemoryGeometry geom{.dram_base = 0x80000000ULL, .dram_size = 0x10000000ULL};
    expect(geom.contains(PhysAddr{0x80001000ULL}), "MemoryGeometry contains PhysAddr within DRAM");
    expect(!geom.contains(PhysAddr{0x10000000ULL}), "MemoryGeometry rejects PhysAddr outside DRAM");

    // std::format and std::hash validation
    const auto formatted_p = std::format("0x{:x}", p1);
    expect(formatted_p.find("80000000") != std::string::npos,
           "std::format specialization formats PhysAddr as hex integer");

    const auto hash_val = std::hash<PhysAddr>{}(p1);
    expect(hash_val == std::hash<Word>{}(p1.val),
           "std::hash<PhysAddr> matches underlying Word hash");
}

void test_cache_hierarchy_semantics() {
    // Test L2 Cache
    simrv::cache::L2Cache l2{};
    std::array<Byte, simrv::cache::L2Cache::kLineBytes> fill_data{};
    fill_data[0] = static_cast<Byte>(0xAA);
    fill_data[31] = static_cast<Byte>(0x55);

    const Address test_addr = 0x80002000;
    std::array<Byte, simrv::cache::L2Cache::kLineBytes> read_buf{};
    expect(!l2.read_line(test_addr, read_buf), "L2 read misses on empty cache");

    l2.write_line(test_addr, fill_data, simrv::memory::MesiState::Exclusive);
    expect(l2.read_line(test_addr, read_buf), "L2 read hits after line insertion");
    expect(read_buf[0] == static_cast<Byte>(0xAA), "L2 read data matches written payload");
    expect(read_buf[31] == static_cast<Byte>(0x55), "L2 read data matches tail byte");

    // Test L3 Cache (LLC)
    simrv::cache::L3Cache l3{};
    simrv::memory::MesiState state = simrv::memory::MesiState::Invalid;
    expect(!l3.lookup_line(test_addr, read_buf, state), "L3 lookup misses on empty cache");

    l3.write_line(test_addr, fill_data, simrv::memory::MesiState::Modified);
    expect(l3.lookup_line(test_addr, read_buf, state), "L3 lookup hits after write_line");
    expect(state == simrv::memory::MesiState::Modified, "L3 line state matches written state");
    expect(read_buf[0] == static_cast<Byte>(0xAA), "L3 line payload preserved");

    l3.invalidate_line(test_addr);
    expect(!l3.lookup_line(test_addr, read_buf, state), "L3 lookup misses after invalidation");
}

}  // namespace

int main() {
    test_strong_semantic_types();
    test_cache_hierarchy_semantics();
    test_unaligned_host_access();
    test_decode_cache_compact_round_robin();
    test_hosted_rv32_cached_shift_width();
    test_compressed_instruction_decode_and_flush();
    test_selective_tlb_and_decode_cache_flush();
    test_tlb_and_decode_cache_deduplication();
    test_runtime_ram_view();
    test_mmio_ranges();
    test_physical_range_validation();
    test_csr_summary_and_presence_rules();
    test_sbi_hart_masks();
    test_vector_length_bytes();
    test_vector_exception_propagation_and_status();
    test_vector_compute_register_group_legality();
    test_vector_mask_and_fault_only_first_memory();
    test_vector_bulk_memory_operations();
    test_vector_integer_remainder_and_reverse_subtract();
    test_vector_gather();
    test_vector_configuration_large_avl();
    test_vector_configuration_preserved_vl();
    test_vector_scalar_move_element_zero();
    test_vector_scalar_extension();
    test_vector_f_only_scalar_fp();
    test_vector_fixed_point_average();
    test_vector_fixed_point_rounding_extremes();
    test_vector_floating_arithmetic();
    test_exception_delegation_mask();
    test_satp_modes();
    test_named_misa_profiles();
    test_sv32_page_walk();
    test_sv39_reserved_pte_bits();
    test_atomic_alignment();
    test_atomic_decode_legality();
    test_fp_decode_legality();
    test_vector_decode_tables();
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
    test_tilelink_c_protocol_checker();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "Core semantic tests passed\n";
    return EXIT_SUCCESS;
}
