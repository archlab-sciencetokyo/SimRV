add_library(simrv-mmc SHARED MmcPlugin.cpp)
target_include_directories(simrv-mmc PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(simrv-mmc PROPERTIES
  OUTPUT_NAME "simrv-mmc"
  PREFIX "lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# Also create aliases librvcomp-mmc.so and libmmc.so in plugins dir for convenient naming
add_custom_command(TARGET simrv-mmc POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "$<TARGET_FILE_NAME:simrv-mmc>"
    "${CMAKE_BINARY_DIR}/plugins/librvcomp-mmc.so"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "$<TARGET_FILE_NAME:simrv-mmc>"
    "${CMAKE_BINARY_DIR}/plugins/libmmc.so"
)

