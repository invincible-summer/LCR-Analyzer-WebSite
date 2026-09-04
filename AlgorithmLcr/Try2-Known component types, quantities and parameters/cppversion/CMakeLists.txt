cmake_minimum_required(VERSION 3.16)
project(netgraph_id_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra)

add_library(ng_id STATIC
    src/adjacency.cpp
    src/components.cpp
    src/enumerate.cpp
    src/filters.cpp
    src/graph.cpp
    src/identify.cpp
    src/iofmt.cpp
    src/metric.cpp
    src/nodal.cpp
    src/report.cpp
    src/selector.cpp
    src/synthetic.cpp
)
target_include_directories(ng_id PUBLIC src)

add_executable(demo apps/demo.cpp)
target_link_libraries(demo PRIVATE ng_id)

add_executable(case_run apps/case_run.cpp)
target_link_libraries(case_run PRIVATE ng_id)

add_executable(ng_tests
    tests/test_main.cpp
    tests/suites_core.cpp
    tests/suites_io.cpp
    tests/suites_e2e.cpp
)
target_link_libraries(ng_tests PRIVATE ng_id)
