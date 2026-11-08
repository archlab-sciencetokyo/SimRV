/**
 * @file E2ETests.cpp
 * @brief End-to-End (E2E) Native C++23 testing suite using Boost.UT.
 *
 * Replaces the Python 4-tier testing suites (E2ETier1 through E2ETier4) with
 * high-speed, compiled native test scenarios.
 */
#include <boost/ut.hpp>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "E2EHelper.hpp"
#include "TestHelper.hpp"

int main() {
    using namespace boost::ut;

    constexpr int kXlen = SIMRV_XLEN;
    const auto root = simrv::test::resolve_repo_root();
    const auto simrv_bin = simrv::test::resolve_simrv_binary(kXlen);
    const auto hello_elf = simrv::test::find_example_elf("hello", kXlen);
    const auto arithmetic_elf = simrv::test::find_example_elf("arithmetic", kXlen);
    const auto traps_elf = simrv::test::find_example_elf("traps", kXlen);

    auto run = [&](const std::vector<std::string>& args) {
        return simrv::test::run_simrv(args, kXlen);
    };

    // =========================================================================
    // Tier 1: Feature Coverage (Core Domain Typing, Execution, Subcommands)
    // =========================================================================

    "tier1: version and isa info reporting"_test = [&] {
        auto res_ver = run({"--version"});
        expect(res_ver.exit_code == 0_i);
        expect(res_ver.stdout_output.contains("RV"));

        auto res_isa = run({"--isa-info"});
        expect(res_isa.exit_code == 0_i);
        expect(res_isa.stdout_output.contains("RV64GCBV") ||
               res_isa.stdout_output.contains("RV32GCBV"));
    };

    "tier1: built-in third-party notices"_test = [&] {
        auto res = run({"--notices"});
        expect(res.exit_code == 0_i);
        expect(res.stdout_output.contains("Boost.UT Testing Framework"));
        expect(res.stdout_output.contains("Boost Software License"));
    };

    "tier1: step counting and execution summary"_test = [&] {
        if (!hello_elf) return;
        auto res = run({"--cli", "--baremetal", "-m", hello_elf->string(), "--steps", "500"});
        expect(res.exit_code == 0_i);
        expect(res.stdout_output.contains("Elapsed cycles"));
        expect(res.stdout_output.contains("Executed instructions"));
        expect(res.stdout_output.contains("500"));
    };

    "tier1: execution modes"_test = [&](const std::string& mode) {
        if (!hello_elf) return;
        auto res = run(
            {"--cli", "--baremetal", "-m", hello_elf->string(), "--mode", mode, "--steps", "200"});
        expect(res.exit_code == 0_i);
    } | std::vector<std::string>{"fast", "detailed", "cycle-accurate"};

    "tier1: pipeline models"_test = [&](const std::string& pipe) {
        if (!hello_elf) return;
        auto res = run({"--cli", "--baremetal", "-m", hello_elf->string(), "--pipeline", pipe,
                        "--steps", "150"});
        expect(res.exit_code == 0_i);
    } | std::vector<std::string>{"5stage", "3stage"};

    "tier1: multi-hart configuration"_test = [&](int harts) {
        if (!hello_elf) return;
        auto res = run({"--cli", "--baremetal", "-m", hello_elf->string(), "--harts",
                        std::to_string(harts), "--steps", "200"});
        expect(res.exit_code == 0_i);
    } | std::vector<int>{1, 2, 4};

    "tier1: mission tools subcommand"_test = [&] {
        simrv::test::ScopedTempDir temp_dir("mission_e2e");
        auto mission_file = temp_dir.path() / "test_lesson.mission";

        auto create_res = run({"--create-mission", mission_file.string()});
        expect(create_res.exit_code == 0_i);
        expect(std::filesystem::is_regular_file(mission_file));

        auto check_res = run({"--check-mission", mission_file.string()});
        expect(check_res.exit_code == 0_i);
        expect(check_res.stdout_output.contains("Mission 'lesson'"));
        expect(check_res.stdout_output.contains("Observe Instruction Flow"));
    };

    "tier1: uefi disk generator subcommand"_test = [&] {
        if (!hello_elf) return;
        simrv::test::ScopedTempDir temp_dir("disk_e2e");
        auto disk_file = temp_dir.path() / "esp.img";

        auto res = run({"--create-disk", disk_file.string(), "-m", hello_elf->string()});
        expect(res.exit_code == 0_i);
        expect(std::filesystem::is_regular_file(disk_file));
        expect(std::filesystem::file_size(disk_file) >= 64 * 1024 * 1024);
    };

    // =========================================================================
    // Tier 2: Boundary Tests (Invalid CLI, Conflicts, Step Limits)
    // =========================================================================

    "tier2: invalid cli options"_test = [&] {
        auto res = run({"-Z"});
        expect(res.exit_code != 0_i);
        expect(res.stderr_output.contains("unknown option"));
    };

    "tier2: conflicting cli debug options"_test = [&] {
        auto res_gdb_tui = run({"--gdb", "--tui"});
        expect(res_gdb_tui.exit_code != 0_i);
        expect(res_gdb_tui.stderr_output.contains("mutually exclusive"));

        auto res_gdb_lockstep = run({"--gdb", "--lockstep"});
        expect(res_gdb_lockstep.exit_code != 0_i);
        expect(res_gdb_lockstep.stderr_output.contains("mutually exclusive"));
    };

    "tier2: missing argument values"_test = [&] {
        auto res = run({"--harts"});
        expect(res.exit_code != 0_i);
    };

    "tier2: zero steps limit"_test = [&] {
        if (!hello_elf) return;
        auto res = run({"--cli", "--baremetal", "-m", hello_elf->string(), "--steps", "0"});
        expect(res.exit_code == 0_i);
    };

    // =========================================================================
    // Tier 3: Combinations (Branch Predictors, Caches, DRAM Sizes)
    // =========================================================================

    "tier3: branch predictor combinations"_test = [&](const std::string& bpred) {
        if (!hello_elf) return;
        auto res = run({"--cli", "--baremetal", "-m", hello_elf->string(), "--bpred", bpred,
                        "--steps", "250"});
        expect(res.exit_code == 0_i);
    } | std::vector<std::string>{"static", "bimodal", "gshare"};

    "tier3: dram size variations"_test = [&](const std::string& dram_sz) {
        if (!hello_elf) return;
        auto res = run({"--cli", "--baremetal", "-m", hello_elf->string(), "--dram-size", dram_sz,
                        "--steps", "150"});
        expect(res.exit_code == 0_i);
    } | std::vector<std::string>{"64MiB", "128MiB", "256MiB", "512MiB"};

    // =========================================================================
    // Tier 4: Scenarios (Program Completion & Trap Semantics)
    // =========================================================================

    "tier4: arithmetic benchmark execution"_test = [&] {
        if (!arithmetic_elf) return;
        auto res = run({"--cli", "--baremetal", "-m", arithmetic_elf->string(), "--steps", "2000"});
        expect(res.exit_code == 0_i);
        expect(res.stdout_output.contains("Process exit status      :                      0") ||
               res.stdout_output.contains("Executed instructions"));
    };

    "tier4: traps handling execution"_test = [&] {
        if (!traps_elf) return;
        auto res = run({"--cli", "--baremetal", "-m", traps_elf->string(), "--steps", "2000"});
        expect(res.exit_code == 0_i);
    };

    return 0;
}
