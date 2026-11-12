/**
 * @file Microbenchmarks.cpp
 * @brief High-precision native microbenchmarks for SimRV architectural subsystems.
 */

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

#include "simrv/cache/BaseCache.hpp"
#include "simrv/core/DecodeCache.hpp"
#include "simrv/memory/ReservationTable.hpp"
#include "simrv/pipeline/BranchPredictor.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "third_party/nanobench.h"

namespace {

void benchmark_decoder(ankerl::nanobench::Bench& bench) {
    // Representative RISC-V instructions:
    // 0: addi a0, zero, 10
    // 1: add a0, a0, a1
    // 2: ld a0, 0(a0)
    // 3: sd a1, 0(a0)
    // 4: beq a0, a1, 8
    // 5: jal zero, 8
    // 6: lui a0, 1
    // 7: auipc a0, 0
    constexpr std::array<simrv::xlen::Instruction, 8> kInstructions = {
        0x00a00513U, 0x00b50533U, 0x00053503U, 0x00b53023U,
        0x00b50463U, 0x0080006fU, 0x00001537U, 0x00000517U,
    };

    size_t idx = 0;
    bench.title("Instruction Decoder")
        .unit("instruction")
        .batch(kInstructions.size())
        .run("Decoder::extract_fields", [&]() {
            for (const auto raw : kInstructions) {
                simrv::pipeline::Decoder decoder(raw);
                auto op = decoder.opcode();
                auto rd = decoder.rd();
                auto rs1 = decoder.rs1();
                auto rs2 = decoder.rs2();
                auto funct3 = decoder.funct3();
                auto imm_i = decoder.imm_i();
                auto imm_b = decoder.imm_b();
                auto imm_j = decoder.imm_j();
                ankerl::nanobench::doNotOptimizeAway(op);
                ankerl::nanobench::doNotOptimizeAway(rd);
                ankerl::nanobench::doNotOptimizeAway(rs1);
                ankerl::nanobench::doNotOptimizeAway(rs2);
                ankerl::nanobench::doNotOptimizeAway(funct3);
                ankerl::nanobench::doNotOptimizeAway(imm_i);
                ankerl::nanobench::doNotOptimizeAway(imm_b);
                ankerl::nanobench::doNotOptimizeAway(imm_j);
            }
            ++idx;
        });
}

void benchmark_decode_cache(ankerl::nanobench::Bench& bench) {
    simrv::pipeline::DecodedInstruction decoded{};
    decoded.cpc = simrv::xlen::VirtAddr{0x80000100U};
    decoded.ir = 0x00b50533U;  // add a0, a0, a1
    decoded.opcode = simrv::isa::Opcode::Op;
    decoded.op_id = simrv::isa::ADD;
    decoded.rd = simrv::xlen::RegId::A0;
    decoded.rs1 = simrv::xlen::RegId::A0;
    decoded.rs2 = simrv::xlen::RegId::A1;
    decoded.imm = 0;

    simrv::core::CachedOp entry{};

    bench.title("Decode Cache").unit("entry").run("CachedOp::copy_from", [&]() {
        entry.copy_from(decoded);
        ankerl::nanobench::doNotOptimizeAway(entry);
    });
}

void benchmark_reservation_table(ankerl::nanobench::Bench& bench) {
    simrv::memory::ReservationTable table;
    constexpr simrv::xlen::HartId kHart0{0};
    constexpr simrv::xlen::HartId kHart1{1};
    const simrv::xlen::PhysAddr addr0{0x80001000U};
    const simrv::xlen::PhysAddr addr1{0x80002000U};

    bench.title("LR/SC Reservation Table (Boost FlatMap)")
        .unit("op")
        .run("ReservationTable::set_and_clear", [&]() {
            table.set_reservation(kHart0, addr0);
            table.set_reservation(kHart1, addr1);
            bool c0 = table.check_and_clear_reservation(kHart0, addr0);
            bool c1 = table.check_and_clear_reservation(kHart1, addr1);
            ankerl::nanobench::doNotOptimizeAway(c0);
            ankerl::nanobench::doNotOptimizeAway(c1);
        });

    bench.run("ReservationTable::invalidate_matching", [&]() {
        table.set_reservation(kHart0, addr0);
        table.invalidate_matching(addr0, kHart1);
        bool c0 = table.check_and_clear_reservation(kHart0, addr0);
        ankerl::nanobench::doNotOptimizeAway(c0);
    });
}

void benchmark_cache_tag_lookup(ankerl::nanobench::Bench& bench) {
    simrv::cache::BaseCache<32, 64, 4> cache;
    const simrv::xlen::PhysAddr addr0{0x80000000U};
    const simrv::xlen::PhysAddr addr1{0x80000040U};
    const simrv::xlen::PhysAddr addr2{0x80000080U};
    const simrv::xlen::PhysAddr addr3{0x800000C0U};

    const std::array<simrv::xlen::Byte, 64> dummy_data{};
    // Allocate lines into cache
    cache.insert(addr0, dummy_data.data(), simrv::memory::MesiState::Exclusive);
    cache.insert(addr1, dummy_data.data(), simrv::memory::MesiState::Exclusive);
    cache.insert(addr2, dummy_data.data(), simrv::memory::MesiState::Exclusive);
    cache.insert(addr3, dummy_data.data(), simrv::memory::MesiState::Exclusive);

    bench.title("L1 Cache Tag Lookup").unit("lookup").run("BaseCache::line_state (hit)", [&]() {
        auto res0 = cache.line_state(addr0);
        auto res1 = cache.line_state(addr1);
        auto res2 = cache.line_state(addr2);
        auto res3 = cache.line_state(addr3);
        ankerl::nanobench::doNotOptimizeAway(res0);
        ankerl::nanobench::doNotOptimizeAway(res1);
        ankerl::nanobench::doNotOptimizeAway(res2);
        ankerl::nanobench::doNotOptimizeAway(res3);
    });
}

void benchmark_branch_predictor(ankerl::nanobench::Bench& bench) {
    simrv::pipeline::BranchPredictorConfig config{
        .type = simrv::pipeline::BranchPredictorType::GShare,
        .bht_entries = 1024,
        .btb_entries = 256,
        .ras_entries = 16,
        .ghr_bits = 10,
    };
    simrv::pipeline::BranchPredictor bpred(config);

    simrv::pipeline::DecodedInstruction beq_inst{};
    beq_inst.opcode = simrv::isa::Opcode::Branch;
    beq_inst.cpc = simrv::xlen::VirtAddr{0x80000020U};
    beq_inst.imm = 16;

    simrv::pipeline::BranchPrediction pred{};

    bench.title("Branch Predictor (GShare)")
        .unit("prediction")
        .run("BranchPredictor::predict_and_update", [&]() {
            bpred.predict(beq_inst.cpc, beq_inst, pred);
            const simrv::pipeline::BranchFeedback feedback{
                .pc = simrv::xlen::VirtAddr{beq_inst.cpc.raw()},
                .actual_taken = true,
                .actual_target = beq_inst.cpc + beq_inst.imm,
                .opcode = beq_inst.opcode,
                .op_id = beq_inst.op_id,
                .prediction = pred,
            };
            bpred.update(feedback);
            ankerl::nanobench::doNotOptimizeAway(pred);
        });
}

}  // namespace

int main(int argc, char* argv[]) {
    ankerl::nanobench::Bench bench;
    bench.epochs(10).warmup(3).relative(true);

    if (argc > 1 && std::string_view(argv[1]) == "--json") {
        bench.render(ankerl::nanobench::templates::json(), std::cout);
    }

    benchmark_decoder(bench);
    benchmark_decode_cache(bench);
    benchmark_reservation_table(bench);
    benchmark_cache_tag_lookup(bench);
    benchmark_branch_predictor(bench);

    return 0;
}
