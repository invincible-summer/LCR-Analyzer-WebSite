cmake_minimum_required(VERSION 3.16)
project(topofit_id_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra)

add_library(tf_id STATIC
    src/adjacency.cpp
    src/fit.cpp
    src/graph.cpp
    src/iofmt.cpp
    src/linalg.cpp
    src/metric.cpp
    src/nodal.cpp
    src/synthetic.cpp
)
target_include_directories(tf_id PUBLIC src)

add_executable(demo apps/demo.cpp)
target_link_libraries(demo PRIVATE tf_id)

add_executable(case_run apps/case_run.cpp)
target_link_libraries(case_run PRIVATE tf_id)

add_executable(tf_tests
    tests/test_main.cpp
    tests/suites_core.cpp
    tests/suites_io.cpp
    tests/suites_e2e.cpp
)
target_link_libraries(tf_tests PRIVATE tf_id)
