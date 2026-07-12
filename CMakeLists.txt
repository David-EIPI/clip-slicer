cmake_minimum_required(VERSION 3.12)
project(stl_slicer VERSION 0.1.0 LANGUAGES CXX)

option(STL_SLICER_BUILD_TESTS "Build slicer tests" ON)

add_library(stl_slicer_lib
    src/stl_reader.cpp
    src/slicer.cpp
    src/cli_writer.cpp)
target_include_directories(stl_slicer_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(stl_slicer_lib PUBLIC cxx_std_17)
target_compile_options(stl_slicer_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(stl-slicer src/main.cpp)
target_link_libraries(stl-slicer PRIVATE stl_slicer_lib)

if(STL_SLICER_BUILD_TESTS)
    enable_testing()
    add_executable(stl_slicer_tests tests/tests.cpp)
    target_link_libraries(stl_slicer_tests PRIVATE stl_slicer_lib)
    add_test(NAME stl_slicer_tests COMMAND stl_slicer_tests)
endif()
