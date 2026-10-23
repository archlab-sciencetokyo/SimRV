cmake_minimum_required(VERSION 3.31...4.3)

# Keep the release version authoritative here. The project() version is derived
# from it so CMake, BuildInfo, CPack, release tooling, and citation checks use
# the same source value.
set(SIMRV_VERSION "3.0.0-rc.16")
string(REGEX REPLACE "-.*$" "" SIMRV_PROJECT_VERSION "${SIMRV_VERSION}")

project(
  SimRV
  VERSION ${SIMRV_PROJECT_VERSION}
  DESCRIPTION "SimCore/RISC-V Functional Simulator"
  HOMEPAGE_URL "https://github.com/archlab-sciencetokyo/SimRV"
  LANGUAGES C CXX)

# Keep source installs self-contained by default. Native distro packages set
# CPACK_PACKAGING_INSTALL_PREFIX independently, and callers can still override
# this with -DCMAKE_INSTALL_PREFIX or cmake --install --prefix.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr/local/simrv" CACHE PATH
      "Install prefix for a source-built SimRV distribution" FORCE)
endif()

include(CTest)
option(SIMRV_ENABLE_TOOLING
       "Enable release validation helpers" OFF)
option(SIMRV_ENABLE_LINUX_TOOLS
       "Enable Linux image build and launch helper targets" OFF)
if(BUILD_TESTING OR SIMRV_ENABLE_TOOLING)
  find_package(
    Python3
    COMPONENTS Interpreter
    REQUIRED)
  set(PYTHON_BIN "${Python3_EXECUTABLE}")
endif()

set(SIMRV_GIT_BRANCH "unknown")
set(SIMRV_GIT_SHA "unknown")

# Prefer GitHub CI metadata when available, otherwise fall back to local git
# state.
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
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(SIMRV_BRANCH_OUTPUT)
      set(SIMRV_GIT_BRANCH "${SIMRV_BRANCH_OUTPUT}")
    endif()
  endif()

  if(SIMRV_GIT_SHA STREQUAL "unknown")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE SIMRV_SHA_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(SIMRV_SHA_OUTPUT)
      set(SIMRV_GIT_SHA "${SIMRV_SHA_OUTPUT}")
    endif()
  endif()
endif()

message(
  STATUS
    "SimRV version: ${SIMRV_VERSION} (${SIMRV_GIT_BRANCH}@${SIMRV_GIT_SHA})")

set(SIMRV_PROJECT_NAME "${PROJECT_NAME}")
set(SIMRV_PROJECT_DESCRIPTION "${PROJECT_DESCRIPTION}")

configure_file(${PROJECT_SOURCE_DIR}/include/simrv/core/BuildInfo.hpp.in
               ${PROJECT_BINARY_DIR}/generated/simrv/core/BuildInfo.hpp @ONLY)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The project has no C++ module sources. Avoid a dependency-scan action for every translation unit.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIMRV_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
option(SIMRV_ENABLE_INSTRUMENTATION
       "Enable CMake build and test instrumentation profiling (trace events)"
       OFF)

if(SIMRV_ENABLE_INSTRUMENTATION)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.3")
    cmake_instrumentation(
      API_VERSION
      1
      DATA_VERSION
      1
      HOOKS
      postGenerate
      postCMakeBuild
      postCTest
      OPTIONS
      staticSystemInformation
      dynamicSystemInformation
      trace)
    message(
      STATUS
        "CMake 4.3 build and test instrumentation enabled (traces in .cmake/instrumentation/v1/data/)"
    )
  else()
    message(WARNING "SIMRV_ENABLE_INSTRUMENTATION requires CMake 4.3 or newer")
  endif()
endif()

