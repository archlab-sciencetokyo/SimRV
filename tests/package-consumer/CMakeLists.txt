cmake_minimum_required(VERSION 3.31)
project(SimRVPackageConsumer LANGUAGES C CXX)

find_package(SimRV 3.0 CONFIG REQUIRED COMPONENTS Development)

add_executable(simrv-package-consumer main.cpp)
target_link_libraries(simrv-package-consumer PRIVATE SimRV::runtime)

add_library(simrv-consumer-plugin SHARED plugin_dummy.c)
target_link_libraries(simrv-consumer-plugin PRIVATE SimRV::simrv-devel)
