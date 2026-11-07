add_library(simrv-rvcomp-uart SHARED UartPlugin.cpp)
target_include_directories(simrv-rvcomp-uart PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(simrv-rvcomp-uart PROPERTIES
  OUTPUT_NAME "rvcomp-uart"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
