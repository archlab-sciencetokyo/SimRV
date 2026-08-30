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

# MachineConfig is the only architectural configuration surface.  Keep the one latched MMU
# telemetry bit private/runtime-only, but reject reintroduction of legacy `s_*` option mirrors.
file(READ "${SIMRV_SOURCE_DIR}/include/simrv/core/Machine.hpp" machine_header)
if(machine_header MATCHES "s_(appmode|tuimode|high_contrast|class_mode|debugmode|debug_mode|dlog_mode|traplog_mode|use_disk|use_mix|bp_trace|misa_override|multithreaded|num_harts|smp_quantum|smp_multithreaded|dram_size|mouse_sensitivity|gdb_mode|gdb_port|lockstep_mode|spike_bin|spike_elf|start_pc|strace|fincnt|trace_begin|trace_end|enabletimer|memimg|isatest_tohost|misa_profile|misa_xlen|vlen|fn_memimg|fn_dskimg|fn_dvtree|fn_traplog|fn_cpuconfig|pipeline_type)")
  message(FATAL_ERROR "Legacy Machine configuration mirror reintroduced; use MachineConfig")
endif()

if(NOT top_level_cmake MATCHES "install\\(DIRECTORY include/simrv")
  message(FATAL_ERROR "The install surface must be rooted at include/simrv")
endif()
