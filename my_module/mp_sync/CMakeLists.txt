cmake_minimum_required(VERSION 3.16)

project(wiep_mp_sync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(wiep_mp_sync
    src/posix_ipc.cc
    src/sync_manager.cc
)

target_include_directories(wiep_mp_sync PUBLIC include)
target_compile_options(wiep_mp_sync PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(wiep_mp_sync PUBLIC pthread rt)

add_executable(worker_sim examples/worker_sim.cc)
target_include_directories(worker_sim PRIVATE examples)
target_link_libraries(worker_sim PRIVATE wiep_mp_sync)

add_executable(main_sim examples/main_sim.cc)
target_include_directories(main_sim PRIVATE examples)
target_link_libraries(main_sim PRIVATE wiep_mp_sync)

enable_testing()
add_test(NAME mp_sync_round_trip
         COMMAND main_sim $<TARGET_FILE:worker_sim>)
