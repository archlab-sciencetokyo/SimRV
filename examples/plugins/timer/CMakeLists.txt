cmake_minimum_required(VERSION 3.20)
project(simrv-demo-timer LANGUAGES C)

find_package(SimRV 3.0 CONFIG QUIET COMPONENTS Development)

add_library(simrv-demo-timer SHARED timer_plugin.c)

if(TARGET SimRV::simrv-devel)
    target_link_libraries(simrv-demo-timer PRIVATE SimRV::simrv-devel)
else()
    target_include_directories(simrv-demo-timer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
    )
endif()

set_target_properties(simrv-demo-timer PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "simrv-demo-timer"
)
