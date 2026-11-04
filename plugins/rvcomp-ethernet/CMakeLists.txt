add_library(simrv-ethernet SHARED EthernetPlugin.cpp)
target_include_directories(simrv-ethernet PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(simrv-ethernet PROPERTIES
  OUTPUT_NAME "simrv-ethernet"
  PREFIX "lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

add_custom_command(TARGET simrv-ethernet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "$<TARGET_FILE_NAME:simrv-ethernet>"
    "${CMAKE_BINARY_DIR}/plugins/librvcomp-ethernet.so"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "$<TARGET_FILE_NAME:simrv-ethernet>"
    "${CMAKE_BINARY_DIR}/plugins/libethernet.so"
)

