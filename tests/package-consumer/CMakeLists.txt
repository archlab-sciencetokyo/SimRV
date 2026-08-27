cmake_minimum_required(VERSION 3.31)
project(SimRVPackageConsumer LANGUAGES CXX)

find_package(SimRV 2.1 CONFIG REQUIRED)

add_executable(simrv-package-consumer main.cpp)
target_compile_features(simrv-package-consumer PRIVATE cxx_std_23)
target_link_libraries(simrv-package-consumer PRIVATE SimRV::runtime)
