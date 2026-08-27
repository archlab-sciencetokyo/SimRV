if(NOT DEFINED SIMRV_BINARY_DIR OR NOT DEFINED SIMRV_SOURCE_DIR)
  message(FATAL_ERROR "SIMRV_BINARY_DIR and SIMRV_SOURCE_DIR are required")
endif()

set(consumer_source "${SIMRV_SOURCE_DIR}/tests/package-consumer")
set(consumer_root "${SIMRV_BINARY_DIR}/package-consumer-check")
set(install_root "${consumer_root}/install")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${consumer_source}" -B "${consumer_root}/build-tree"
          -D "SimRV_DIR=${SIMRV_BINARY_DIR}"
  RESULT_VARIABLE build_tree_configure_result
)
if(NOT build_tree_configure_result EQUAL 0)
  message(FATAL_ERROR "Build-tree package consumer configure failed: ${build_tree_configure_result}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_root}/build-tree"
  RESULT_VARIABLE build_tree_build_result
)
if(NOT build_tree_build_result EQUAL 0)
  message(FATAL_ERROR "Build-tree package consumer build failed: ${build_tree_build_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${SIMRV_BINARY_DIR}" --prefix "${install_root}"
  RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "SimRV install step failed: ${install_result}")
endif()

file(GLOB package_dirs "${install_root}/lib*/cmake/SimRV")
list(LENGTH package_dirs package_dir_count)
if(NOT package_dir_count EQUAL 1)
  message(FATAL_ERROR "Expected one installed SimRV package directory, found: ${package_dirs}")
endif()
list(GET package_dirs 0 package_dir)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${consumer_source}" -B "${consumer_root}/build"
          -D "SimRV_DIR=${package_dir}"
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Installed-package consumer configure failed: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_root}/build"
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Installed-package consumer build failed: ${build_result}")
endif()
