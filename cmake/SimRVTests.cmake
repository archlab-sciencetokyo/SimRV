# Toolchain detection for testing. Host-specific defaults are intentionally
# avoided: release dependencies must be supplied through cache variables or
# PATH.
if(BUILD_TESTING)
  if(NOT DEFINED RISCV_PREFIX)
    if(DEFINED ENV{RISCV_PREFIX})
      set(RISCV_PREFIX "$ENV{RISCV_PREFIX}")
    else()
      set(RISCV_PREFIX "")
    endif()
  endif()

  set(OBJCOPY_NAMES)
  set(NM_NAMES)
  if(NOT "${RISCV_PREFIX}" STREQUAL "")
    list(APPEND OBJCOPY_NAMES "${RISCV_PREFIX}objcopy")
    list(APPEND NM_NAMES "${RISCV_PREFIX}nm")
  endif()
  list(APPEND OBJCOPY_NAMES "riscv64-unknown-elf-objcopy"
       "riscv32-unknown-elf-objcopy" "riscv-none-elf-objcopy" "objcopy")
  list(APPEND NM_NAMES "riscv64-unknown-elf-nm" "riscv32-unknown-elf-nm"
       "riscv-none-elf-nm" "nm")

  find_program(
    RISCV_OBJCOPY
    NAMES ${OBJCOPY_NAMES}
    DOC "Path to objcopy tool for binary extraction")
  find_program(
    RISCV_NM
    NAMES ${NM_NAMES}
    DOC "Path to nm tool for symbol lookup")
  find_program(SPIKE_BIN spike
               DOC "Path to Spike binary for lockstep co-simulation")

  set(ISA_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/.isa_tmp")
  file(MAKE_DIRECTORY "${ISA_WORK_DIR}")

  function(simrv_add_runtime_test target source test_name labels)
    add_executable(${target} ${source})
    set_property(GLOBAL APPEND PROPERTY SIMRV_NATIVE_TEST_TARGETS ${target})
    target_link_libraries(${target} PRIVATE simrv-runtime)
    simrv_configure_native_test(${target})
    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES LABELS "${labels}" TIMEOUT 60)
  endfunction()

  # Fast, deterministic tests for the terminal parser, theme helpers, widgets,
  # and TUI layout framework.
  simrv_add_runtime_test(simrv-tui-tests tests/TuiTests.cpp
                         tui "gate;regress;tui")
  set_tests_properties(tui PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  if(SIMRV_ENABLE_TSAN)
    set_property(
      TEST tui
      APPEND
      PROPERTY LABELS "thread")
  endif()

  simrv_add_runtime_test(
    simrv-instruction-explainer-tests tests/InstructionExplainerTests.cpp
    instruction-explainer "gate;regress;core")
  simrv_add_runtime_test(simrv-pipeline-tests tests/PipelineTests.cpp pipeline
                         "gate;regress;pipeline;bpred")
  if(SIMRV_ENABLE_TSAN)
    set_property(
      TEST pipeline
      APPEND
      PROPERTY LABELS "thread")
  endif()

  simrv_add_runtime_test(
    simrv-core-semantics-tests tests/CoreSemanticsTests.cpp core-semantics
    "gate;regress;memory;mmio")
  simrv_add_runtime_test(simrv-memory-protection-tests tests/MemoryProtectionTests.cpp
                         memory-protection "gate;regress;core;memory;pmp")
  simrv_add_runtime_test(
    simrv-machine-config-tests tests/MachineConfigTests.cpp machine-config
    "gate;regress;core;config")
  simrv_add_runtime_test(simrv-gdb-stub-tests tests/GdbStubTests.cpp gdb-stub
                         "gate;regress;debug;thread")
  set_tests_properties(gdb-stub PROPERTIES TIMEOUT 30)
  simrv_add_runtime_test(
    simrv-platform-integration-tests tests/PlatformIntegrationTests.cpp platform-integration
    "gate;regress;semantic;device;mmio;pcie;aia")
  simrv_add_runtime_test(simrv-trace-tests tests/TraceTests.cpp architectural-trace
                         "gate;regress;core;trace")
  if(SIMRV_ENABLE_TSAN)
    set_property(
      TEST platform-integration
      APPEND
      PROPERTY LABELS thread)
  endif()
  function(simrv_add_test_plugin target output_name source)
    add_library(${target} SHARED ${source})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output_name})
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include")
    simrv_apply_options(${target})
  endfunction()

  simrv_add_test_plugin(simrv-test-cfu-plugin test-cfu-plugin tests/TestCfuPlugin.cpp)
  simrv_add_test_plugin(simrv-test-legacy-cfu-plugin test-legacy-cfu-plugin tests/TestLegacyCfuPlugin.c)
  simrv_add_test_plugin(simrv-test-wide-cfu-plugin test-wide-cfu-plugin tests/TestWideCfuPlugin.c)
  simrv_add_test_plugin(simrv-test-legacy-device-plugin test-legacy-device-plugin tests/TestLegacyDevicePlugin.c)
  simrv_add_test_plugin(simrv-test-raw-cfu-plugin test-raw-cfu-plugin tests/TestRawCfuPlugin.c)
  simrv_add_test_plugin(simrv-test-mmc-plugin test-device-plugin tests/TestDevicePlugin.cpp)
  simrv_add_test_plugin(simrv-test-serial-plugin test-serial-plugin tests/TestSerialPlugin.cpp)
  simrv_add_test_plugin(simrv-demo-timer demo-timer examples/plugins/timer/timer_plugin.c)
  simrv_add_test_plugin(simrv-demo-dma-accel demo-dma-accel examples/plugins/dma_accel/dma_accel_plugin.c)

  simrv_add_runtime_test(
    simrv-cfu-integration-tests tests/CfuIntegrationTests.cpp
    cfu-integration "gate;regress;pipeline;cfu")
  target_compile_definitions(
    simrv-cfu-integration-tests
    PRIVATE TEST_CFU_PLUGIN_PATH="$<TARGET_FILE:simrv-test-cfu-plugin>"
            TEST_LEGACY_CFU_PLUGIN_PATH="$<TARGET_FILE:simrv-test-legacy-cfu-plugin>"
            TEST_WIDE_CFU_PLUGIN_PATH="$<TARGET_FILE:simrv-test-wide-cfu-plugin>"
            TEST_RAW_CFU_PLUGIN_PATH="$<TARGET_FILE:simrv-test-raw-cfu-plugin>")
  add_dependencies(simrv-cfu-integration-tests simrv-test-cfu-plugin simrv-test-raw-cfu-plugin simrv-test-legacy-cfu-plugin simrv-test-wide-cfu-plugin)

  simrv_add_runtime_test(simrv-microarchitecture-tests tests/MicroarchitectureTests.cpp microarchitecture
                         "gate;regress;pipeline;rvcomp")
  simrv_add_runtime_test(simrv-device-plugin-tests tests/DevicePluginTests.cpp device-plugins
                         "gate;regress;device;plugin")
  target_compile_definitions(simrv-device-plugin-tests PRIVATE
                             TEST_SERIAL_PLUGIN_PATH="$<TARGET_FILE:simrv-test-serial-plugin>"
                             TEST_LEGACY_DEVICE_PLUGIN_PATH="$<TARGET_FILE:simrv-test-legacy-device-plugin>"
                             TEST_TIMER_PLUGIN_PATH="$<TARGET_FILE:simrv-demo-timer>"
                             TEST_DMA_PLUGIN_PATH="$<TARGET_FILE:simrv-demo-dma-accel>")
  add_dependencies(simrv-device-plugin-tests simrv-test-serial-plugin simrv-demo-timer simrv-demo-dma-accel simrv-test-legacy-device-plugin)
  if(TARGET simrv-mmc)
    add_dependencies(simrv-microarchitecture-tests simrv-mmc)
  endif()
  if(TARGET simrv-rvcomp-uart)
    add_dependencies(simrv-microarchitecture-tests simrv-rvcomp-uart simrv-reset)
    target_compile_definitions(simrv-microarchitecture-tests PRIVATE SIMRV_TEST_RVCOMP_UART=1)
  endif()

  simrv_add_runtime_test(
    simrv-cpu-model-loader-tests tests/CpuModelLoaderTests.cpp cpu-model-loader
    "gate;regress;pipeline;model")

  simrv_add_runtime_test(
    simrv-simulator-standards-tests tests/SimulatorStandardsTests.cpp
    simulator-standards "gate;regress;core;sbi;isa")

  simrv_add_runtime_test(
    simrv-rva23-profile-tests tests/Rva23ProfileTests.cpp
    rva23-profile "gate;regress;core;isa;cache")

  simrv_add_runtime_test(
    simrv-fdt-generator-tests tests/FdtGeneratorTests.cpp
    fdt-generator "gate;regress;core;fdt;platform")

  # Architectural equivalence is a first-class native gate. The fixture
  # deliberately exercises functional execution and both CA pipelines while
  # excluding cache/cycle/UI observations.
  add_test(NAME semantic-equivalence COMMAND simrv-platform-integration-tests
                                             --semantic-equivalence)
  set_tests_properties(semantic-equivalence
                       PROPERTIES LABELS "gate;semantic;equivalence")

  # Native CLI tests
  add_test(NAME cli-help COMMAND $<TARGET_FILE:SimRV> -h)
  set_tests_properties(cli-help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:"
                                           LABELS "gate;regress;cli")
  add_test(NAME cli-help-isa COMMAND $<TARGET_FILE:SimRV> -h)
  set_tests_properties(cli-help-isa PROPERTIES PASS_REGULAR_EXPRESSION "--isa"
                                               LABELS "gate;regress;cli")
  add_test(NAME cli-isa-info COMMAND $<TARGET_FILE:SimRV> --isa-info)
  set_tests_properties(cli-isa-info PROPERTIES PASS_REGULAR_EXPRESSION "RV64GCBV"
                                               LABELS "gate;regress;cli;release")
  add_test(NAME cli-doctor COMMAND $<TARGET_FILE:SimRV> --doctor)
  set_tests_properties(cli-doctor PROPERTIES PASS_REGULAR_EXPRESSION "SimRV environment diagnostics"
                                             LABELS "gate;regress;cli;release")
  add_test(NAME cli-doctor-graphics
           COMMAND $<TARGET_FILE:SimRV> --doctor)
  set_tests_properties(
    cli-doctor-graphics
    PROPERTIES ENVIRONMENT "TERM=dumb;SIMRV_GRAPHICS=auto;SIMRV_ENABLE_DISPLAY="
               PASS_REGULAR_EXPRESSION "Sixel graphics[ ]*:[ ]*not detected"
               LABELS "gate;regress;cli;tui")
  add_test(NAME cli-summary-rejects-stdout COMMAND $<TARGET_FILE:SimRV> --summary -)
  set_tests_properties(
    cli-summary-rejects-stdout
    PROPERTIES PASS_REGULAR_EXPRESSION "stdout is reserved for guest UART"
               LABELS "gate;regress;cli")
  add_test(NAME cli-events-reject-stdout COMMAND $<TARGET_FILE:SimRV> --events -)
  set_tests_properties(
    cli-events-reject-stdout
    PROPERTIES PASS_REGULAR_EXPRESSION "stdout is reserved for guest UART"
               LABELS "gate;regress;cli")

  add_test(NAME cli-license COMMAND $<TARGET_FILE:SimRV> --license)
  set_tests_properties(
    cli-license
    PROPERTIES PASS_REGULAR_EXPRESSION "Licensed under the MIT License" LABELS
               "gate;regress;cli;license")

  add_test(NAME cli-invalid-option COMMAND $<TARGET_FILE:SimRV> -Z)
  set_tests_properties(
    cli-invalid-option PROPERTIES PASS_REGULAR_EXPRESSION "unknown option"
                                  LABELS "gate;regress;cli")

  add_test(NAME cli-invalid-pipeline COMMAND $<TARGET_FILE:SimRV> --pipeline
                                             dual)
  set_tests_properties(cli-invalid-pipeline PROPERTIES WILL_FAIL TRUE LABELS
                                                       "gate;regress;cli")

  add_test(NAME cli-gdb-tui-conflict COMMAND $<TARGET_FILE:SimRV> --tui --gdb)
  set_tests_properties(
    cli-gdb-tui-conflict
    PROPERTIES PASS_REGULAR_EXPRESSION "--gdb and --tui are mutually exclusive"
               LABELS "gate;regress;cli;debug")

  add_test(NAME cli-gdb-lockstep-conflict COMMAND $<TARGET_FILE:SimRV> --gdb
                                                  --lockstep)
  set_tests_properties(
    cli-gdb-lockstep-conflict
    PROPERTIES PASS_REGULAR_EXPRESSION
               "--gdb and --lockstep are mutually exclusive" LABELS
               "gate;regress;cli;debug")

  # Release and metadata native verification suite using Boost.UT
  simrv_add_runtime_test(simrv-release-tests tests/ReleaseTests.cpp
                         release-suite "release;tools;gate")
  # End-to-End (E2E) native testing suite using Boost.UT
  simrv_add_runtime_test(simrv-e2e-tests tests/E2ETests.cpp
                         e2e-suite "regress;e2e")
  set_tests_properties(e2e-suite PROPERTIES TIMEOUT 120)

  # RVA23 Architectural Compatibility Tests (ACT)
  if(PYTHON_BIN)
    if(SIMRV_XLEN STREQUAL "64")
      add_test(NAME rva23-act
               COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/run_act_tests.py
                       --simrv $<TARGET_FILE:SimRV>
                       --xlen ${SIMRV_XLEN}
                       --work-dir ${CMAKE_CURRENT_BINARY_DIR}/act-qualification)
      set_tests_properties(rva23-act
                           PROPERTIES LABELS "act;rva23;profile" TIMEOUT 45)
    endif()

    add_test(NAME act-runner-tests
             COMMAND ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/run_act_tests.py --help)
    set_tests_properties(act-runner-tests
                         PROPERTIES LABELS "act;tool" TIMEOUT 15)
  endif()

  # Optional App/Linux smoke tests
  if(DEFINED ENV{SIMRV_APP_IMG})
    add_test(NAME app-smoke COMMAND $<TARGET_FILE:SimRV> -m $ENV{SIMRV_APP_IMG}
                                    -b -e 200000)
    set_tests_properties(app-smoke PROPERTIES TIMEOUT 20 LABELS "external;app")
  endif()

  if(DEFINED ENV{SIMRV_LINUX_MEM_IMG} AND DEFINED ENV{SIMRV_LINUX_DISK_IMG})
    set(LINUX_BOOT_CMD
        $<TARGET_FILE:SimRV>
        -m
        $ENV{SIMRV_LINUX_MEM_IMG}
        -D
        $ENV{SIMRV_LINUX_DISK_IMG}
        -e
        1000000)
    if(DEFINED ENV{SIMRV_LINUX_DTB})
      list(APPEND LINUX_BOOT_CMD -f $ENV{SIMRV_LINUX_DTB})
    endif()
    add_test(NAME linux-boot COMMAND ${LINUX_BOOT_CMD})
    set_tests_properties(linux-boot PROPERTIES TIMEOUT 60 LABELS
                                               "external;linux")
  endif()

  function(simrv_add_linux_pty_test name environment timeout labels)
    add_test(NAME "${name}"
             COMMAND "${PYTHON_BIN}" "${CMAKE_SOURCE_DIR}/scripts/test_linux_pty.py")
    set_tests_properties(
      "${name}"
      PROPERTIES ENVIRONMENT "${environment}" TIMEOUT "${timeout}"
                 RUN_SERIAL TRUE LABELS "${labels}")
  endfunction()

  set(TEST_IMAGES_DIR "${SIMRV_LINUX_IMAGES_DIR}")
  if(EXISTS "${TEST_IMAGES_DIR}/opensbi-linux-payload.elf")
    set(LINUX_PTY_COMMON_ENV
        "SIMRV_BIN=$<TARGET_FILE:SimRV>;SIMRV_IMAGES_DIR=${TEST_IMAGES_DIR}")
    set(LINUX_PTY_PROFILE_ENV
        "${LINUX_PTY_COMMON_ENV};SIMRV_LINUX_DTB=${TEST_IMAGES_DIR}/devicetree.dtb")
    set(LINUX_PTY_DYNAMIC_ENV "${LINUX_PTY_COMMON_ENV};SIMRV_LINUX_DTB=${TEST_IMAGES_DIR}/devicetree.dtb")
    simrv_add_linux_pty_test(
      linux-boot-pty
      "${LINUX_PTY_PROFILE_ENV};SIMRV_TEST_INSTRUCTIONS=20000000000;SIMRV_TEST_TIMEOUT=300"
      200 "external;linux;pty;required")
    simrv_add_linux_pty_test(
      linux-privilege-pty
      "${LINUX_PTY_PROFILE_ENV};SIMRV_TEST_PRIVILEGE=1;SIMRV_TEST_INSTRUCTIONS=20000000000;SIMRV_TEST_TIMEOUT=300"
      200 "external;linux;pty;privilege")
    simrv_add_linux_pty_test(
      linux-ia-quantum-smp-pty
      "${LINUX_PTY_DYNAMIC_ENV};SIMRV_TEST_EXTRA_ARGS=--smp 2;SIMRV_TEST_EXPECT_CPUS=2;SIMRV_TEST_INSTRUCTIONS=12000000000;SIMRV_TEST_TIMEOUT=180"
      200 "external;linux;pty;ia;smp;quantum")
    simrv_add_linux_pty_test(
      linux-ca-boot-pty
      "${LINUX_PTY_COMMON_ENV};SIMRV_TEST_EXTRA_ARGS=--mode cycle-accurate;SIMRV_TEST_INSTRUCTIONS=12000000000;SIMRV_TEST_TIMEOUT=420"
      440 "external;linux;pty;ca")
    simrv_add_linux_pty_test(
      linux-ca-quantum-smp-pty
      "${LINUX_PTY_DYNAMIC_ENV};SIMRV_TEST_EXTRA_ARGS=--mode cycle-accurate --smp 2;SIMRV_TEST_EXPECT_CPUS=2;SIMRV_TEST_INSTRUCTIONS=12000000000;SIMRV_TEST_TIMEOUT=480"
      500 "external;linux;pty;ca;smp;quantum")
    simrv_add_linux_pty_test(
      linux-ca-mt-smp-pty
      "${LINUX_PTY_DYNAMIC_ENV};SIMRV_TEST_EXTRA_ARGS=--mode cycle-accurate --smp 2;SIMRV_TEST_EXPECT_CPUS=2;SIMRV_TEST_INSTRUCTIONS=12000000000;SIMRV_TEST_TIMEOUT=480"
      500 "external;linux;pty;ca;smp;mt;thread")
    simrv_add_linux_pty_test(
      linux-poweroff-quit-pty
      "${LINUX_PTY_PROFILE_ENV};SIMRV_TEST_LIFECYCLE=poweroff;SIMRV_TEST_INSTRUCTIONS=12000000000;SIMRV_TEST_TIMEOUT=180"
      200 "external;linux;pty;lifecycle;required")
    simrv_add_linux_pty_test(
      linux-poweroff-reboot-pty
      "${LINUX_PTY_PROFILE_ENV};SIMRV_TEST_LIFECYCLE=poweroff-reboot;SIMRV_TEST_INSTRUCTIONS=12000000000;SIMRV_TEST_TIMEOUT=180"
      200 "external;linux;pty;lifecycle;required")
  endif()

  # RISC-V tests and benchmarks location resolution:
  # 1. Explicit CMake cache variable: -DRISCV_TESTS_DIR=...
  # 2. Environment variable: RISCV_TESTS_DIR
  # 3. In-tree directory/submodule: tests/riscv-tests
  # 4. Sibling directory fallback (e.g. multi-project workspace): ../../tests/riscv-tests
  if(NOT DEFINED RISCV_TESTS_DIR OR "${RISCV_TESTS_DIR}" STREQUAL "")
    if(DEFINED ENV{RISCV_TESTS_DIR} AND NOT "$ENV{RISCV_TESTS_DIR}" STREQUAL "")
      set(RISCV_TESTS_DIR "$ENV{RISCV_TESTS_DIR}")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/tests/riscv-tests" AND IS_DIRECTORY "${CMAKE_SOURCE_DIR}/tests/riscv-tests")
      set(RISCV_TESTS_DIR "${CMAKE_SOURCE_DIR}/tests/riscv-tests")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/../../tests/riscv-tests" AND IS_DIRECTORY "${CMAKE_SOURCE_DIR}/../../tests/riscv-tests")
      set(RISCV_TESTS_DIR "${CMAKE_SOURCE_DIR}/../../tests/riscv-tests")
    else()
      set(RISCV_TESTS_DIR "${CMAKE_SOURCE_DIR}/tests/riscv-tests")
    endif()
  endif()
  set(RISCV_TESTS_DIR "${RISCV_TESTS_DIR}" CACHE PATH "Path to riscv-tests directory")

  cmake_path(ABSOLUTE_PATH RISCV_TESTS_DIR BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)

  if(EXISTS "${RISCV_TESTS_DIR}/share/riscv-tests")
    set(RISCV_TESTS_DIR "${RISCV_TESTS_DIR}/share/riscv-tests")
  endif()

  message(STATUS "RISC-V tests and benchmarks: ${RISCV_TESTS_DIR}")

  set(ISA_DIR "${RISCV_TESTS_DIR}/isa")

  set(ISA_SMOKE_RV32GC
      "rv32ui-p-add"
      "rv32ui-p-addi"
      "rv32ui-p-and"
      "rv32ui-p-beq"
      "rv32ui-p-bne"
      "rv32ui-p-bltu"
      "rv32ui-p-bgeu"
      "rv32ui-p-lb"
      "rv32ui-p-lbu"
      "rv32ui-p-lh"
      "rv32ui-p-lhu"
      "rv32ui-p-lw"
      "rv32ui-p-sw"
      "rv32ui-p-sll"
      "rv32ui-p-slt"
      "rv32ui-p-sltu"
      "rv32ui-p-sra"
      "rv32ui-p-srl"
      "rv32ui-p-sub"
      "rv32ui-p-xor"
      "rv32ui-p-or"
      "rv32um-p-mul"
      "rv32um-p-mulh"
      "rv32um-p-mulhu"
      "rv32um-p-div"
      "rv32um-p-divu"
      "rv32um-p-rem"
      "rv32um-p-remu"
      "rv32ua-p-amoadd_w"
      "rv32ua-p-amomax_w"
      "rv32ua-p-amomaxu_w"
      "rv32ua-p-amomin_w"
      "rv32ua-p-amominu_w"
      "rv32ua-p-amoor_w"
      "rv32ua-p-amoswap_w"
      "rv32ua-p-amoxor_w"
      "rv32ua-p-lrsc")

  set(ISA_SMOKE_RV64GC
      "rv64ui-p-add"
      "rv64ui-p-addi"
      "rv64ui-p-and"
      "rv64ui-p-auipc"
      "rv64ui-p-beq"
      "rv64ui-p-bge"
      "rv64ui-p-bgeu"
      "rv64ui-p-blt"
      "rv64ui-p-bltu"
      "rv64ui-p-bne"
      "rv64ui-p-jal"
      "rv64ui-p-jalr"
      "rv64ui-p-lb"
      "rv64ui-p-lbu"
      "rv64ui-p-lh"
      "rv64ui-p-lhu"
      "rv64ui-p-lw"
      "rv64ui-p-lwu"
      "rv64ui-p-or"
      "rv64ui-p-sb"
      "rv64ui-p-sd"
      "rv64ui-p-sh"
      "rv64ui-p-sw"
      "rv64ui-p-sll"
      "rv64ui-p-slli"
      "rv64ui-p-slt"
      "rv64ui-p-sltu"
      "rv64ui-p-sra"
      "rv64ui-p-srai"
      "rv64ui-p-srl"
      "rv64ui-p-srli"
      "rv64ui-p-sub"
      "rv64ui-p-xor"
      "rv64um-p-mul"
      "rv64um-p-mulh"
      "rv64um-p-mulhsu"
      "rv64um-p-mulhu"
      "rv64um-p-div"
      "rv64um-p-divu"
      "rv64um-p-rem"
      "rv64um-p-remu"
      "rv64ua-p-amoadd_w"
      "rv64ua-p-amomax_w"
      "rv64ua-p-amomaxu_w"
      "rv64ua-p-amomin_w"
      "rv64ua-p-amominu_w"
      "rv64ua-p-amoor_w"
      "rv64ua-p-amoswap_w"
      "rv64ua-p-amoxor_w"
      "rv64ua-p-lrsc")

  set(ALL_ISA_TESTS)
  if(EXISTS "${ISA_DIR}" AND IS_DIRECTORY "${ISA_DIR}")
    file(GLOB ALL_FILES CONFIGURE_DEPENDS "${ISA_DIR}/rv${SIMRV_XLEN}*")
    foreach(f IN LISTS ALL_FILES)
      if(IS_DIRECTORY "${f}")
        continue()
      endif()
      get_filename_component(ext "${f}" LAST_EXT)
      get_filename_component(name "${f}" NAME)
      if(ext STREQUAL ""
         AND name MATCHES
             "^rv${SIMRV_XLEN}(ui|uc|um|ua|uf|ud|uv|uzba|uzbb|uzbc|uzbs)-[pv]-")
        list(APPEND ALL_ISA_TESTS "${name}")
      endif()
    endforeach()
  endif()

  function(register_isa_test test_name is_smoke)
    set(labels "external;isa")
    if(is_smoke)
      list(APPEND labels "smoke")
    endif()
    if(test_name MATCHES "^rv(32|64)")
      list(APPEND labels "rv${CMAKE_MATCH_1}gc")
      if(SIMRV_XLEN STREQUAL "64" AND CMAKE_MATCH_1 STREQUAL "32")
        list(APPEND labels "hosted-rv32")
      endif()
    endif()

    add_test(
      NAME "isa-${test_name}"
      COMMAND
        ${CMAKE_COMMAND} -D SIMRV_BIN=$<TARGET_FILE:SimRV> -D
        ELF_PATH=${ISA_DIR}/${test_name} -D OBJCOPY_BIN=${RISCV_OBJCOPY} -D
        NM_BIN=${RISCV_NM} -D WORK_DIR=${ISA_WORK_DIR} -D TIMEOUT_SECS=20 -D
        END_INSNS=2000000 -D DEFAULT_TOHOST_ADDR=0x80001000 -P
        ${CMAKE_SOURCE_DIR}/cmake/RunIsaTest.cmake)
    set_tests_properties(
      "isa-${test_name}" PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                                    LABELS "${labels}" SKIP_RETURN_CODE 2)
  endfunction()

  function(register_lockstep_isa_test test_name)
    add_test(
      NAME "isa-lockstep-${test_name}"
      COMMAND
        ${CMAKE_COMMAND} -D SIMRV_BIN=$<TARGET_FILE:SimRV> -D
        ELF_PATH=${ISA_DIR}/${test_name} -D OBJCOPY_BIN=${RISCV_OBJCOPY} -D
        NM_BIN=${RISCV_NM} -D WORK_DIR=${ISA_WORK_DIR} -D TIMEOUT_SECS=20 -D
        END_INSNS=2000000 -D DEFAULT_TOHOST_ADDR=0x80001000 -D LOCKSTEP=TRUE -D
        SPIKE_BIN=${SPIKE_BIN} -P ${CMAKE_SOURCE_DIR}/cmake/RunIsaTest.cmake)
    set_tests_properties(
      "isa-lockstep-${test_name}"
      PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS
                 "external;lockstep;rv${SIMRV_XLEN}gc" SKIP_RETURN_CODE 2)
  endfunction()

  if(SIMRV_XLEN STREQUAL "64")
    set(SMOKE_LIST ${ISA_SMOKE_RV64GC})
  else()
    set(SMOKE_LIST ${ISA_SMOKE_RV32GC})
  endif()

  foreach(test_name IN LISTS SMOKE_LIST)
    register_isa_test("${test_name}" TRUE)
  endforeach()

  # The distributed RV64-capable binary must retain its RV32 personality. Run
  # the RV32 smoke corpus through it in addition to validating the native RV32
  # build; the latter remains a strict compile-time-width oracle in CI.
  if(SIMRV_XLEN STREQUAL "64")
    foreach(test_name IN LISTS ISA_SMOKE_RV32GC)
      if(EXISTS "${ISA_DIR}/${test_name}")
        register_isa_test("${test_name}" TRUE)
      endif()
    endforeach()
  endif()

  function(register_ca_isa_test test_name pipeline_type)
    if(NOT EXISTS "${ISA_DIR}/${test_name}")
      return()
    endif()
    add_test(
      NAME "ca-${pipeline_type}-${test_name}"
      COMMAND
        ${CMAKE_COMMAND} -D SIMRV_BIN=$<TARGET_FILE:SimRV> -D
        ELF_PATH=${ISA_DIR}/${test_name} -D OBJCOPY_BIN=${RISCV_OBJCOPY} -D
        NM_BIN=${RISCV_NM} -D WORK_DIR=${ISA_WORK_DIR}/${pipeline_type} -D
        TIMEOUT_SECS=20 -D END_INSNS=2000000 -D DEFAULT_TOHOST_ADDR=0x80001000
        -D CYCLE_MODE=TRUE -D PIPELINE_TYPE=${pipeline_type} -P
        ${CMAKE_SOURCE_DIR}/cmake/RunIsaTest.cmake)
    set_tests_properties(
      "ca-${pipeline_type}-${test_name}"
      PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS
                 "external;isa;ca;smoke;pipeline;rv${SIMRV_XLEN}gc"
                 SKIP_RETURN_CODE 2)
  endfunction()

  set(CA_SMOKE_TESTS
      "rv${SIMRV_XLEN}ui-p-add"
      "rv${SIMRV_XLEN}ui-p-jalr"
      "rv${SIMRV_XLEN}ui-p-lw"
      "rv${SIMRV_XLEN}ui-p-sw"
      "rv${SIMRV_XLEN}ui-p-ma_data"
      "rv${SIMRV_XLEN}ui-p-fence_i"
      "rv${SIMRV_XLEN}um-p-mul"
      "rv${SIMRV_XLEN}um-p-div"
      "rv${SIMRV_XLEN}ua-p-lrsc"
      "rv${SIMRV_XLEN}uc-p-rvc"
      "rv${SIMRV_XLEN}uf-p-fadd")
  foreach(ca_pipeline IN ITEMS 5stage 3stage)
    foreach(ca_test IN LISTS CA_SMOKE_TESTS)
      register_ca_isa_test("${ca_test}" "${ca_pipeline}")
    endforeach()
  endforeach()

  if(SPIKE_BIN)
    foreach(test_name IN LISTS SMOKE_LIST)
      register_lockstep_isa_test("${test_name}")
    endforeach()
  endif()

  foreach(test_name IN LISTS ALL_ISA_TESTS)
    if(NOT test_name IN_LIST SMOKE_LIST)
      register_isa_test("${test_name}" FALSE)
    endif()
  endforeach()

  # Register vector tests wrapper if an explicit vector test package is
  # available.
  if(SIMRV_VECTOR_TESTS_DIR AND EXISTS "${SIMRV_VECTOR_TESTS_DIR}")
    set(RISCV_GCC_NAMES)
    if(NOT "${RISCV_PREFIX}" STREQUAL "")
      list(APPEND RISCV_GCC_NAMES "${RISCV_PREFIX}gcc")
    endif()
    list(
      APPEND
      RISCV_GCC_NAMES
      "riscv64-unknown-elf-gcc"
      "riscv32-unknown-elf-gcc"
      "riscv64-linux-gnu-gcc"
      "riscv32-linux-gnu-gcc"
      "riscv-none-elf-gcc")
    find_program(
      RISCV_GCC
      NAMES ${RISCV_GCC_NAMES}
      DOC "Path to RISC-V GCC cross-compiler for compiling vector tests")

    if(RISCV_GCC)
      add_test(
        NAME vector-suite
        COMMAND
          ${PYTHON_BIN} ${CMAKE_SOURCE_DIR}/scripts/run_vector_tests.py --simrv
          $<TARGET_FILE:SimRV> --xlen ${SIMRV_XLEN} --gcc ${RISCV_GCC}
          --objcopy ${RISCV_OBJCOPY} --nm ${RISCV_NM} --work-dir
          ${CMAKE_CURRENT_BINARY_DIR}/vector-tests-gen --vector-tests-dir
          ${SIMRV_VECTOR_TESTS_DIR})
      set_tests_properties(
        vector-suite PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} LABELS
                                "external;vector;required" TIMEOUT 600)
    endif()
  endif()

  add_executable(simrv-microbenchmarks tests/Microbenchmarks.cpp)
  set_property(GLOBAL APPEND PROPERTY SIMRV_NATIVE_TEST_TARGETS simrv-microbenchmarks)
  target_link_libraries(simrv-microbenchmarks PRIVATE simrv-runtime)
  simrv_configure_native_test(simrv-microbenchmarks)
  target_include_directories(simrv-microbenchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME microbenchmarks COMMAND simrv-microbenchmarks)
  set_tests_properties(microbenchmarks PROPERTIES LABELS "benchmark;perf" TIMEOUT 120)

  set(_consumer_cxx_flags "${CMAKE_CXX_FLAGS}")
  set(_consumer_exe_linker_flags "${CMAKE_EXE_LINKER_FLAGS}")
  if(_simrv_sanitizer_flags)
    string(APPEND _consumer_cxx_flags " -fsanitize=${_simrv_sanitizer_flags}")
    string(APPEND _consumer_exe_linker_flags " -fsanitize=${_simrv_sanitizer_flags}")
  endif()
  add_test(
    NAME package-install-consumer
    COMMAND
      ${CMAKE_COMMAND} -D SIMRV_SOURCE_DIR=${CMAKE_SOURCE_DIR} -D
      SIMRV_BINARY_DIR=${CMAKE_BINARY_DIR}
      -D "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      -D "CMAKE_CXX_FLAGS=${_consumer_cxx_flags}"
      -D "CMAKE_EXE_LINKER_FLAGS=${_consumer_exe_linker_flags}" -P
      ${CMAKE_SOURCE_DIR}/cmake/TestPackageConsumer.cmake)
  set_tests_properties(package-install-consumer
                       PROPERTIES LABELS "gate;regress;package;sdk")
endif()

if(BUILD_TESTING)
  add_test(NAME mit-license-audit
           COMMAND ${CMAKE_COMMAND} -D SIMRV_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P
                   ${CMAKE_SOURCE_DIR}/cmake/CheckMitLicense.cmake)
  set_tests_properties(mit-license-audit PROPERTIES LABELS "gate;license")
  add_test(NAME architecture-boundaries
           COMMAND ${CMAKE_COMMAND} -D SIMRV_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P
                   ${CMAKE_SOURCE_DIR}/cmake/CheckArchitecture.cmake)
  set_tests_properties(architecture-boundaries
                       PROPERTIES LABELS "gate;regress;architecture")
endif()
