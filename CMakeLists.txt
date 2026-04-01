cmake_minimum_required(VERSION 3.20)

project(SimRV LANGUAGES CXX)

set(SIMRV_VERSION "0.0.0")
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
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE SIMRV_TAG_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(SIMRV_TAG_OUTPUT)
    string(REGEX REPLACE "^v" "" SIMRV_VERSION "${SIMRV_TAG_OUTPUT}")
  endif()

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

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(simrv
  src/Main.cpp
  src/Machine.cpp
  src/Module.cpp
  src/DecodeUnit.cpp
  src/ExecuteUnit.cpp
  src/Disk.cpp
  src/Console.cpp
  src/State.cpp
)

target_include_directories(simrv PRIVATE
  include
  include/simrv
)

target_compile_options(simrv PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

target_compile_definitions(simrv PRIVATE
  SIMRV_VERSION_STR="${SIMRV_VERSION}"
  SIMRV_GIT_BRANCH_STR="${SIMRV_GIT_BRANCH}"
  SIMRV_GIT_SHA_STR="${SIMRV_GIT_SHA}"
)

# Match prior optimization defaults for single-config generators.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(simrv PRIVATE -O2)
endif()

set(ISA_SMOKE_RV32IM
  "rv32ui-p-add,rv32ui-p-addi,rv32ui-p-and,rv32ui-p-beq,rv32ui-p-bne,rv32ui-p-bltu,rv32ui-p-bgeu,rv32ui-p-lb,rv32ui-p-lbu,rv32ui-p-lh,rv32ui-p-lhu,rv32ui-p-lw,rv32ui-p-sw,rv32ui-p-sll,rv32ui-p-slt,rv32ui-p-sltu,rv32ui-p-sra,rv32ui-p-srl,rv32ui-p-sub,rv32ui-p-xor,rv32ui-p-or,rv32um-p-mul,rv32um-p-mulh,rv32um-p-mulhu,rv32um-p-div,rv32um-p-divu,rv32um-p-rem,rv32um-p-remu"
)

add_custom_target(regress
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:simrv> bash ${CMAKE_SOURCE_DIR}/scripts/regression.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS simrv
  USES_TERMINAL
)

add_custom_target(isa-tests
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:simrv> bash ${CMAKE_SOURCE_DIR}/scripts/run-isa-tests.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS simrv
  USES_TERMINAL
)

add_custom_target(isa-smoke
  COMMAND ${CMAKE_COMMAND} -E env SIMRV_SKIP_BUILD=1 SIMRV_BIN=$<TARGET_FILE:simrv> SIMRV_ISA_LIST=${ISA_SMOKE_RV32IM} bash ${CMAKE_SOURCE_DIR}/scripts/run-isa-tests.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS simrv
  USES_TERMINAL
)

add_custom_target(phase1-gate
  DEPENDS regress isa-smoke
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
