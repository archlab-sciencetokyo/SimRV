simrv_add_component(simrv-foundation
  Logger.cpp
  ../util/FdtGenerator.cpp
)
add_library(simrv::foundation ALIAS simrv-foundation)

simrv_add_component(simrv-core
  Machine.cpp
  MachineInit.cpp
  PlatformBuilder.cpp
  StateControl.cpp
  CsrFile.cpp
  Tracer.cpp
  Sbi.cpp
  Tlb.cpp
  Cpu.cpp
  ../util/CliParser.cpp
  ../util/InstructionExplainer.cpp
)
add_library(simrv::core ALIAS simrv-core)

add_library(simrv-runtime STATIC
  $<TARGET_OBJECTS:simrv-foundation>
  $<TARGET_OBJECTS:simrv-isa>
  $<TARGET_OBJECTS:simrv-execution>
  $<TARGET_OBJECTS:simrv-pipeline>
  $<TARGET_OBJECTS:simrv-memory>
  $<TARGET_OBJECTS:simrv-platform>
  $<TARGET_OBJECTS:simrv-debug>
  $<TARGET_OBJECTS:simrv-tui-framework>
  $<TARGET_OBJECTS:simrv-tui>
  $<TARGET_OBJECTS:simrv-core>
)
add_library(simrv::runtime ALIAS simrv-runtime)
add_library(SimRV::runtime ALIAS simrv-runtime)
target_include_directories(simrv-runtime PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:include>
)
set_target_properties(simrv-runtime PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME runtime
)
simrv_apply_options(simrv-runtime)
set_property(TARGET simrv-runtime PROPERTY INTERFACE_LINK_LIBRARIES "")
