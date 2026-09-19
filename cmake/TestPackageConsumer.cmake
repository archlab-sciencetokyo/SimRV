if(NOT DEFINED SIMRV_BINARY_DIR OR NOT DEFINED SIMRV_SOURCE_DIR)
  message(FATAL_ERROR "SIMRV_BINARY_DIR and SIMRV_SOURCE_DIR are required")
endif()

set(consumer_source "${SIMRV_SOURCE_DIR}/tests/package-consumer")
set(consumer_root "${SIMRV_BINARY_DIR}/package-consumer-check")
set(install_root "${consumer_root}/install")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${consumer_source}" -B
          "${consumer_root}/build-tree" -D "SimRV_DIR=${SIMRV_BINARY_DIR}"
  RESULT_VARIABLE build_tree_configure_result)
if(NOT build_tree_configure_result EQUAL 0)
  message(
    FATAL_ERROR
      "Build-tree package consumer configure failed: ${build_tree_configure_result}"
  )
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env CCACHE_DISABLE=1 "${CMAKE_COMMAND}" --build
          "${consumer_root}/build-tree" RESULT_VARIABLE build_tree_build_result)
if(NOT build_tree_build_result EQUAL 0)
  message(
    FATAL_ERROR
      "Build-tree package consumer build failed: ${build_tree_build_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${SIMRV_BINARY_DIR}" --prefix
          "${install_root}" RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "SimRV install step failed: ${install_result}")
endif()

foreach(tool IN ITEMS simrv-cpu-wizard simrv-tune simrv-parity simrv-benchmark)
  if(NOT EXISTS "${install_root}/bin/${tool}")
    message(
      FATAL_ERROR "Expected installed tool ${tool} in ${install_root}/bin")
  endif()
endforeach()

if(NOT EXISTS "${install_root}/share/man/man1/simrv.1")
  message(
    FATAL_ERROR
      "Expected installed man page at ${install_root}/share/man/man1/simrv.1")
endif()

file(GLOB installed_guides "${install_root}/share/doc/*/USER_GUIDE.md")
if(NOT installed_guides)
  message(
    FATAL_ERROR "Expected installed USER_GUIDE.md in ${install_root}/share/doc")
endif()

execute_process(
  COMMAND "${install_root}/bin/simrv-parity" --list-targets
  RESULT_VARIABLE parity_check_result
  OUTPUT_VARIABLE parity_check_output)
if(NOT parity_check_result EQUAL 0)
  message(
    FATAL_ERROR
      "Installed simrv-parity execution failed: ${parity_check_result}\n${parity_check_output}"
  )
endif()

file(GLOB package_dirs "${install_root}/lib*/cmake/SimRV")
list(LENGTH package_dirs package_dir_count)
if(NOT package_dir_count EQUAL 1)
  message(
    FATAL_ERROR
      "Expected one installed SimRV package directory, found: ${package_dirs}")
endif()
list(GET package_dirs 0 package_dir)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${consumer_source}" -B "${consumer_root}/build"
          -D "SimRV_DIR=${package_dir}" RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(
    FATAL_ERROR
      "Installed-package consumer configure failed: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env CCACHE_DISABLE=1 "${CMAKE_COMMAND}" --build
          "${consumer_root}/build" RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(
    FATAL_ERROR "Installed-package consumer build failed: ${build_result}")
endif()
