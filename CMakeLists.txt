cmake_minimum_required(VERSION 3.12)
project(stl_slicer VERSION 0.1.0 LANGUAGES CXX)

option(STL_SLICER_BUILD_TESTS "Build slicer tests" ON)
option(STL_SLICER_BUILD_GUI "Build the CLIP Slicer wxWidgets application" ON)
set(STL_SLICER_ARCH "" CACHE STRING
    "Compiler -march target (for example native, core-avx-i, or skylake); empty keeps a portable target")

add_library(stl_slicer_lib
    src/stl_reader.cpp
    src/cli_reader.cpp
    src/slicer.cpp
    src/cli_writer.cpp
    src/scene_model.cpp)
target_include_directories(stl_slicer_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(stl_slicer_lib PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(stl_slicer_lib PRIVATE /W4)
else()
    target_compile_options(stl_slicer_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(STL_SLICER_ARCH)
    include(CheckCXXCompilerFlag)
    set(STL_SLICER_ARCH_FLAG "-march=${STL_SLICER_ARCH}")
    check_cxx_compiler_flag("${STL_SLICER_ARCH_FLAG}" STL_SLICER_ARCH_SUPPORTED)
    if(NOT STL_SLICER_ARCH_SUPPORTED)
        message(FATAL_ERROR "The compiler does not support ${STL_SLICER_ARCH_FLAG}")
    endif()
    target_compile_options(stl_slicer_lib PRIVATE "${STL_SLICER_ARCH_FLAG}")
endif()

add_executable(stl-slicer src/main.cpp)
target_link_libraries(stl-slicer PRIVATE stl_slicer_lib)

if(STL_SLICER_BUILD_GUI)
    find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base gl)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)
    include(${wxWidgets_USE_FILE})
    add_executable(clip-slicer
        app/main.cpp
        app/main_frame.cpp
        app/document_frame.cpp
        app/gl_canvas.cpp)
    target_include_directories(clip-slicer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(clip-slicer PRIVATE stl_slicer_lib ${wxWidgets_LIBRARIES} PkgConfig::EPOXY)
    target_compile_features(clip-slicer PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(clip-slicer PRIVATE /W4)
    else()
        target_compile_options(clip-slicer PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(STL_SLICER_BUILD_TESTS)
    enable_testing()
    add_executable(stl_slicer_tests tests/tests.cpp)
    target_link_libraries(stl_slicer_tests PRIVATE stl_slicer_lib)
    add_test(NAME stl_slicer_tests COMMAND stl_slicer_tests)
endif()
