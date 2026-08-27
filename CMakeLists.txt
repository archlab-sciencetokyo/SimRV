cmake_minimum_required(VERSION 3.31)

project(SimRV VERSION 2.1.0 LANGUAGES C CXX)

include(CTest)
option(SIMRV_ENABLE_TOOLING "Enable release, documentation, and formatting helper targets" OFF)
option(SIMRV_ENABLE_LINUX_TOOLS "Enable Linux image build and launch helper targets" OFF)
if(BUILD_TESTING OR SIMRV_ENABLE_TOOLING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(PYTHON_BIN "${Python3_EXECUTABLE}")
endif()

set(SIMRV_VERSION "2.1.0-alpha.1")
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

option(SIMRV_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)

option(SIMRV_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
option(SIMRV_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SIMRV_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SIMRV_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(SIMRV_NATIVE_HOST_OPTIMIZATIONS
       "Tune a Release build for the host CPU; do not use for redistributable binaries" OFF)
option(SIMRV_X86_64_V3_BASELINE
       "Use the x86-64-v3 ISA baseline for non-native x86-64 Release builds" ON)
set(SIMRV_VECTOR_TESTS_DIR "" CACHE PATH "Path to a built chipsalliance/riscv-vector-tests checkout")

if(SIMRV_ENABLE_TSAN AND (SIMRV_ENABLE_ASAN OR SIMRV_ENABLE_UBSAN))
  message(FATAL_ERROR "ThreadSanitizer must use a separate build from ASan/UBSan")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT "cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not support required C++23 features")
endif()

# GCC 15 is the minimum supported GNU toolchain. It is the GNU baseline validated for this
# C++23 codebase; use a current Clang release when GNU 15+ is unavailable.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
  message(FATAL_ERROR "GCC 15 or newer is required (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()

# XLEN option (32 or 64) – compile-time selection
option(SIMRV_XLEN "Target XLEN (32 or 64)" 32)
set_property(CACHE SIMRV_XLEN PROPERTY STRINGS 32 64)
if(NOT SIMRV_XLEN STREQUAL "32" AND NOT SIMRV_XLEN STREQUAL "64")
  message(FATAL_ERROR "SIMRV_XLEN must be 32 or 64 (got: ${SIMRV_XLEN})")
endif()
set(SIMRV_LINUX_IMAGES_DIR "${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}" CACHE PATH "Path to reproducible Linux test images")

set(SIMRV_DRAM_SIZE_MB "256" CACHE STRING "DRAM size in megabytes (must be power of 2)")
set(SIMRV_DISK_SIZE_MB "256" CACHE STRING "Disk size in megabytes")
set(SIMRV_CORE_COUNT "1" CACHE STRING "Number of CPU cores (experimental)")
set(SIMRV_SPIKE_PATH "spike" CACHE STRING "Path to Spike binary for lockstep co-simulation (runtime default, not used at build time)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(SimRVOptions)

  add_subdirectory(src/pipeline)
  add_subdirectory(src/execute)
  add_subdirectory(src/memory)
  add_subdirectory(src/device)
  add_subdirectory(src/debug)
  add_subdirectory(src/tui)
  # Runtime is the composition target and must be declared after all component object targets.
  add_subdirectory(src/core)

  add_executable(SimRV src/Main.cpp)
  target_include_directories(SimRV PRIVATE
    include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
  )
  target_link_libraries(SimRV PRIVATE simrv-runtime)
  simrv_apply_options(SimRV)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-frounding-math" COMPILER_SUPPORTS_FROUNDING_MATH)
if(COMPILER_SUPPORTS_FROUNDING_MATH)
  # Only the host-FP execution units alter the floating-point environment.  Applying strict
  # rounding semantics to the entire simulator prevents useful optimization in the integer,
  # memory, and scheduling hot paths.
  set_source_files_properties(
    src/execute/ExecuteUnitFloat.cpp
    src/execute/vector/VectorFloat.cpp
    PROPERTIES COMPILE_OPTIONS "-frounding-math"
  )
endif()

# Release optimizations and Link-Time Optimization (LTO/IPO)
if(CMAKE_BUILD_TYPE STREQUAL "Release")

  if(SIMRV_NATIVE_HOST_OPTIMIZATIONS)
    message(STATUS "Native-host Release tuning enabled; binary is not portable.")
  endif()

  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set(SIMRV_IPO_ENABLED ON)
    # Most simulator code is compiled through object libraries and then composed into simrv-sdk.
    # Enabling IPO only on the executable leaves those hot-path objects outside the LTO unit.
    # Apply it to every concrete part of the final product so the linker can inline across the
    # CPU, memory, pipeline, and device boundaries.
    set(simrv_ipo_targets
      simrv-foundation
      simrv-isa
      simrv-execution
      simrv-pipeline
      simrv-memory
      simrv-platform
      simrv-debug
      simrv-tui-framework
      simrv-tui
      simrv-core
      simrv-runtime
      SimRV
    )
    foreach(simrv_ipo_target IN LISTS simrv_ipo_targets)
      set_property(TARGET ${simrv_ipo_target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endforeach()
    message(STATUS "LTO (Link-Time Optimization) enabled across the simulator runtime.")
  else()
    set(SIMRV_IPO_ENABLED OFF)
    message(STATUS "LTO not supported: ${ipo_error}")
  endif()
endif()

# Standard compilation optimization options are handled via build types (e.g., -O3 in Release mode)

# Toolchain detection for testing. Host-specific defaults are intentionally
# avoided: release dependencies must be supplied through cache variables or PATH.
if(BUILD_TESTING)
if(NOT DEFINED RISCV_PREFIX)
  if(DEFINED ENV{RISCV_PREFIX})
    set(RISCV_PREFIX "$ENV{RISCV_PREFIX}")
  else()
    set(RISCV_PREFIX "")
  endif()
endif()

set(OBJCOPY_NAMES)
set(NM_NAMES)
if(NOT "${RISCV_PREFIX}" STREQUAL "")
  list(APPEND OBJCOPY_NAMES "${RISCV_PREFIX}objcopy")
  list(APPEND NM_NAMES "${RISCV_PREFIX}nm")
endif()
list(APPEND OBJCOPY_NAMES "riscv64-unknown-elf-objcopy" "riscv32-unknown-elf-objcopy" "riscv-none-elf-objcopy" "objcopy")
list(APPEND NM_NAMES "riscv64-unknown-elf-nm" "riscv32-unknown-elf-nm" "riscv-none-elf-nm" "nm")

find_program(RISCV_OBJCOPY NAMES ${OBJCOPY_NAMES}
             DOC "Path to objcopy tool for binary extraction")
find_program(RISCV_NM NAMES ${NM_NAMES}
             DOC "Path to nm tool for symbol lookup")
find_program(SPIKE_BIN spike DOC "Path to Spike binary for lockstep co-simulation")

set(ISA_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/.isa_tmp")
file(MAKE_DIRECTORY "${ISA_WORK_DIR}")

# Fast, deterministic tests for the terminal parser, theme helpers, and key registry.
add_executable(simrv-tui-tests
  tests/TuiFrameworkTests.cpp
)
target_link_libraries(simrv-tui-tests PRIVATE simrv-runtime)
simrv_configure_native_test(simrv-tui-tests)

add_test(NAME tui-framework COMMAND simrv-tui-tests)
set_tests_properties(tui-framework PROPERTIES LABELS "gate;regress;tui")
if(SIMRV_ENABLE_TSAN)
  set_property(TEST tui-framework APPEND PROPERTY LABELS "thread")
endif()

add_executable(simrv-tui-framework-tests tests/TuiFrameworkCoreTests.cpp)
target_compile_features(simrv-tui-framework-tests PRIVATE cxx_std_23)
target_link_libraries(simrv-tui-framework-tests PRIVATE simrv-tui-framework)
simrv_configure_native_test(simrv-tui-framework-tests)
add_test(NAME tui-framework-core COMMAND simrv-tui-framework-tests)
set_tests_properties(tui-framework-core PROPERTIES LABELS "gate;regress;tui")

add_executable(simrv-pipeline-model-tests
  tests/PipelineModelTests.cpp
)
target_link_libraries(simrv-pipeline-model-tests PRIVATE simrv-runtime)
simrv_configure_native_test(simrv-pipeline-model-tests)

add_test(NAME pipeline-models COMMAND simrv-pipeline-model-tests)
set_tests_properties(pipeline-models PROPERTIES LABELS "gate;regress;pipeline")
if(SIMRV_ENABLE_TSAN)
  set_property(TEST pipeline-models APPEND PROPERTY LABELS "thread")
endif()

add_executable(simrv-branch-predictor-tests
  tests/BranchPredictorTests.cpp
)
target_link_libraries(simrv-branch-predictor-tests PRIVATE simrv-runtime)
simrv_configure_native_test(simrv-branch-predictor-tests)

add_test(NAME branch-predictor COMMAND simrv-branch-predictor-tests)
set_tests_properties(branch-predictor PROPERTIES LABELS "gate;regress;pipeline;bpred")
if(SIMRV_ENABLE_TSAN)
  set_property(TEST branch-predictor APPEND PROPERTY LABELS "thread")
endif()

add_executable(simrv-core-semantics-tests tests/CoreSemanticsTests.cpp)
target_link_libraries(simrv-core-semantics-tests PRIVATE simrv-runtime)
simrv_configure_native_test(simrv-core-semantics-tests)

add_test(NAME core-semantics COMMAND simrv-core-semantics-tests)
set_tests_properties(core-semantics PROPERTIES LABELS "gate;regress;memory;mmio")

add_executable(simrv-pmp-tests tests/PmpTests.cpp)
target_link_libraries(simrv-pmp-tests PRIVATE simrv-runtime)
simrv_configure_native_test(simrv-pmp-tests)
add_test(NAME pmp COMMAND simrv-pmp-tests)
set_tests_properties(pmp PROPERTIES LABELS "gate;regress;core;memory;pmp")

add_executable(simrv-machine-config-tests tests/MachineConfigTests.cpp)
simrv_configure_native_test(simrv-machine-config-tests)
add_test(NAME machine-config COMMAND simrv-machine-config-tests)
set_tests_properties(machine-config PROPERTIES LABELS "gate;regress;core;config")

add_executable(simrv-machine-ownership-tests tests/MachineOwnershipTests.cpp)
target_link_libraries(simrv-machine-ownership-tests PRIVATE simrv-runtime)
simrv_configure_native_test(simrv-machine-ownership-tests)
add_test(NAME machine-ownership COMMAND simrv-machine-ownership-tests)
set_tests_properties(machine-ownership PROPERTIES LABELS "gate;regress;core;ownership")

add_executable(simrv-modern-platform-tests
  tests/ModernPlatformTests.cpp
)
target_link_libraries(simrv-modern-platform-tests PRIVATE simrv-runtime)
simrv_configure_native_test(simrv-modern-platform-tests)

add_test(NAME modern-platform COMMAND simrv-modern-platform-tests)
set_tests_properties(modern-platform PROPERTIES LABELS "gate;regress;device;mmio;pcie;aia")

# Native CLI tests
add_test(NAME cli-help COMMAND $<TARGET_FILE:SimRV> -h)
set_tests_properties(cli-help PROPERTIES
  PASS_REGULAR_EXPRESSION "Usage:"
  LABELS "gate;regress;cli"
)

add_test(NAME cli-invalid-option COMMAND $<TARGET_FILE:SimRV> -Z)
set_tests_properties(cli-invalid-option PROPERTIES
  PASS_REGULAR_EXPRESSION "unknown option"
  LABELS "gate;regress;cli"
)

add_test(NAME cli-removed-rollback COMMAND $<TARGET_FILE:SimRV> --rollback)
set_tests_properties(cli-removed-rollback PROPERTIES
  PASS_REGULAR_EXPRESSION "unknown option"
  LABELS "gate;regress;cli"
)

add_test(NAME cli-conflicting-execution COMMAND $<TARGET_FILE:SimRV> --ca --ia)
set_tests_properties(cli-conflicting-execution PROPERTIES
  WILL_FAIL TRUE
  LABELS "gate;regress;cli"
)

foreach(obsolete_option IN ITEMS --preset --pipe --bp-type --btb-size --no-ex-forwarding --no-mem-forwarding)
  string(REGEX REPLACE "^-+" "" obsolete_name "${obsolete_option}")
  add_test(NAME "cli-obsolete-${obsolete_name}" COMMAND $<TARGET_FILE:SimRV> ${obsolete_option})
  set_tests_properties("cli-obsolete-${obsolete_name}" PROPERTIES
    WILL_FAIL TRUE
    LABELS "gate;regress;cli"
  )
endforeach()

add_test(NAME cli-invalid-pipeline COMMAND $<TARGET_FILE:SimRV> --pipeline dual)
set_tests_properties(cli-invalid-pipeline PROPERTIES
  WILL_FAIL TRUE
  LABELS "gate;regress;cli"
)

add_test(NAME release-metadata
  COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/release_check.py
)
set_tests_properties(release-metadata PROPERTIES LABELS "gate;regress;release")
add_test(NAME release-tools
  COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/tests/ReleaseToolTests.py
)
set_tests_properties(release-tools PROPERTIES LABELS "gate;regress;release")

# Optional App/Linux smoke tests
if(DEFINED ENV{SIMRV_APP_IMG})
  add_test(NAME app-smoke COMMAND $<TARGET_FILE:SimRV> -m $ENV{SIMRV_APP_IMG} -b -e 200000)
  set_tests_properties(app-smoke PROPERTIES
    TIMEOUT 20
    LABELS "external;app"
  )
endif()

if(DEFINED ENV{SIMRV_LINUX_MEM_IMG} AND DEFINED ENV{SIMRV_LINUX_DISK_IMG})
  set(LINUX_BOOT_CMD $<TARGET_FILE:SimRV> -m $ENV{SIMRV_LINUX_MEM_IMG} -D $ENV{SIMRV_LINUX_DISK_IMG} -e 1000000)
  if(DEFINED ENV{SIMRV_LINUX_DTB})
    list(APPEND LINUX_BOOT_CMD -f $ENV{SIMRV_LINUX_DTB})
  endif()
  add_test(NAME linux-boot COMMAND ${LINUX_BOOT_CMD})
  set_tests_properties(linux-boot PROPERTIES
    TIMEOUT 60
    LABELS "external;linux"
  )
endif()

set(TEST_IMAGES_DIR "${SIMRV_LINUX_IMAGES_DIR}")
if(EXISTS "${TEST_IMAGES_DIR}/fw_payload.bin")
  find_program(PYTHON_BIN NAMES "python3" "python" DOC "Path to python3 interpreter")
  if(NOT PYTHON_BIN)
    set(PYTHON_BIN "python3")
  endif()
  add_test(NAME linux-boot-pty
    COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/test_linux_pty.py
  )
  set_tests_properties(linux-boot-pty PROPERTIES
    ENVIRONMENT "SIMRV_BIN=$<TARGET_FILE:SimRV>;SIMRV_IMAGES_DIR=${TEST_IMAGES_DIR}"
    TIMEOUT 110
    LABELS "external;linux;pty;required"
  )
  add_test(NAME linux-poweroff-quit-pty
    COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/test_linux_pty.py
  )
  set_tests_properties(linux-poweroff-quit-pty PROPERTIES
    ENVIRONMENT "SIMRV_BIN=$<TARGET_FILE:SimRV>;SIMRV_IMAGES_DIR=${TEST_IMAGES_DIR};SIMRV_TEST_LIFECYCLE=poweroff"
    TIMEOUT 110
    LABELS "external;linux;pty;lifecycle;required"
  )
  add_test(NAME linux-poweroff-reboot-pty
    COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/test_linux_pty.py
  )
  set_tests_properties(linux-poweroff-reboot-pty PROPERTIES
    ENVIRONMENT "SIMRV_BIN=$<TARGET_FILE:SimRV>;SIMRV_IMAGES_DIR=${TEST_IMAGES_DIR};SIMRV_TEST_LIFECYCLE=poweroff-reboot"
    TIMEOUT 110
    LABELS "external;linux;pty;lifecycle;required"
  )
endif()

# Glob available ISA tests from riscv-tests at configure time
set(DEFAULT_RISCV_TESTS_DIR "${CMAKE_SOURCE_DIR}/../../tests/riscv-tests")
if(DEFINED RISCV_TESTS_DIR AND NOT "${RISCV_TESTS_DIR}" STREQUAL "")
  set(RISCV_TESTS_DIR "${RISCV_TESTS_DIR}")
elseif(EXISTS "${DEFAULT_RISCV_TESTS_DIR}" AND IS_DIRECTORY "${DEFAULT_RISCV_TESTS_DIR}")
  set(RISCV_TESTS_DIR "${DEFAULT_RISCV_TESTS_DIR}")
elseif(DEFINED ENV{RISCV_TESTS_DIR})
  set(RISCV_TESTS_DIR "$ENV{RISCV_TESTS_DIR}")
else()
  set(RISCV_TESTS_DIR "${DEFAULT_RISCV_TESTS_DIR}")
endif()

if(EXISTS "${RISCV_TESTS_DIR}/share/riscv-tests")
  set(RISCV_TESTS_DIR "${RISCV_TESTS_DIR}/share/riscv-tests")
endif()

set(ISA_DIR "${RISCV_TESTS_DIR}/isa")

set(ISA_SMOKE_RV32GC
  "rv32ui-p-add" "rv32ui-p-addi" "rv32ui-p-and" "rv32ui-p-beq" "rv32ui-p-bne" "rv32ui-p-bltu" "rv32ui-p-bgeu"
  "rv32ui-p-lb" "rv32ui-p-lbu" "rv32ui-p-lh" "rv32ui-p-lhu" "rv32ui-p-lw" "rv32ui-p-sw" "rv32ui-p-sll"
  "rv32ui-p-slt" "rv32ui-p-sltu" "rv32ui-p-sra" "rv32ui-p-srl" "rv32ui-p-sub" "rv32ui-p-xor" "rv32ui-p-or"
  "rv32um-p-mul" "rv32um-p-mulh" "rv32um-p-mulhu" "rv32um-p-div" "rv32um-p-divu" "rv32um-p-rem" "rv32um-p-remu"
  "rv32ua-p-amoadd_w" "rv32ua-p-amomax_w" "rv32ua-p-amomaxu_w" "rv32ua-p-amomin_w" "rv32ua-p-amominu_w"
  "rv32ua-p-amoor_w" "rv32ua-p-amoswap_w" "rv32ua-p-amoxor_w" "rv32ua-p-lrsc"
)

set(ISA_SMOKE_RV64GC
  "rv64ui-p-add" "rv64ui-p-addi" "rv64ui-p-and" "rv64ui-p-auipc" "rv64ui-p-beq" "rv64ui-p-bge" "rv64ui-p-bgeu"
  "rv64ui-p-blt" "rv64ui-p-bltu" "rv64ui-p-bne" "rv64ui-p-jal" "rv64ui-p-jalr" "rv64ui-p-lb" "rv64ui-p-lbu"
  "rv64ui-p-lh" "rv64ui-p-lhu" "rv64ui-p-lw" "rv64ui-p-lwu" "rv64ui-p-or" "rv64ui-p-sb" "rv64ui-p-sd"
  "rv64ui-p-sh" "rv64ui-p-sw" "rv64ui-p-sll" "rv64ui-p-slli" "rv64ui-p-slt" "rv64ui-p-sltu" "rv64ui-p-sra"
  "rv64ui-p-srai" "rv64ui-p-srl" "rv64ui-p-srli" "rv64ui-p-sub" "rv64ui-p-xor" "rv64um-p-mul" "rv64um-p-mulh"
  "rv64um-p-mulhsu" "rv64um-p-mulhu" "rv64um-p-div" "rv64um-p-divu" "rv64um-p-rem" "rv64um-p-remu"
  "rv64ua-p-amoadd_w" "rv64ua-p-amomax_w" "rv64ua-p-amomaxu_w" "rv64ua-p-amomin_w" "rv64ua-p-amominu_w"
  "rv64ua-p-amoor_w" "rv64ua-p-amoswap_w" "rv64ua-p-amoxor_w" "rv64ua-p-lrsc"
)

set(ALL_ISA_TESTS)
if(EXISTS "${ISA_DIR}" AND IS_DIRECTORY "${ISA_DIR}")
  file(GLOB ALL_FILES "${ISA_DIR}/rv${SIMRV_XLEN}*")
  foreach(f IN LISTS ALL_FILES)
    if(IS_DIRECTORY "${f}")
      continue()
    endif()
    get_filename_component(ext "${f}" LAST_EXT)
    get_filename_component(name "${f}" NAME)
    if(ext STREQUAL "" AND name MATCHES "^rv${SIMRV_XLEN}(ui|uc|um|ua|uf|ud|uv|uzba|uzbb|uzbc|uzbs)-[pv]-")
      list(APPEND ALL_ISA_TESTS "${name}")
    endif()
  endforeach()
endif()

function(register_isa_test test_name is_smoke)
  set(labels "external;isa")
  if(is_smoke)
    list(APPEND labels "smoke")
  endif()
  list(APPEND labels "rv${SIMRV_XLEN}gc")

  add_test(NAME "isa-${test_name}"
    COMMAND ${CMAKE_COMMAND}
      -D SIMRV_BIN=$<TARGET_FILE:SimRV>
      -D ELF_PATH=${ISA_DIR}/${test_name}
      -D OBJCOPY_BIN=${RISCV_OBJCOPY}
      -D NM_BIN=${RISCV_NM}
      -D WORK_DIR=${ISA_WORK_DIR}
      -D TIMEOUT_SECS=20
      -D END_INSNS=2000000
      -D DEFAULT_TOHOST_ADDR=0x80001000
      -P ${CMAKE_SOURCE_DIR}/scripts/RunIsaTest.cmake
  )
  set_tests_properties("isa-${test_name}" PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    LABELS "${labels}"
    SKIP_RETURN_CODE 2
  )
endfunction()

function(register_lockstep_isa_test test_name)
  add_test(NAME "isa-lockstep-${test_name}"
    COMMAND ${CMAKE_COMMAND}
      -D SIMRV_BIN=$<TARGET_FILE:SimRV>
      -D ELF_PATH=${ISA_DIR}/${test_name}
      -D OBJCOPY_BIN=${RISCV_OBJCOPY}
      -D NM_BIN=${RISCV_NM}
      -D WORK_DIR=${ISA_WORK_DIR}
      -D TIMEOUT_SECS=20
      -D END_INSNS=2000000
      -D DEFAULT_TOHOST_ADDR=0x80001000
      -D LOCKSTEP=TRUE
      -D SPIKE_BIN=${SPIKE_BIN}
      -P ${CMAKE_SOURCE_DIR}/scripts/RunIsaTest.cmake
  )
  set_tests_properties("isa-lockstep-${test_name}" PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    LABELS "external;isa;lockstep;rv${SIMRV_XLEN}gc"
    SKIP_RETURN_CODE 2
  )
endfunction()

if(SIMRV_XLEN STREQUAL "64")
  set(SMOKE_LIST ${ISA_SMOKE_RV64GC})
else()
  set(SMOKE_LIST ${ISA_SMOKE_RV32GC})
endif()

foreach(test_name IN LISTS SMOKE_LIST)
  register_isa_test("${test_name}" TRUE)
endforeach()

function(register_ca_isa_test test_name pipeline_type)
  if(NOT EXISTS "${ISA_DIR}/${test_name}")
    return()
  endif()
  add_test(NAME "ca-${pipeline_type}-${test_name}"
    COMMAND ${CMAKE_COMMAND}
      -D SIMRV_BIN=$<TARGET_FILE:SimRV>
      -D ELF_PATH=${ISA_DIR}/${test_name}
      -D OBJCOPY_BIN=${RISCV_OBJCOPY}
      -D NM_BIN=${RISCV_NM}
      -D WORK_DIR=${ISA_WORK_DIR}/${pipeline_type}
      -D TIMEOUT_SECS=20
      -D END_INSNS=2000000
      -D DEFAULT_TOHOST_ADDR=0x80001000
      -D CYCLE_MODE=TRUE
      -D PIPELINE_TYPE=${pipeline_type}
      -P ${CMAKE_SOURCE_DIR}/scripts/RunIsaTest.cmake
  )
  set_tests_properties("ca-${pipeline_type}-${test_name}" PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    LABELS "external;isa;ca;smoke;pipeline;rv${SIMRV_XLEN}gc"
    SKIP_RETURN_CODE 2
  )
endfunction()

set(CA_SMOKE_TESTS
  "rv${SIMRV_XLEN}ui-p-add"
  "rv${SIMRV_XLEN}ui-p-jalr"
  "rv${SIMRV_XLEN}ui-p-lw"
  "rv${SIMRV_XLEN}ui-p-sw"
  "rv${SIMRV_XLEN}ui-p-ma_data"
  "rv${SIMRV_XLEN}ui-p-fence_i"
  "rv${SIMRV_XLEN}um-p-mul"
  "rv${SIMRV_XLEN}um-p-div"
  "rv${SIMRV_XLEN}ua-p-lrsc"
  "rv${SIMRV_XLEN}uc-p-rvc"
  "rv${SIMRV_XLEN}uf-p-fadd"
)
foreach(ca_pipeline IN ITEMS 5stage 3stage)
  foreach(ca_test IN LISTS CA_SMOKE_TESTS)
    register_ca_isa_test("${ca_test}" "${ca_pipeline}")
  endforeach()
endforeach()

if(SPIKE_BIN)
  foreach(test_name IN LISTS SMOKE_LIST)
    register_lockstep_isa_test("${test_name}")
  endforeach()
endif()

foreach(test_name IN LISTS ALL_ISA_TESTS)
  if(NOT test_name IN_LIST SMOKE_LIST)
    register_isa_test("${test_name}" FALSE)
  endif()
endforeach()

# Register vector tests wrapper if an explicit vector test package is available.
if(SIMRV_VECTOR_TESTS_DIR AND EXISTS "${SIMRV_VECTOR_TESTS_DIR}")
  set(RISCV_GCC_NAMES)
  if(NOT "${RISCV_PREFIX}" STREQUAL "")
    list(APPEND RISCV_GCC_NAMES "${RISCV_PREFIX}gcc")
  endif()
  list(APPEND RISCV_GCC_NAMES "riscv64-unknown-elf-gcc" "riscv32-unknown-elf-gcc" "riscv64-linux-gnu-gcc" "riscv32-linux-gnu-gcc" "riscv-none-elf-gcc")
  find_program(RISCV_GCC NAMES ${RISCV_GCC_NAMES}
               DOC "Path to RISC-V GCC cross-compiler for compiling vector tests")
  find_program(PYTHON_BIN NAMES "python3" "python" DOC "Path to python3 interpreter")
  if(NOT PYTHON_BIN)
    set(PYTHON_BIN "python3")
  endif()

  if(RISCV_GCC)
    add_test(NAME vector-suite
      COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/run_vector_tests.py
        --simrv $<TARGET_FILE:SimRV>
        --xlen ${SIMRV_XLEN}
        --gcc ${RISCV_GCC}
        --objcopy ${RISCV_OBJCOPY}
        --nm ${RISCV_NM}
        --work-dir ${CMAKE_CURRENT_BINARY_DIR}/vector-tests-gen
        --vector-tests-dir ${SIMRV_VECTOR_TESTS_DIR}
    )
    set_tests_properties(vector-suite PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      LABELS "external;vector;required"
      TIMEOUT 600
    )
  endif()
endif()


add_test(NAME benchmark
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_BIN=$<TARGET_FILE:SimRV> SIMRV_BENCH_ITERS=5 bash ${CMAKE_SOURCE_DIR}/scripts/benchmark.sh
)
set_tests_properties(benchmark PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  LABELS "benchmark;perf"
  SKIP_RETURN_CODE 2
)
endif()

# Utility Targets
if(SIMRV_ENABLE_TOOLING)
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
    USES_TERMINAL
  )
endif()

endif()

if(SIMRV_ENABLE_LINUX_TOOLS)
add_custom_target(linux-images
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_USE_LOCAL_SCRATCH=1 bash ${CMAKE_SOURCE_DIR}/scripts/build-linux-image.sh --arch rv${SIMRV_XLEN}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Building RV${SIMRV_XLEN}GC Linux kernel and rootfs images"
  VERBATIM
  USES_TERMINAL
)
endif()

add_custom_target(isa-gate
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -L "gate"
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running ISA validation gate (All non-vector RV${SIMRV_XLEN}GC ISA and CLI tests)"
  VERBATIM
  USES_TERMINAL
)

add_custom_target(integration-gate
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -L "gate"
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running the complete SimRV integration gate"
  VERBATIM
  USES_TERMINAL
)

if(SIMRV_ENABLE_TOOLING)
add_custom_target(release-check
  COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/release_check.py
    --binary $<TARGET_FILE:SimRV>
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Validating SimRV release metadata and binary"
  VERBATIM
)
endif()

if(SIMRV_VECTOR_TESTS_DIR AND EXISTS "${SIMRV_VECTOR_TESTS_DIR}" AND RISCV_GCC)
  add_custom_target(vector-gate
    COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/run_vector_tests.py
      --simrv $<TARGET_FILE:SimRV>
      --xlen ${SIMRV_XLEN}
      --gcc ${RISCV_GCC}
      --objcopy ${RISCV_OBJCOPY}
      --nm ${RISCV_NM}
      --work-dir ${CMAKE_CURRENT_BINARY_DIR}/vector-tests-gen
      --vector-tests-dir ${SIMRV_VECTOR_TESTS_DIR}
    DEPENDS SimRV
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Full Vector validation gate"
  )
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set_target_properties(SimRV PROPERTIES EXPORT_NAME cli)

install(TARGETS SimRV simrv-runtime
  EXPORT SimRVTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/simrv
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp"
)
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp"
)
install(DIRECTORY schemas/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/SimRV/schemas
  FILES_MATCHING PATTERN "*.json"
)

configure_package_config_file(
  cmake/SimRVConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/SimRVConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SimRV"
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/SimRVConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)
install(EXPORT SimRVTargets
  FILE SimRVTargets.cmake
  NAMESPACE SimRV::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SimRV"
)
export(EXPORT SimRVTargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/SimRVTargets.cmake"
  NAMESPACE SimRV::
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/SimRVConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/SimRVConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SimRV"
)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/SimRV)

if(BUILD_TESTING)
  add_test(NAME architecture-boundaries
    COMMAND ${CMAKE_COMMAND}
      -D SIMRV_SOURCE_DIR=${CMAKE_SOURCE_DIR}
      -P ${CMAKE_SOURCE_DIR}/cmake/CheckArchitecture.cmake
  )
  set_tests_properties(architecture-boundaries PROPERTIES
    LABELS "gate;regress;architecture"
  )
  add_test(NAME package-install-consumer
    COMMAND ${CMAKE_COMMAND}
      -D SIMRV_SOURCE_DIR=${CMAKE_SOURCE_DIR}
      -D SIMRV_BINARY_DIR=${CMAKE_BINARY_DIR}
      -P ${CMAKE_SOURCE_DIR}/cmake/TestPackageConsumer.cmake
  )
  set_tests_properties(package-install-consumer PROPERTIES
    LABELS "gate;regress;package;sdk"
  )
endif()

if(SIMRV_ENABLE_LINUX_TOOLS)
add_custom_target(run-tui
  COMMAND ${CMAKE_COMMAND}
    -D SIMRV_BIN=$<TARGET_FILE:SimRV>
    -D SIMRV_FW_PAYLOAD=${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/fw_payload.bin
    -D SIMRV_DISK_IMAGE=${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/root.img
    -D SIMRV_DTB=${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/devicetree.dtb
    -D SIMRV_TUI=ON
    -P ${CMAKE_SOURCE_DIR}/cmake/SimRVRunLinux.cmake
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Booting RV${SIMRV_XLEN} Linux in TUI mode..."
  VERBATIM
  USES_TERMINAL
)

add_custom_target(run-linux
  COMMAND ${CMAKE_COMMAND}
    -D SIMRV_BIN=$<TARGET_FILE:SimRV>
    -D SIMRV_FW_PAYLOAD=${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/fw_payload.bin
    -D SIMRV_DISK_IMAGE=${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/root.img
    -D SIMRV_DTB=${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/devicetree.dtb
    -P ${CMAKE_SOURCE_DIR}/cmake/SimRVRunLinux.cmake
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Booting RV${SIMRV_XLEN} Linux in standard console mode..."
  VERBATIM
  USES_TERMINAL
)

endif()

if(SIMRV_ENABLE_TOOLING)
find_program(CLANG_FORMAT_BIN NAMES clang-format DOC "Path to clang-format tool")
if(CLANG_FORMAT_BIN)
  file(GLOB_RECURSE ALL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/include/*.hpp"
  )

  add_custom_target(format
    COMMAND ${CLANG_FORMAT_BIN} -i ${ALL_SOURCE_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting C++ codebase with clang-format"
    VERBATIM
    USES_TERMINAL
  )

  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT_BIN} --dry-run --Werror ${ALL_SOURCE_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking C++ codebase formatting with clang-format"
    VERBATIM
    USES_TERMINAL
  )
endif()
endif()

set(CPACK_PACKAGE_NAME "SimRV")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "SimRV-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
include(CPack)
