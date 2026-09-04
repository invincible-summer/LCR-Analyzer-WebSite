# lcr_wasm — bridge the three AlgorithmLcr cppversion engines into one
# WebAssembly module (and, with a host compiler, a native sanity-test binary).
#
#   emscripten build:  ./build.sh            -> frontend/src/wasm/lcr_wasm.{js,wasm}
#   native tests:      cmake -S . -B build-native && cmake --build build-native --target glue_test && ./build-native/glue_test
#
# The three engine headers share basenames (adjacency.hpp, graph.hpp, ...), so
# each engine is wrapped by its OWN translation unit that sees only that
# engine's include dir; the object files are linked into one module.

cmake_minimum_required(VERSION 3.16)
project(lcr_wasm CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(ALGO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../AlgorithmLcr" CACHE PATH
    "AlgorithmLcr source root (the authoritative C++ engines)")
set(RLC_DIR "${ALGO_ROOT}/Try1-Completely unknown single port fitting/cppversion")
set(NG_DIR "${ALGO_ROOT}/Try2-Known component types, quantities and parameters/cppversion")
set(TF_DIR "${ALGO_ROOT}/Try3-Component types with known topology and corresponding positions/cppversion")

foreach(_dir RLC_DIR NG_DIR TF_DIR)
    if(NOT EXISTS "${${_dir}}/CMakeLists.txt")
        message(FATAL_ERROR "engine sources not found under ${ALGO_ROOT} (${_dir}=${${_dir}}); pass -DALGO_ROOT=...")
    endif()
endforeach()

# Exceptions must stay enabled in ALL engine code (the glue catches
# std::invalid_argument / tf::PortOpenError).  Directory property, inherited
# by every target defined below.
add_compile_options(-fexceptions)

# The three sub-projects each define same-named utility targets (demo,
# case_run, ...) so add_subdirectory() is not usable.  Their src/ directories
# contain exactly the library sources, so compile them into our own static
# libraries directly.  (Re-run cmake — build.sh always does — after engine
# files are added/removed.)
file(GLOB RLC_SOURCES "${RLC_DIR}/src/*.cpp")
file(GLOB NG_SOURCES "${NG_DIR}/src/*.cpp")
file(GLOB TF_SOURCES "${TF_DIR}/src/*.cpp")

add_library(rlc_id STATIC ${RLC_SOURCES})
target_include_directories(rlc_id PUBLIC "${RLC_DIR}/src")

add_library(ng_id STATIC ${NG_SOURCES})
target_include_directories(ng_id PUBLIC "${NG_DIR}/src")

add_library(tf_id STATIC ${TF_SOURCES})
target_include_directories(tf_id PUBLIC "${TF_DIR}/src")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    find_package(Threads REQUIRED)
    target_link_libraries(rlc_id PUBLIC Threads::Threads)
endif()

add_library(glue_version OBJECT src/version_glue.cpp)
target_include_directories(glue_version PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(glue1 OBJECT src/try1_glue.cpp)
target_include_directories(glue1 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${RLC_DIR}/src")

add_library(glue2 OBJECT src/try2_glue.cpp)
target_include_directories(glue2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${NG_DIR}/src")

add_library(glue3 OBJECT src/try3_glue.cpp)
target_include_directories(glue3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${TF_DIR}/src")

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(lcr_wasm
        $<TARGET_OBJECTS:glue_version>
        $<TARGET_OBJECTS:glue1>
        $<TARGET_OBJECTS:glue2>
        $<TARGET_OBJECTS:glue3>)
    target_link_libraries(lcr_wasm PRIVATE rlc_id ng_id tf_id)
    target_link_options(lcr_wasm PRIVATE
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createLcrModule"
        "-sEXPORT_ES6=1"
        "-sENVIRONMENT=web,worker"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sFILESYSTEM=0"
        "-sINVOKE_RUN=0"
        "-sEXPORTED_FUNCTIONS=_lcr_version,_lcr_try1,_lcr_try2,_lcr_try3,_lcr_free,_malloc,_free"
        "-sEXPORTED_RUNTIME_METHODS=UTF8ToString,getValue,setValue,HEAPF64,HEAP32,HEAPU8"
        "-fexceptions"
        "-O3")
else()
    # Host build: sanity tests calling the same C ABI natively.
    add_executable(glue_test
        tests/glue_test.cpp
        $<TARGET_OBJECTS:glue_version>
        $<TARGET_OBJECTS:glue1>
        $<TARGET_OBJECTS:glue2>
        $<TARGET_OBJECTS:glue3>)
    target_include_directories(glue_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(glue_test PRIVATE rlc_id ng_id tf_id)
    find_package(Threads REQUIRED)
    target_link_libraries(glue_test PRIVATE Threads::Threads)
endif()
