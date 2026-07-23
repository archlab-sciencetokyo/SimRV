# scripts/RunIsaTest.cmake
# Native CMake script to prepare and run a single RISC-V ISA test case.

if(NOT EXISTS "${ELF_PATH}")
  message(FATAL_ERROR "test binary not found: ${ELF_PATH}")
endif()

get_filename_component(test_name "${ELF_PATH}" NAME)
set(bin_path "${WORK_DIR}/${test_name}.bin")
file(MAKE_DIRECTORY "${WORK_DIR}")

# 1. Run NM to lookup 'tohost' address
set(tohost_addr "${DEFAULT_TOHOST_ADDR}")
if(EXISTS "${NM_BIN}")
  execute_process(
    COMMAND ${NM_BIN} ${ELF_PATH}
    OUTPUT_VARIABLE nm_out
    RESULT_VARIABLE nm_res
    ERROR_VARIABLE nm_err
  )
  if(nm_res EQUAL 0)
    if(nm_out MATCHES "([0-9a-fA-F]+)[ \t]+[DdTtGgBb][ \t]+tohost")
      set(tohost_addr "0x${CMAKE_MATCH_1}")
    endif()
  endif()
endif()

if(tohost_addr STREQUAL "${DEFAULT_TOHOST_ADDR}" AND EXISTS "${ELF_PATH}.dump")
  file(READ "${ELF_PATH}.dump" dump_content)
  if(dump_content MATCHES "([0-9a-fA-F]+)[ \t]+<tohost>")
    set(tohost_addr "0x${CMAKE_MATCH_1}")
  endif()
endif()

# 2. Run SimRV directly with ELF binary
set(SIMRV_ARGS -a --cli -m ${ELF_PATH} -e ${END_INSNS} -b -H ${tohost_addr})
if(LOCKSTEP)
  list(APPEND SIMRV_ARGS --lockstep)
  list(APPEND SIMRV_ARGS --spike-elf ${ELF_PATH})
  if(SPIKE_BIN)
    list(APPEND SIMRV_ARGS --spike-bin ${SPIKE_BIN})
  endif()
endif()

execute_process(
  COMMAND ${SIMRV_BIN} ${SIMRV_ARGS}
  TIMEOUT ${TIMEOUT_SECS}
  OUTPUT_VARIABLE sim_out
  ERROR_VARIABLE sim_err
  RESULT_VARIABLE sim_res
)

# 4. Check results
if(sim_out MATCHES "ISA TEST PASS")
  message(STATUS "PASS: ${test_name} (HTIF tohost: ${tohost_addr})")
elseif(sim_out MATCHES "ISA TEST FAIL")
  message(FATAL_ERROR "FAIL: ${test_name}\nSimulator stdout:\n${sim_out}\nSimulator stderr:\n${sim_err}")
else()
  message(FATAL_ERROR "FAIL: ${test_name} (Execution error/timeout/no pass marker)\nResult: ${sim_res}\nSimulator stdout:\n${sim_out}\nSimulator stderr:\n${sim_err}")
endif()
