add_library(simrv-reset SHARED ResetPlugin.cpp)
target_include_directories(simrv-reset PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(simrv-reset PROPERTIES
  OUTPUT_NAME "simrv-reset"
  PREFIX "lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

add_custom_command(TARGET simrv-reset POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "$<TARGET_FILE_NAME:simrv-reset>"
    "${CMAKE_BINARY_DIR}/plugins/librvcomp-reset.so"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "$<TARGET_FILE_NAME:simrv-reset>"
    "${CMAKE_BINARY_DIR}/plugins/libreset.so"
)

