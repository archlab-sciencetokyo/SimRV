cmake_minimum_required(VERSION 3.20)
project(simrv-demo-dma-accel LANGUAGES C)

find_package(SimRV 3.0 CONFIG QUIET COMPONENTS Development)

add_library(simrv-demo-dma-accel SHARED dma_accel_plugin.c)

if(TARGET SimRV::simrv-devel)
    target_link_libraries(simrv-demo-dma-accel PRIVATE SimRV::simrv-devel)
else()
    target_include_directories(simrv-demo-dma-accel PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
    )
endif()

set_target_properties(simrv-demo-dma-accel PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "simrv-demo-dma-accel"
)
