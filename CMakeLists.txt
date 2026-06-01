cmake_minimum_required(VERSION 3.20)

project(SimRV LANGUAGES CXX)

enable_testing()

set(SIMRV_VERSION "2.0.0-alpha.5")
set(SIMRV_GIT_BRANCH "unknown")
set(SIMRV_GIT_SHA "unknown")

# Prefer GitHub CI metadata when available, otherwise fall back to local git state.
if(DEFINED ENV{GITHUB_REF_NAME})
  set(SIMRV_GIT_BRANCH "$ENV{GITHUB_REF_NAME}")
endif()

if(DEFINED ENV{GITHUB_SHA})
  string(SUBSTRING "$ENV{GITHUB_SHA}" 0 12 SIMRV_GIT_SHA)
endif()

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  if(SIMRV_GIT_BRANCH STREQUAL "unknown")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE SIMRV_BRANCH_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(SIMRV_BRANCH_OUTPUT)
      set(SIMRV_GIT_BRANCH "${SIMRV_BRANCH_OUTPUT}")
    endif()
  endif()

  if(SIMRV_GIT_SHA STREQUAL "unknown")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE SIMRV_SHA_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(SIMRV_SHA_OUTPUT)
      set(SIMRV_GIT_SHA "${SIMRV_SHA_OUTPUT}")
    endif()
  endif()
endif()

message(STATUS "SimRV version: ${SIMRV_VERSION} (${SIMRV_GIT_BRANCH}@${SIMRV_GIT_SHA})")

set(SIMRV_PROJECT_NAME "SimRV")
set(SIMRV_PROJECT_DESCRIPTION "SimCore/RISC-V Functional Simulator")

configure_file(
  ${CMAKE_SOURCE_DIR}/include/simrv/core/BuildInfo.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/simrv/core/BuildInfo.hpp
  @ONLY
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_CLANG_TIDY clang-tidy)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT "cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not support required C++23 features")
endif()

# XLEN option (32 or 64) – compile-time selection
option(SIMRV_XLEN "Target XLEN (32 or 64)" 32)
set_property(CACHE SIMRV_XLEN PROPERTY STRINGS 32 64)
if(NOT SIMRV_XLEN STREQUAL "32" AND NOT SIMRV_XLEN STREQUAL "64")
  message(FATAL_ERROR "SIMRV_XLEN must be 32 or 64 (got: ${SIMRV_XLEN})")
endif()


set(SIMRV_DRAM_SIZE_MB "256" CACHE STRING "DRAM size in megabytes (must be power of 2)")
set(SIMRV_DISK_SIZE_MB "256" CACHE STRING "Disk size in megabytes")
set(SIMRV_CORE_COUNT "1" CACHE STRING "Number of CPU cores (experimental)")

add_executable(SimRV
  src/Main.cpp
  src/core/Machine.cpp
  src/core/MachineInit.cpp
  src/core/StateControl.cpp
  src/core/CsrFile.cpp
  src/core/Tracer.cpp
  src/core/Sbi.cpp
  src/core/Tlb.cpp
  src/decode/Decoder.cpp
  src/execute/ExecuteUnitInt.cpp
  src/execute/ExecuteUnitFloat.cpp
  src/execute/ExecuteUnit.cpp
  src/core/StageIF.cpp
  src/core/StageID.cpp
  src/core/StageEX.cpp
  src/core/StageMEM.cpp
  src/core/StageWB.cpp
  src/core/StageCommit.cpp
  src/core/Cpu.cpp
  src/cache/ICache.cpp
  src/cache/DCache.cpp
  src/memory/MemoryAccess.cpp
  src/memory/MemorySubsystem.cpp
  src/memory/Mmu.cpp
  src/memory/TileLinkBus.cpp
  src/device/Console.cpp
  src/device/Disk.cpp
  src/device/VirtioDevice.cpp
  src/device/Rtc.cpp
  src/device/Uart.cpp
  src/device/Tui.cpp
)

target_compile_features(SimRV PRIVATE cxx_std_23)

target_compile_definitions(SimRV PRIVATE
  SIMRV_XLEN=${SIMRV_XLEN}
  SIMRV_DRAM_SIZE_MB=${SIMRV_DRAM_SIZE_MB}
  SIMRV_DISK_SIZE_MB=${SIMRV_DISK_SIZE_MB}
  SIMRV_CORE_COUNT=${SIMRV_CORE_COUNT}
)

