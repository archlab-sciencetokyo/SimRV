cmake_minimum_required(VERSION 3.20)

project(SimRV LANGUAGES CXX)

set(SIMRV_VERSION "2.0.0-alpha.2")
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
  ${CMAKE_SOURCE_DIR}/include/simrv/BuildInfo.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/simrv/BuildInfo.hpp
  @ONLY
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(SimRV
  src/Main.cpp
  src/Machine.cpp
  src/Module.cpp
  src/CsrFile.cpp
  src/StateControl.cpp
  src/DecodeUnit.cpp
  src/ExecuteUnitInt.cpp
  src/ExecuteUnitFloat.cpp
  src/ExecuteUnit.cpp
  src/MmioRouter.cpp
  src/Disk.cpp
  src/Console.cpp
  src/State.cpp
)

target_include_directories(SimRV PRIVATE
  include
  include/simrv
  ${CMAKE_CURRENT_BINARY_DIR}/generated/simrv
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

set(ISA_SMOKE_RV32IM
  "rv32ui-p-add,rv32ui-p-addi,rv32ui-p-and,rv32ui-p-beq,rv32ui-p-bne,rv32ui-p-bltu,rv32ui-p-bgeu,rv32ui-p-lb,rv32ui-p-lbu,rv32ui-p-lh,rv32ui-p-lhu,rv32ui-p-lw,rv32ui-p-sw,rv32ui-p-sll,rv32ui-p-slt,rv32ui-p-sltu,rv32ui-p-sra,rv32ui-p-srl,rv32ui-p-sub,rv32ui-p-xor,rv32ui-p-or,rv32um-p-mul,rv32um-p-mulh,rv32um-p-mulhu,rv32um-p-div,rv32um-p-divu,rv32um-p-rem,rv32um-p-remu"
)

set(ISA_SMOKE_RV32GC
  "rv32ui-p-add,rv32ui-p-addi,rv32ui-p-and,rv32ui-p-beq,rv32ui-p-bne,rv32ui-p-bltu,rv32ui-p-bgeu,rv32ui-p-lb,rv32ui-p-lbu,rv32ui-p-lh,rv32ui-p-lhu,rv32ui-p-lw,rv32ui-p-sw,rv32ui-p-sll,rv32ui-p-slt,rv32ui-p-sltu,rv32ui-p-sra,rv32ui-p-srl,rv32ui-p-sub,rv32ui-p-xor,rv32ui-p-or,rv32um-p-mul,rv32um-p-mulh,rv32um-p-mulhu,rv32um-p-div,rv32um-p-divu,rv32um-p-rem,rv32um-p-remu,rv32ua-p-amoadd_w,rv32ua-p-amomax_w,rv32ua-p-amomaxu_w,rv32ua-p-amomin_w,rv32ua-p-amominu_w,rv32ua-p-amoor_w,rv32ua-p-amoswap_w,rv32ua-p-amoxor_w,rv32ua-p-lrsc"
)

set(ISA_EXT_RV32GCFD_EXPERIMENTAL
  "rv32uc-p-rvc,rv32uf-p-fadd,rv32uf-p-fclass,rv32uf-p-fcmp,rv32uf-p-fcvt,rv32uf-p-fcvt_w,rv32uf-p-fdiv,rv32uf-p-fmadd,rv32uf-p-fmin,rv32uf-p-ldst,rv32ud-p-fadd,rv32ud-p-fclass,rv32ud-p-fcmp,rv32ud-p-fcvt,rv32ud-p-fcvt_w,rv32ud-p-fdiv,rv32ud-p-fmadd,rv32ud-p-fmin,rv32ud-p-ldst"
)

add_custom_target(regress
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:SimRV> bash ${CMAKE_SOURCE_DIR}/scripts/regression.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS SimRV
  USES_TERMINAL
)

add_custom_target(isa-tests
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:SimRV> bash ${CMAKE_SOURCE_DIR}/scripts/run-isa-tests.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS SimRV
  USES_TERMINAL
)

add_custom_target(isa-smoke
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:SimRV> SIMRV_ISA_LIST=${ISA_SMOKE_RV32IM} bash ${CMAKE_SOURCE_DIR}/scripts/run-isa-tests.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS SimRV
  USES_TERMINAL
)

add_custom_target(isa-smoke-rv32gc
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:SimRV> SIMRV_ISA_LIST=${ISA_SMOKE_RV32GC} bash ${CMAKE_SOURCE_DIR}/scripts/run-isa-tests.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS SimRV
  USES_TERMINAL
)

add_custom_target(isa-ext-rv32gc-experimental
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:SimRV> SIMRV_ISA_LIST=${ISA_EXT_RV32GCFD_EXPERIMENTAL} bash ${CMAKE_SOURCE_DIR}/scripts/run-isa-tests.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS SimRV
  USES_TERMINAL
)

add_custom_target(phase1-gate
  DEPENDS regress isa-smoke
)

add_custom_target(phase2-gate
  DEPENDS regress isa-smoke-rv32gc
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
