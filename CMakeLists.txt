cmake_minimum_required(VERSION 3.25)

project(SimRV VERSION 2.1.0 LANGUAGES C CXX)

enable_testing()
find_program(PYTHON_BIN NAMES "python3" "python" DOC "Path to Python interpreter")
if(NOT PYTHON_BIN)
  set(PYTHON_BIN "python3")
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
if(SIMRV_ENABLE_CLANG_TIDY)
  set(CMAKE_CXX_CLANG_TIDY clang-tidy)
endif()

option(SIMRV_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
option(SIMRV_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SIMRV_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SIMRV_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
set(SIMRV_VECTOR_TESTS_DIR "" CACHE PATH "Path to a built chipsalliance/riscv-vector-tests checkout")

if(SIMRV_ENABLE_TSAN AND (SIMRV_ENABLE_ASAN OR SIMRV_ENABLE_UBSAN))
  message(FATAL_ERROR "ThreadSanitizer must use a separate build from ASan/UBSan")
endif()

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
set(SIMRV_LINUX_IMAGES_DIR "${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}" CACHE PATH "Path to reproducible Linux test images")

set(SIMRV_DRAM_SIZE_MB "256" CACHE STRING "DRAM size in megabytes (must be power of 2)")
set(SIMRV_DISK_SIZE_MB "256" CACHE STRING "Disk size in megabytes")
set(SIMRV_CORE_COUNT "1" CACHE STRING "Number of CPU cores (experimental)")
set(SIMRV_SPIKE_PATH "spike" CACHE STRING "Path to Spike binary for lockstep co-simulation (runtime default, not used at build time)")

add_executable(SimRV
  src/Main.cpp
  src/core/Logger.cpp
  src/core/Machine.cpp
  src/core/BaremetalMachine.cpp
  src/core/OSMachine.cpp
  src/core/MachineInit.cpp
  src/core/StateControl.cpp
  src/core/CsrFile.cpp
  src/core/Tracer.cpp
  src/core/Sbi.cpp
  src/core/Tlb.cpp
  src/pipeline/Decoder.cpp
  src/pipeline/PipelineSim.cpp
  src/pipeline/InOrderPipeline.cpp
  src/execute/ExecuteUnitInt.cpp
  src/execute/ExecuteUnitB.cpp
  src/execute/ExecuteUnitFloat.cpp
  src/execute/ExecuteUnit.cpp
  src/execute/ExecuteUnitVector.cpp
  src/execute/vector/VectorConfig.cpp
  src/execute/vector/VectorMemory.cpp
  src/execute/vector/VectorInteger.cpp
  src/execute/vector/VectorFixedPoint.cpp
  src/execute/vector/VectorFloat.cpp
  src/execute/vector/VectorPermute.cpp
  src/pipeline/PipelineStages.cpp
  src/core/Cpu.cpp
  src/util/CliParser.cpp
  src/cache/ICache.cpp
  src/cache/DCache.cpp
  src/memory/MemoryAccess.cpp
  src/memory/MemorySubsystem.cpp
  src/memory/MmioRouter.cpp
  src/memory/Mmu.cpp
  src/memory/TileLinkBus.cpp
  src/memory/CoherenceHub.cpp
  src/device/Console.cpp
  src/device/Disk.cpp
  src/device/VirtioDevice.cpp
  src/device/Rng.cpp
  src/device/Rtc.cpp
  src/device/Uart.cpp
  src/device/Power.cpp
  src/tui/Tui.cpp
  src/tui/TuiFrameRenderer.cpp
  src/tui/TuiGuidance.cpp
  src/tui/TuiKeybindings.cpp
  src/tui/TuiModal.cpp
  src/tui/TuiTheme.cpp
  src/tui/modals/AddressModal.cpp
  src/tui/modals/BreakpointModal.cpp
  src/tui/modals/HelpModal.cpp
  src/tui/modals/LoadModal.cpp
  src/tui/modals/MisaModal.cpp
  src/tui/modals/SettingsModal.cpp
  src/tui/modals/SystemConfigModal.cpp
  src/tui/modals/StepModal.cpp
  src/tui/panels/LeftPane.cpp
  src/tui/panels/LeftPaneRegs.cpp
  src/tui/panels/LeftPanePipeline.cpp
  src/tui/panels/LeftPaneCache.cpp
  src/tui/panels/LeftPaneTlb.cpp
  src/tui/panels/LeftPaneBp.cpp
  src/tui/panels/LeftPaneHazard.cpp
  src/tui/panels/LeftPaneIo.cpp
  src/tui/panels/LeftPaneStats.cpp
  src/tui/panels/LeftPaneExplain.cpp
  src/tui/panels/LeftPaneStack.cpp
  src/tui/panels/RightPane.cpp
  src/tui/panels/StatusBar.cpp
  src/debug/GdbStub.cpp
  src/debug/SpikeLockstep.cpp
  src/debug/SymbolTable.cpp
  src/debug/BreakpointManager.cpp
  src/util/InstructionExplainer.cpp
  src/util/FdtGenerator.cpp
  src/device/Framebuffer.cpp
  src/device/Audio.cpp
  src/device/InputDevice.cpp
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

if(SIMRV_WARNINGS_AS_ERRORS)
  target_compile_options(SimRV PRIVATE -Werror)
endif()

if(SIMRV_ENABLE_ASAN)
  target_compile_options(SimRV PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(SimRV PRIVATE -fsanitize=address)
endif()
if(SIMRV_ENABLE_UBSAN)
  target_compile_options(SimRV PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
  target_link_options(SimRV PRIVATE -fsanitize=undefined)
endif()
if(SIMRV_ENABLE_TSAN)
  target_compile_options(SimRV PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(SimRV PRIVATE -fsanitize=thread)
endif()

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-frounding-math" COMPILER_SUPPORTS_FROUNDING_MATH)
if(COMPILER_SUPPORTS_FROUNDING_MATH)
  target_compile_options(SimRV PRIVATE -frounding-math)
endif()

# Release optimizations and Link-Time Optimization (LTO/IPO)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(SimRV PRIVATE -O3)
  
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-march=x86-64-v3" COMPILER_SUPPORTS_MARCH_X86_64_V3)
  if(COMPILER_SUPPORTS_MARCH_X86_64_V3)
    target_compile_options(SimRV PRIVATE -march=x86-64-v3)
    message(STATUS "x86-64-v3 instruction set architecture (AVX2/FMA/BMI) enabled.")
  endif()
  
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set_property(TARGET SimRV PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "LTO (Link-Time Optimization) enabled.")
  else()
    message(STATUS "LTO not supported: ${ipo_error}")
  endif()
endif()

# Standard compilation optimization options are handled via build types (e.g., -O3 in Release mode)

# Toolchain detection for testing. Host-specific defaults are intentionally
# avoided: release dependencies must be supplied through cache variables or PATH.
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
  src/tui/TuiFrameRenderer.cpp
  src/tui/TuiGuidance.cpp
  src/tui/TuiKeybindings.cpp
  src/tui/TuiTheme.cpp
  src/tui/modals/HelpModal.cpp
  src/tui/modals/SettingsModal.cpp
  src/tui/modals/SystemConfigModal.cpp
)
target_compile_features(simrv-tui-tests PRIVATE cxx_std_23)
target_include_directories(simrv-tui-tests PRIVATE include ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(simrv-tui-tests PRIVATE -Wall -Wextra -Wpedantic)
if(SIMRV_WARNINGS_AS_ERRORS)
  target_compile_options(simrv-tui-tests PRIVATE -Werror)
endif()
if(SIMRV_ENABLE_ASAN)
  target_compile_options(simrv-tui-tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(simrv-tui-tests PRIVATE -fsanitize=address)
endif()
if(SIMRV_ENABLE_UBSAN)
  target_compile_options(simrv-tui-tests PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
  target_link_options(simrv-tui-tests PRIVATE -fsanitize=undefined)
endif()
if(SIMRV_ENABLE_TSAN)
  target_compile_options(simrv-tui-tests PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(simrv-tui-tests PRIVATE -fsanitize=thread)
endif()

add_test(NAME tui-framework COMMAND simrv-tui-tests)
set_tests_properties(tui-framework PROPERTIES LABELS "gate;regress;tui")
if(SIMRV_ENABLE_TSAN)
  set_property(TEST tui-framework APPEND PROPERTY LABELS "thread")
endif()

add_executable(simrv-core-semantics-tests
  tests/CoreSemanticsTests.cpp
  src/core/Logger.cpp
  src/cache/ICache.cpp
  src/cache/DCache.cpp
  src/memory/Mmu.cpp
  src/memory/MmioRouter.cpp
  src/pipeline/Decoder.cpp
  src/util/FdtGenerator.cpp
)
target_compile_features(simrv-core-semantics-tests PRIVATE cxx_std_23)
target_compile_definitions(simrv-core-semantics-tests PRIVATE SIMRV_XLEN=${SIMRV_XLEN})
target_include_directories(simrv-core-semantics-tests PRIVATE include)
target_compile_options(simrv-core-semantics-tests PRIVATE -Wall -Wextra -Wpedantic)
if(SIMRV_WARNINGS_AS_ERRORS)
  target_compile_options(simrv-core-semantics-tests PRIVATE -Werror)
endif()
if(SIMRV_ENABLE_ASAN)
  target_compile_options(simrv-core-semantics-tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(simrv-core-semantics-tests PRIVATE -fsanitize=address)
endif()
if(SIMRV_ENABLE_UBSAN)
  target_compile_options(simrv-core-semantics-tests PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
  target_link_options(simrv-core-semantics-tests PRIVATE -fsanitize=undefined)
endif()

add_test(NAME core-semantics COMMAND simrv-core-semantics-tests)
set_tests_properties(core-semantics PROPERTIES LABELS "gate;regress;memory;mmio")

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

add_test(NAME cli-removed-option COMMAND $<TARGET_FILE:SimRV> --headless)
set_tests_properties(cli-removed-option PROPERTIES
  PASS_REGULAR_EXPRESSION "removed in SimRV 2.0; use --cli"
  LABELS "gate;regress;cli"
)

add_test(NAME cli-removed-linux-option COMMAND $<TARGET_FILE:SimRV> --linux)
set_tests_properties(cli-removed-linux-option PROPERTIES
  PASS_REGULAR_EXPRESSION "removed in SimRV 2.0; use --os"
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
    LABELS "gate;regress;app"
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
    LABELS "gate;regress;linux"
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
    LABELS "gate;regress;linux;pty;required"
  )
  add_test(NAME linux-poweroff-quit-pty
    COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/test_linux_pty.py
  )
  set_tests_properties(linux-poweroff-quit-pty PROPERTIES
    ENVIRONMENT "SIMRV_BIN=$<TARGET_FILE:SimRV>;SIMRV_IMAGES_DIR=${TEST_IMAGES_DIR};SIMRV_TEST_LIFECYCLE=poweroff"
    TIMEOUT 110
    LABELS "gate;regress;linux;pty;lifecycle;required"
  )
  add_test(NAME linux-poweroff-reboot-pty
    COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/test_linux_pty.py
  )
  set_tests_properties(linux-poweroff-reboot-pty PROPERTIES
    ENVIRONMENT "SIMRV_BIN=$<TARGET_FILE:SimRV>;SIMRV_IMAGES_DIR=${TEST_IMAGES_DIR};SIMRV_TEST_LIFECYCLE=poweroff-reboot"
    TIMEOUT 110
    LABELS "gate;regress;linux;pty;lifecycle;required"
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
  set(labels "gate;isa")
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
    LABELS "gate;isa;lockstep;rv${SIMRV_XLEN}gc"
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
      LABELS "gate;vector;required"
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

# Utility Targets
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

add_custom_target(linux-images
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_USE_LOCAL_SCRATCH=1 bash ${CMAKE_SOURCE_DIR}/scripts/build-linux-image.sh --arch rv${SIMRV_XLEN}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Building RV${SIMRV_XLEN}GC Linux kernel and rootfs images"
  VERBATIM
  USES_TERMINAL
)

add_custom_target(isa-gate
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure -L "gate"
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running ISA validation gate (All non-vector RV${SIMRV_XLEN}GC ISA and CLI tests)"
  VERBATIM
  USES_TERMINAL
)

add_custom_target(release-check
  COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/release_check.py
    --binary $<TARGET_FILE:SimRV>
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Validating SimRV release metadata and binary"
  VERBATIM
)

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

install(TARGETS SimRV
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/simrv
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp"
)

add_custom_target(run-tui
  COMMAND $<TARGET_FILE:SimRV> -m ${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/fw_payload.bin
                               -D ${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/root.bin
                               -f ${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/devicetree.dtb
                               -u
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Booting RV${SIMRV_XLEN} Linux in TUI mode..."
  VERBATIM
  USES_TERMINAL
)

add_custom_target(run-linux
  COMMAND $<TARGET_FILE:SimRV> -m ${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/fw_payload.bin
                               -D ${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/root.bin
                               -f ${CMAKE_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/devicetree.dtb
  DEPENDS SimRV
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Booting RV${SIMRV_XLEN} Linux in standard console mode..."
  VERBATIM
  USES_TERMINAL
)

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