target_include_directories(SimRV PRIVATE
  include
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_options(SimRV PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

# Match prior optimization defaults for single-config generators.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(SimRV PRIVATE -O2)
endif()

set(ISA_SMOKE_RV32GC
  "rv32ui-p-add,rv32ui-p-addi,rv32ui-p-and,rv32ui-p-beq,rv32ui-p-bne,rv32ui-p-bltu,rv32ui-p-bgeu,rv32ui-p-lb,rv32ui-p-lbu,rv32ui-p-lh,rv32ui-p-lhu,rv32ui-p-lw,rv32ui-p-sw,rv32ui-p-sll,rv32ui-p-slt,rv32ui-p-sltu,rv32ui-p-sra,rv32ui-p-srl,rv32ui-p-sub,rv32ui-p-xor,rv32ui-p-or,rv32um-p-mul,rv32um-p-mulh,rv32um-p-mulhu,rv32um-p-div,rv32um-p-divu,rv32um-p-rem,rv32um-p-remu,rv32ua-p-amoadd_w,rv32ua-p-amomax_w,rv32ua-p-amomaxu_w,rv32ua-p-amomin_w,rv32ua-p-amominu_w,rv32ua-p-amoor_w,rv32ua-p-amoswap_w,rv32ua-p-amoxor_w,rv32ua-p-lrsc"
)

set(ISA_SMOKE_RV64GC
  "rv64ui-p-add,rv64ui-p-addi,rv64ui-p-and,rv64ui-p-auipc,rv64ui-p-beq,rv64ui-p-bge,rv64ui-p-bgeu,rv64ui-p-blt,rv64ui-p-bltu,rv64ui-p-bne,rv64ui-p-jal,rv64ui-p-jalr,rv64ui-p-lb,rv64ui-p-lbu,rv64ui-p-lh,rv64ui-p-lhu,rv64ui-p-lw,rv64ui-p-lwu,rv64ui-p-or,rv64ui-p-sb,rv64ui-p-sd,rv64ui-p-sh,rv64ui-p-sw,rv64ui-p-sll,rv64ui-p-slli,rv64ui-p-slt,rv64ui-p-sltu,rv64ui-p-sra,rv64ui-p-srai,rv64ui-p-srl,rv64ui-p-srli,rv64ui-p-sub,rv64ui-p-xor,rv64um-p-mul,rv64um-p-mulh,rv64um-p-mulhsu,rv64um-p-mulhu,rv64um-p-div,rv64um-p-divu,rv64um-p-rem,rv64um-p-remu,rv64ua-p-amoadd_w,rv64ua-p-amomax_w,rv64ua-p-amomaxu_w,rv64ua-p-amomin_w,rv64ua-p-amominu_w,rv64ua-p-amoor_w,rv64ua-p-amoswap_w,rv64ua-p-amoxor_w,rv64ua-p-lrsc"
)

add_test(NAME regress
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:SimRV> bash ${CMAKE_SOURCE_DIR}/scripts/regression.sh
)
set_tests_properties(regress PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  LABELS "gate;regress"
)

add_test(NAME isa-smoke-rv32gc
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:SimRV> SIMRV_ISA_LIST=${ISA_SMOKE_RV32GC} bash ${CMAKE_SOURCE_DIR}/scripts/run-isa-tests.sh
)
set_tests_properties(isa-smoke-rv32gc PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  LABELS "gate;isa;rv32gc"
  SKIP_RETURN_CODE 2
)

add_test(NAME benchmark
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_BIN=$<TARGET_FILE:SimRV> SIMRV_BENCH_ITERS=5 bash ${CMAKE_SOURCE_DIR}/scripts/benchmark.sh
)
set_tests_properties(benchmark PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  LABELS "benchmark;perf"
  SKIP_RETURN_CODE 2
)

add_test(NAME phase2-gate
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:SimRV> bash ${CMAKE_SOURCE_DIR}/scripts/phase2-gate.sh
)
set_tests_properties(phase2-gate PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  LABELS "gate;phase2;comprehensive"
  SKIP_RETURN_CODE 2
)

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
    USES_TERMINAL
  )
else()
  message(STATUS "Doxygen not found: 'docs' target will be unavailable")
endif()

# Linux image building for Phase 2 validation
add_custom_target(linux-images
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_USE_LOCAL_SCRATCH=1 bash ${CMAKE_SOURCE_DIR}/scripts/build-linux-image.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Building RV32GC Linux kernel and rootfs images for Phase 2 testing"
  VERBATIM
  USES_TERMINAL
)

# Comprehensive Phase 2 validation gate with Linux image support
add_custom_target(phase2-gate
  COMMAND bash -c "source ${CMAKE_SOURCE_DIR}/linux-images/setup.sh 2>/dev/null || true; ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:SimRV> bash ${CMAKE_SOURCE_DIR}/scripts/phase2-gate.sh"
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running Phase 2 comprehensive validation gate (regress, app, linux, isa tests)"
  VERBATIM
  USES_TERMINAL
)

add_test(NAME phase3-gate
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/phase3-gate.sh
)
set_tests_properties(phase3-gate PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  LABELS "gate;phase3;isa;rv32gc;rv64gc"
  SKIP_RETURN_CODE 2
)

add_custom_target(phase3-gate
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/phase3-gate.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running Phase 3 validation gate (RV32 + RV64 ISA smoke tests)"
  VERBATIM
  USES_TERMINAL
)

# Phase 1 gates (for reference)
add_custom_target(phase1-gate
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -R "^regress$"
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running Phase 1 validation gate"
  VERBATIM
  USES_TERMINAL
)