option(SIMRV_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
option(
  SIMRV_ALLOW_UNQUALIFIED_TOOLCHAIN
  "Allow release-only packaging builds below the supported compiler baseline"
  OFF)
option(SIMRV_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SIMRV_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SIMRV_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(
  SIMRV_NATIVE_HOST_OPTIMIZATIONS
  "Tune a Release build for the host CPU; do not use for redistributable binaries"
  OFF)
option(SIMRV_X86_64_V3_BASELINE
       "Use the x86-64-v3 ISA baseline for non-native x86-64 Release builds" ON)
option(SIMRV_USE_LIBCXX "Use LLVM libc++ standard library with Clang" OFF)
set(SIMRV_VECTOR_TESTS_DIR
    ""
    CACHE PATH "Path to a built chipsalliance/riscv-vector-tests checkout")

if(SIMRV_ENABLE_TSAN AND (SIMRV_ENABLE_ASAN OR SIMRV_ENABLE_UBSAN))
  message(
    FATAL_ERROR "ThreadSanitizer must use a separate build from ASan/UBSan")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT "cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  message(
    FATAL_ERROR
      "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not support required C++23 features"
  )
endif()

option(SIMRV_PREFER_MOLD "Prefer mold linker for fast links when available" ON)
option(SIMRV_PREFER_LLD "Prefer lld for Clang links when available" ON)
option(SIMRV_ENABLE_LTO "Enable link-time optimization for Release builds" ON)

set(SIMRV_LINKER_NAME "system")
if(SIMRV_PREFER_MOLD)
  find_program(SIMRV_MOLD_EXECUTABLE NAMES mold)
  if(SIMRV_MOLD_EXECUTABLE)
    add_link_options($<$<LINK_LANGUAGE:CXX>:-fuse-ld=mold>)
    set(SIMRV_LINKER_NAME "mold")
    message(STATUS "Fast linking enabled with mold.")
  endif()
endif()

if(SIMRV_LINKER_NAME STREQUAL "system"
   AND SIMRV_PREFER_LLD
   AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  find_program(SIMRV_LLD_EXECUTABLE NAMES ld.lld)
  if(SIMRV_LLD_EXECUTABLE)
    add_link_options($<$<LINK_LANGUAGE:CXX>:-fuse-ld=lld>)
    set(SIMRV_LINKER_NAME "lld")
    message(STATUS "Linking enabled with lld.")
  endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND SIMRV_ENABLE_LTO)
  if(SIMRV_LINKER_NAME MATCHES "lld|mold" AND CMAKE_CXX_COMPILER_ID STREQUAL
                                              "Clang")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    message(
      STATUS "Release LTO enabled across the runtime with ${SIMRV_LINKER_NAME}."
    )
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    include(CheckIPOSupported)
    check_ipo_supported(
      RESULT SIMRV_IPO_ENABLED
      OUTPUT SIMRV_IPO_ERROR
      LANGUAGES CXX)
    if(SIMRV_IPO_ENABLED)
      set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
      message(STATUS "Release LTO enabled across the runtime.")
    else()
      message(STATUS "Release LTO disabled: ${SIMRV_IPO_ERROR}")
    endif()
  else()
    message(STATUS "Release LTO disabled: Clang requires lld or mold.")
  endif()
endif()

# GCC 16 is the minimum supported GNU toolchain. It is the GNU baseline
# validated for this C++23 codebase; use Clang 22+ when GNU 16+ is unavailable.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION
                                            VERSION_LESS 16
   AND NOT SIMRV_ALLOW_UNQUALIFIED_TOOLCHAIN)
  message(
    FATAL_ERROR
      "GCC 16 or newer is required (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION
                                              VERSION_LESS 22
   AND NOT SIMRV_ALLOW_UNQUALIFIED_TOOLCHAIN)
  message(
    FATAL_ERROR
      "Clang 22 or newer is required (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()

# XLEN option (32 or 64) – compile-time selection
set(SIMRV_XLEN
    "64"
    CACHE STRING "Target XLEN (32 or 64)")
set_property(CACHE SIMRV_XLEN PROPERTY STRINGS "32" "64")
if(NOT SIMRV_XLEN STREQUAL "32" AND NOT SIMRV_XLEN STREQUAL "64")
  message(FATAL_ERROR "SIMRV_XLEN must be 32 or 64 (got: ${SIMRV_XLEN})")
endif()
set(_SIMRV_LEGACY_LINUX_IMAGES_DIR
    "${PROJECT_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}")
set(_SIMRV_DEFAULT_LINUX_IMAGES_DIR
    "${PROJECT_SOURCE_DIR}/linux-images/rv${SIMRV_XLEN}/alpine-gui")
if(NOT DEFINED SIMRV_LINUX_IMAGES_DIR
   OR "${SIMRV_LINUX_IMAGES_DIR}" STREQUAL "${_SIMRV_LEGACY_LINUX_IMAGES_DIR}")
  set(SIMRV_LINUX_IMAGES_DIR
      "${_SIMRV_DEFAULT_LINUX_IMAGES_DIR}"
      CACHE PATH "Path to reproducible Linux test images" FORCE)
else()
  set(SIMRV_LINUX_IMAGES_DIR
      "${SIMRV_LINUX_IMAGES_DIR}"
      CACHE PATH "Path to reproducible Linux test images")
endif()

set(SIMRV_DRAM_SIZE_MB
    "256"
    CACHE STRING "DRAM size in megabytes (must be power of 2)")
set(SIMRV_SPIKE_PATH
    "spike"
    CACHE
      STRING
      "Path to Spike binary for lockstep co-simulation (runtime default, not used at build time)"
)

set(SIMRV_SOURCE_DATE_EPOCH_JSON "null")
if(DEFINED ENV{SOURCE_DATE_EPOCH} AND "$ENV{SOURCE_DATE_EPOCH}" MATCHES "^[0-9]+$")
  set(SIMRV_SOURCE_DATE_EPOCH_JSON "$ENV{SOURCE_DATE_EPOCH}")
endif()
configure_file(
  cmake/SimRVBuildMetadata.json.in
  "${CMAKE_CURRENT_BINARY_DIR}/SimRVBuildMetadata.json"
  @ONLY)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(SimRVOptions)

add_subdirectory(src/pipeline)
add_subdirectory(src/execute)
add_subdirectory(src/memory)
add_subdirectory(src/device)
add_subdirectory(src/debug)
add_subdirectory(src/tui)
# Runtime is the composition target and must be declared after all component
# object targets.
add_subdirectory(src/core)

add_executable(SimRV src/Main.cpp)
add_executable(SimRV::cli ALIAS SimRV)
target_include_directories(SimRV PRIVATE ${PROJECT_SOURCE_DIR}/include
                                         ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(SimRV PRIVATE simrv-runtime)
simrv_apply_options(SimRV)

check_cxx_compiler_flag("-frounding-math" COMPILER_SUPPORTS_FROUNDING_MATH)
if(COMPILER_SUPPORTS_FROUNDING_MATH)
  # Only the host-FP execution units alter the floating-point environment.
  # Applying strict rounding semantics to the entire simulator prevents useful
  # optimization in the integer, memory, and scheduling hot paths.
  set_source_files_properties(
    src/execute/ExecuteUnitFp.cpp src/execute/vector/VectorFloat.cpp
    PROPERTIES COMPILE_OPTIONS "-frounding-math")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND SIMRV_NATIVE_HOST_OPTIMIZATIONS)
  message(STATUS "Native-host Release tuning enabled; binary is not portable.")
endif()

include("${PROJECT_SOURCE_DIR}/cmake/SimRVTests.cmake")

include("${PROJECT_SOURCE_DIR}/cmake/SimRVDeveloperTargets.cmake")

include("${PROJECT_SOURCE_DIR}/cmake/SimRVPackaging.cmake")
