cmake_minimum_required(VERSION 3.16)
project(rlc_id_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra)

add_library(rlc_id STATIC
    src/adjacency.cpp
    src/circuits.cpp
    src/iofmt.cpp
    src/linalg.cpp
    src/library.cpp
    src/fit_engine_a.cpp
    src/fit_engine_b.cpp
    src/pruning.cpp
    src/selector.cpp
    src/synthetic.cpp
    src/report.cpp
    src/identify.cpp
)
target_include_directories(rlc_id PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(rlc_id PUBLIC Threads::Threads)

add_executable(rlc_tests
    tests/test_main.cpp
    tests/suites_core.cpp
    tests/suites_io.cpp
    tests/suites_sweep.cpp
)
target_link_libraries(rlc_tests PRIVATE rlc_id)

add_executable(demo apps/demo.cpp)
target_link_libraries(demo PRIVATE rlc_id)

add_executable(benchmark apps/benchmark.cpp)
target_link_libraries(benchmark PRIVATE rlc_id)

add_executable(dump apps/dump.cpp)
target_link_libraries(dump PRIVATE rlc_id)

add_executable(bench_dump apps/bench_dump.cpp)
target_link_libraries(bench_dump PRIVATE rlc_id)
