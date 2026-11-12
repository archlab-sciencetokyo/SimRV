add_library(simrv-rv98-keyboard SHARED KeyboardPlugin.cpp)
target_include_directories(simrv-rv98-keyboard PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(simrv-rv98-keyboard PROPERTIES
  OUTPUT_NAME "rv98-keyboard"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
