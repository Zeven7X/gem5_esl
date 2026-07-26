cmake_minimum_required(VERSION 3.22)

project(gem5_clion_workspace LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GEM5_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(GEM5_IDE_STUB_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/gem5_ide_stubs")
set(GEM5_IDE_ALIAS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/gem5_include_alias")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateGem5IdeStubs.cmake")
gem5_generate_ide_stubs(
    ROOT_DIR "${GEM5_PROJECT_ROOT}"
    OUTPUT_DIR "${GEM5_IDE_STUB_DIR}"
)

file(MAKE_DIRECTORY "${GEM5_IDE_ALIAS_DIR}")
if(NOT EXISTS "${GEM5_IDE_ALIAS_DIR}/gem5")
    file(CREATE_LINK
        "${GEM5_PROJECT_ROOT}/src"
        "${GEM5_IDE_ALIAS_DIR}/gem5"
        SYMBOLIC
    )
endif()

add_library(gem5_headers INTERFACE)

target_include_directories(gem5_headers
    INTERFACE
        "${GEM5_PROJECT_ROOT}/my_module/ide_stubs"
        "${GEM5_IDE_ALIAS_DIR}"
        "${GEM5_PROJECT_ROOT}/src"
        "${GEM5_PROJECT_ROOT}/include"
        "${GEM5_PROJECT_ROOT}/ext"
        "${GEM5_PROJECT_ROOT}/ext/dramsim2"
        "${GEM5_PROJECT_ROOT}/ext/dramsim3"
        "${GEM5_PROJECT_ROOT}/ext/dramsys"
        "${GEM5_PROJECT_ROOT}/ext/googletest/googlemock/include"
        "${GEM5_PROJECT_ROOT}/ext/googletest/googletest/include"
        "${GEM5_PROJECT_ROOT}/ext/pybind11/include"
        "${GEM5_PROJECT_ROOT}/ext/systemc/src"
        "${GEM5_PROJECT_ROOT}/src/systemc/ext"
        "${GEM5_IDE_STUB_DIR}"
)

target_compile_features(gem5_headers INTERFACE cxx_std_17)

target_compile_definitions(gem5_headers
    INTERFACE
        TRACING_ON=1
)

file(GLOB_RECURSE GEM5_IDE_TREE_FILES CONFIGURE_DEPENDS
    "${GEM5_PROJECT_ROOT}/src/*.hh"
    "${GEM5_PROJECT_ROOT}/src/*.h"
    "${GEM5_PROJECT_ROOT}/src/*.cc"
    "${GEM5_PROJECT_ROOT}/src/*.cpp"
    "${GEM5_PROJECT_ROOT}/include/*.hh"
    "${GEM5_PROJECT_ROOT}/include/*.h"
    "${GEM5_PROJECT_ROOT}/my_module/include/*.hh"
    "${GEM5_PROJECT_ROOT}/my_module/src/*.cc"
)

add_custom_target(gem5_source_tree SOURCES ${GEM5_IDE_TREE_FILES})

set(GEM5_IDE_PROBE_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/gem5_ide_probe.cc")
file(WRITE "${GEM5_IDE_PROBE_SOURCE}"
"#include <gem5/m5ops.h>\n"
"#include \"cpu/base.hh\"\n"
"int gem5_ide_probe() { return 0; }\n"
)

add_library(gem5_ide_probe OBJECT EXCLUDE_FROM_ALL "${GEM5_IDE_PROBE_SOURCE}")
target_link_libraries(gem5_ide_probe PRIVATE gem5_headers)

set(GEM5_USER_INCLUDE_DIRS "")
if(EXISTS "${GEM5_PROJECT_ROOT}/my_module/include")
    list(APPEND GEM5_USER_INCLUDE_DIRS "${GEM5_PROJECT_ROOT}/my_module/include")
endif()

set(GEM5_USER_SOURCES "")
if(EXISTS "${GEM5_PROJECT_ROOT}/my_module/src")
    file(GLOB_RECURSE GEM5_MY_MODULE_SOURCES CONFIGURE_DEPENDS
        "${GEM5_PROJECT_ROOT}/my_module/src/*.cpp"
        "${GEM5_PROJECT_ROOT}/my_module/src/*.cxx"
    )
    list(APPEND GEM5_USER_SOURCES ${GEM5_MY_MODULE_SOURCES})
endif()

if(EXISTS "${GEM5_PROJECT_ROOT}/sim/main.cpp")
    add_executable(sim_main
        "${GEM5_PROJECT_ROOT}/sim/main.cpp"
        ${GEM5_USER_SOURCES}
    )

    target_link_libraries(sim_main PRIVATE gem5_headers)
    if(GEM5_USER_INCLUDE_DIRS)
        target_include_directories(sim_main PRIVATE ${GEM5_USER_INCLUDE_DIRS})
    endif()
endif()
