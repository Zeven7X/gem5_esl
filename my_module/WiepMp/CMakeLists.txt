cmake_minimum_required(VERSION 3.10)

project(WiepMp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(WiepMp
    WiepProcessManager.cc
)

target_include_directories(WiepMp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(WiepMp PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(WiepMp PUBLIC pthread rt)

add_executable(wiep_mp_example examples/WiepMpExample.cc)
target_link_libraries(wiep_mp_example PRIVATE WiepMp)
