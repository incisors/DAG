cmake_minimum_required(VERSION 3.14)
project(dag LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only engine.
add_library(dag INTERFACE)
target_include_directories(dag INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(dag INTERFACE Threads::Threads)

# ---- Tests --------------------------------------------------------------
enable_testing()
add_executable(test_executor tests/test_executor.cpp)
target_link_libraries(test_executor PRIVATE dag)
add_test(NAME test_executor COMMAND test_executor)

# Optional UB-detection build. ASan/TSan have no runtime on mingw-w64, but
# clang's UBSan *trap* mode needs none -- UB aborts at the faulting instruction.
option(DAG_UBSAN_TRAP "Build test_executor with clang UBSan trap mode" OFF)
if(DAG_UBSAN_TRAP)
    target_compile_options(test_executor PRIVATE
        -fsanitize=undefined,bounds,integer -fsanitize-trap=all -g -O1)
endif()

# ---- Examples -----------------------------------------------------------
add_executable(example_basic examples/basic.cpp)
target_link_libraries(example_basic PRIVATE dag)

add_executable(example_pipeline examples/pipeline.cpp)
target_link_libraries(example_pipeline PRIVATE dag)
