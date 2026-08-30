include(CheckCXXCompilerFlag)

add_library(simrv-project-options INTERFACE)
target_compile_features(simrv-project-options INTERFACE cxx_std_23)
target_compile_definitions(simrv-project-options INTERFACE
  SIMRV_XLEN=${SIMRV_XLEN}
  SIMRV_DRAM_SIZE_MB=${SIMRV_DRAM_SIZE_MB}
  SIMRV_DISK_SIZE_MB=${SIMRV_DISK_SIZE_MB}
)
target_compile_options(simrv-project-options INTERFACE
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
)

if(SIMRV_WARNINGS_AS_ERRORS)
  target_compile_options(simrv-project-options INTERFACE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Werror>
  )
endif()

if(SIMRV_ENABLE_ASAN OR SIMRV_ENABLE_UBSAN OR SIMRV_ENABLE_TSAN)
  set(_simrv_sanitizers)
  if(SIMRV_ENABLE_ASAN)
    list(APPEND _simrv_sanitizers address)
  endif()
  if(SIMRV_ENABLE_UBSAN)
    list(APPEND _simrv_sanitizers undefined)
  endif()
  if(SIMRV_ENABLE_TSAN)
    list(APPEND _simrv_sanitizers thread)
  endif()
  string(JOIN "," _simrv_sanitizer_flags ${_simrv_sanitizers})
  target_compile_options(simrv-project-options INTERFACE
    -fsanitize=${_simrv_sanitizer_flags} -fno-omit-frame-pointer
  )
  target_link_options(simrv-project-options INTERFACE
    -fsanitize=${_simrv_sanitizer_flags}
  )
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$" AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang" AND
   CMAKE_BUILD_TYPE STREQUAL "Release" AND
   SIMRV_X86_64_V3_BASELINE AND
   NOT SIMRV_NATIVE_HOST_OPTIMIZATIONS)
  check_cxx_compiler_flag("-march=x86-64-v3" SIMRV_SUPPORTS_X86_64_V3)
  if(SIMRV_SUPPORTS_X86_64_V3)
    target_compile_options(simrv-project-options INTERFACE -march=x86-64-v3)
  else()
    message(FATAL_ERROR "SIMRV_X86_64_V3_BASELINE is ON but the compiler rejects -march=x86-64-v3")
  endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND SIMRV_NATIVE_HOST_OPTIMIZATIONS)
  check_cxx_compiler_flag("-march=native" SIMRV_SUPPORTS_MARCH_NATIVE)
  check_cxx_compiler_flag("-mtune=native" SIMRV_SUPPORTS_MTUNE_NATIVE)
  if(SIMRV_SUPPORTS_MARCH_NATIVE)
    target_compile_options(simrv-project-options INTERFACE -march=native)
  endif()
  if(SIMRV_SUPPORTS_MTUNE_NATIVE)
    target_compile_options(simrv-project-options INTERFACE -mtune=native)
  endif()
endif()

function(simrv_apply_options target)
  target_link_libraries(${target} PRIVATE simrv-project-options)
  if(SIMRV_ENABLE_CLANG_TIDY)
    set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY "clang-tidy")
  endif()
endfunction()

function(simrv_configure_native_test target)
  simrv_apply_options(${target})
  # Component object libraries may contain LTO bitcode.  Their native test consumers must use
  # the same link mode so the selected linker plugin materializes those objects.
  if(SIMRV_IPO_ENABLED)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
  target_include_directories(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_BINARY_DIR}/generated"
  )
endfunction()

function(simrv_add_component target)
  add_library(${target} OBJECT ${ARGN})
  target_include_directories(${target} PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/generated"
  )
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  simrv_apply_options(${target})
endfunction()
