add_library(simrv-cfu-provingground SHARED CfuPlugin.cpp)
target_include_directories(simrv-cfu-provingground PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(simrv-cfu-provingground PROPERTIES
  OUTPUT_NAME "simrv-cfu-provingground"
  PREFIX "lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

add_custom_command(TARGET simrv-cfu-provingground POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "$<TARGET_FILE_NAME:simrv-cfu-provingground>"
    "${CMAKE_BINARY_DIR}/plugins/libcfu-provingground.so"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "$<TARGET_FILE_NAME:simrv-cfu-provingground>"
    "${CMAKE_BINARY_DIR}/plugins/libcfu-pg.so"
)

