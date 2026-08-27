if(NOT DEFINED SIMRV_SOURCE_DIR)
  message(FATAL_ERROR "SIMRV_SOURCE_DIR is required")
endif()

file(READ "${SIMRV_SOURCE_DIR}/CMakeLists.txt" top_level_cmake)
foreach(forbidden IN ITEMS "LINK_GROUP" "SIMRV_SOURCES" "SIMRV_LEGACY_TARGET_GRAPH")
  if(top_level_cmake MATCHES "${forbidden}")
    message(FATAL_ERROR "Forbidden legacy build construct remains: ${forbidden}")
  endif()
endforeach()

set(adapter_sources
  "${SIMRV_SOURCE_DIR}/src/Main.cpp"
  "${SIMRV_SOURCE_DIR}/src/v3/*.cpp"
  "${SIMRV_SOURCE_DIR}/src/debug/*.cpp"
  "${SIMRV_SOURCE_DIR}/src/device/*.cpp"
  "${SIMRV_SOURCE_DIR}/src/device/mmio/*.cpp"
  "${SIMRV_SOURCE_DIR}/src/device/pci/*.cpp"
  "${SIMRV_SOURCE_DIR}/src/memory/*.cpp"
  "${SIMRV_SOURCE_DIR}/src/pipeline/*.cpp"
  "${SIMRV_SOURCE_DIR}/src/tui/*.cpp"
  "${SIMRV_SOURCE_DIR}/src/tui/modals/*.cpp"
  "${SIMRV_SOURCE_DIR}/src/tui/panels/*.cpp"
)
file(GLOB adapter_files ${adapter_sources})
foreach(source IN LISTS adapter_files)
  file(READ "${source}" contents)
  if(contents MATCHES "machine_?\\.(cpu|mmem|secondary_harts_|uart|tui|tracer|symbols|breakpoints|gdb_stub|spike_lockstep|s_platform_profile|s_net_mode)([^A-Za-z0-9_]|$)")
    message(FATAL_ERROR "Adapter bypasses Machine capabilities: ${source}")
  endif()
endforeach()

if(NOT top_level_cmake MATCHES "install\\(DIRECTORY include/simrv/v3")
  message(FATAL_ERROR "The install surface must be rooted at include/simrv/v3")
endif()
if(top_level_cmake MATCHES "install\\(DIRECTORY include/simrv/(core|memory|device|tui|debug|pipeline)")
  message(FATAL_ERROR "An internal header directory is exposed by installation rules")
endif()
