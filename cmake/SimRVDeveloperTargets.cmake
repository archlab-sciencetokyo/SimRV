if(SIMRV_ENABLE_LINUX_TOOLS)
  function(simrv_add_linux_image_target target comment)
    add_custom_target(
      ${target}
      COMMAND "${CMAKE_COMMAND}" -E env SIMRV_USE_LOCAL_SCRATCH=1 bash
              "${PROJECT_SOURCE_DIR}/scripts/build-linux-image.sh" --arch
              "rv${SIMRV_XLEN}" ${ARGN}
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      COMMENT "${comment}"
      VERBATIM USES_TERMINAL)
  endfunction()

  simrv_add_linux_image_target(
    linux-images "Building RV${SIMRV_XLEN}GC Linux kernel and rootfs images")
  simrv_add_linux_image_target(
    linux-images-buildroot "Building RV${SIMRV_XLEN}GC Buildroot FPGA Linux profile"
    --rootfs buildroot --profile fpga)
  simrv_add_linux_image_target(
    linux-images-debug "Building RV${SIMRV_XLEN}GC expanded Alpine debug Linux profile"
    --rootfs alpine --profile debug)
endif()

add_custom_target(
  integration-gate
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
          --output-on-failure -L "gate"
  DEPENDS SimRV
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  COMMENT "Running the complete SimRV integration gate"
  VERBATIM USES_TERMINAL)
if(BUILD_TESTING)
  get_property(
    SIMRV_NATIVE_TEST_TARGETS
    GLOBAL
    PROPERTY SIMRV_NATIVE_TEST_TARGETS)
  if(SIMRV_NATIVE_TEST_TARGETS)
    add_dependencies(integration-gate ${SIMRV_NATIVE_TEST_TARGETS})
  endif()
endif()

if(SIMRV_ENABLE_TOOLING)
  add_custom_target(
    release-check
    COMMAND "${PYTHON_BIN}" "${PROJECT_SOURCE_DIR}/scripts/release_check.py" --binary
            $<TARGET_FILE:SimRV>
    DEPENDS SimRV
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Validating SimRV release metadata and binary"
    VERBATIM)
endif()

if(SIMRV_VECTOR_TESTS_DIR
   AND EXISTS "${SIMRV_VECTOR_TESTS_DIR}"
   AND RISCV_GCC)
  add_custom_target(
    vector-gate
    COMMAND
      "${PYTHON_BIN}" "${PROJECT_SOURCE_DIR}/scripts/run_vector_tests.py" --simrv
      $<TARGET_FILE:SimRV> --xlen ${SIMRV_XLEN} --gcc ${RISCV_GCC} --objcopy
      ${RISCV_OBJCOPY} --nm ${RISCV_NM} --work-dir
      ${CMAKE_CURRENT_BINARY_DIR}/vector-tests-gen --vector-tests-dir
      ${SIMRV_VECTOR_TESTS_DIR}
    DEPENDS SimRV
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Running Full Vector validation gate")
endif()

if(SIMRV_ENABLE_LINUX_TOOLS)
  function(simrv_add_linux_run_target target comment)
    add_custom_target(
      ${target}
      COMMAND "${CMAKE_COMMAND}"
              -D "SIMRV_BIN=$<TARGET_FILE:SimRV>"
              -D "SIMRV_FW_PAYLOAD=${SIMRV_LINUX_IMAGES_DIR}/opensbi-linux-payload.elf"
              -D "SIMRV_DISK_IMAGE=${SIMRV_LINUX_IMAGES_DIR}/rootfs.img"
              -D "SIMRV_DTB=${SIMRV_LINUX_IMAGES_DIR}/devicetree.dtb"
              ${ARGN}
              -P "${PROJECT_SOURCE_DIR}/cmake/SimRVRunLinux.cmake"
      DEPENDS SimRV
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      COMMENT "${comment}"
      VERBATIM USES_TERMINAL)
  endfunction()

  simrv_add_linux_run_target(run-tui "Booting RV${SIMRV_XLEN} Linux in TUI mode..."
                             -D SIMRV_TUI=ON)
  simrv_add_linux_run_target(run-linux
                             "Booting RV${SIMRV_XLEN} Linux in standard console mode...")
endif()

