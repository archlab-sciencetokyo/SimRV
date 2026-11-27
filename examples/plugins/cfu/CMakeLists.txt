cmake_minimum_required(VERSION 3.20)
project(simrv-demo-cfu LANGUAGES C)

find_package(SimRV 3.0 CONFIG QUIET COMPONENTS Development)

add_library(simrv-demo-cfu SHARED cfu_plugin.c)

if(TARGET SimRV::simrv-devel)
    target_link_libraries(simrv-demo-cfu PRIVATE SimRV::simrv-devel)
else()
    target_include_directories(simrv-demo-cfu PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
    )
endif()

set_target_properties(simrv-demo-cfu PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "simrv-demo-cfu"
)
