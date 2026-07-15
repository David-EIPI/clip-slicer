cmake_minimum_required(VERSION 3.12)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(stl_slicer VERSION 0.1.0 LANGUAGES CXX)

set(STL_SLICER_CLIPPER2_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/Clipper2-Clipper2_2.0.1_DLLs/CPP"
    CACHE PATH "Local Clipper2 C++ source tree")

find_package(Clipper2 CONFIG QUIET)
if(NOT TARGET Clipper2::Clipper2)
    if(EXISTS "${STL_SLICER_CLIPPER2_SOURCE_DIR}/CMakeLists.txt")
        set(CLIPPER2_UTILS OFF CACHE BOOL "" FORCE)
        set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(CLIPPER2_TESTS OFF CACHE BOOL "" FORCE)
        set(CLIPPER2_USINGZ OFF CACHE STRING "" FORCE)
        add_subdirectory("${STL_SLICER_CLIPPER2_SOURCE_DIR}" clipper2 EXCLUDE_FROM_ALL)
        if(TARGET Clipper2)
            add_library(Clipper2::Clipper2 ALIAS Clipper2)
        endif()
    else()
        find_file(CLIPPER2_HEADER clipper2/clipper.h)
        if(NOT CLIPPER2_HEADER)
            message(FATAL_ERROR
                "Clipper2 was not found. Install the package config or provide either "
                "a local source tree or clipper2/clipper.h in the compiler include path.")
        endif()

        get_filename_component(CLIPPER2_HEADER_DIR "${CLIPPER2_HEADER}" DIRECTORY)
        file(GLOB CLIPPER2_HEADERS "${CLIPPER2_HEADER_DIR}/*.h")

        set(CLIPPER2_WRAPPER_DIR "${CMAKE_CURRENT_BINARY_DIR}/clipper2_wrapper")
        file(MAKE_DIRECTORY "${CLIPPER2_WRAPPER_DIR}/clipper2")

        foreach(clipper_header IN LISTS CLIPPER2_HEADERS)
            get_filename_component(clipper_name "${clipper_header}" NAME)
            file(WRITE "${CLIPPER2_WRAPPER_DIR}/clipper2/${clipper_name}"
                "#pragma once\n#include \"${clipper_header}\"\n")
        endforeach()

        add_library(Clipper2::Clipper2 INTERFACE IMPORTED)
        set_target_properties(Clipper2::Clipper2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${CLIPPER2_WRAPPER_DIR}")
    endif()
endif()

option(STL_SLICER_BUILD_TESTS "Build slicer tests" ON)
option(STL_SLICER_BUILD_GUI "Build the CLIP Slicer wxWidgets application" ON)
set(STL_SLICER_ARCH "" CACHE STRING
    "Compiler -march target (for example native, core-avx-i, or skylake); empty keeps a portable target")

add_library(stl_slicer_lib
    src/stl_reader.cpp
    src/stl_writer.cpp
    src/cli_reader.cpp
    src/slice.cpp
    src/slicer.cpp
    src/unsupported_area.cpp
    src/orientation_optimizer.cpp
    src/cli_writer.cpp
    src/scene_model.cpp)
target_include_directories(stl_slicer_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(stl_slicer_lib PUBLIC Clipper2::Clipper2)
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
    set(STL_SLICER_WINDOWS_WX_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/wxWidgets-3.2.11"
        CACHE PATH "Local wxWidgets source tree for Windows builds")
    set(STL_SLICER_WINDOWS_WX_ROOT
        "/home/ucodex/my_msvc/wxWidgets"
        CACHE PATH "Windows wxWidgets root directory")
    set(STL_SLICER_WINDOWS_WX_LIB_DIR
        "/home/ucodex/my_msvc/wxWidgets/lib/vc14x_x64_dll"
        CACHE PATH "Windows wxWidgets library directory")

    if(WIN32)
        if(EXISTS "${STL_SLICER_WINDOWS_WX_SOURCE_DIR}/CMakeLists.txt")
            set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
            set(wxBUILD_MONOLITHIC OFF CACHE BOOL "" FORCE)
            set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
            set(wxBUILD_PRECOMP OFF CACHE BOOL "" FORCE)
            set(wxBUILD_SAMPLES OFF CACHE STRING "" FORCE)
            set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
            set(wxBUILD_TESTS OFF CACHE STRING "" FORCE)
            set(HAVE_GETTIMEOFDAY OFF CACHE INTERNAL "" FORCE)
            set(wxUSE_OPENGL ON CACHE BOOL "" FORCE)
            set(wxUSE_HELP OFF CACHE BOOL "" FORCE)
            set(wxUSE_MS_HTML_HELP OFF CACHE BOOL "" FORCE)
            set(wxUSE_WXHTML_HELP OFF CACHE BOOL "" FORCE)
            set(wxUSE_WEBVIEW OFF CACHE BOOL "" FORCE)
            set(wxUSE_MEDIACTRL OFF CACHE BOOL "" FORCE)
            set(wxUSE_AUI OFF CACHE BOOL "" FORCE)
            set(wxUSE_RIBBON OFF CACHE BOOL "" FORCE)
            set(wxUSE_PROPGRID OFF CACHE BOOL "" FORCE)
            set(wxUSE_RICHTEXT OFF CACHE BOOL "" FORCE)
            set(wxUSE_STC OFF CACHE BOOL "" FORCE)
            set(wxUSE_XRC OFF CACHE BOOL "" FORCE)
            set(wxUSE_XML OFF CACHE BOOL "" FORCE)
            set(wxUSE_HTML OFF CACHE BOOL "" FORCE)
            add_subdirectory("${STL_SLICER_WINDOWS_WX_SOURCE_DIR}" wxwidgets EXCLUDE_FROM_ALL)
            set(STL_SLICER_USE_LOCAL_WX_SOURCE ON)
        else()
            if(NOT EXISTS "${STL_SLICER_WINDOWS_WX_ROOT}/include/wx/wx.h")
                message(FATAL_ERROR
                    "Windows wxWidgets headers were not found under "
                    "${STL_SLICER_WINDOWS_WX_ROOT}")
            endif()
            if(NOT EXISTS "${STL_SLICER_WINDOWS_WX_LIB_DIR}")
                message(FATAL_ERROR
                    "Windows wxWidgets libraries were not found under "
                    "${STL_SLICER_WINDOWS_WX_LIB_DIR}")
            endif()

            set(STL_SLICER_WINDOWS_WX_SUFFIX "u")
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(STL_SLICER_WINDOWS_WX_SUFFIX "ud")
            endif()

            add_library(wxWidgets::wxWidgets INTERFACE IMPORTED)
            set_target_properties(wxWidgets::wxWidgets PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES
                    "${STL_SLICER_WINDOWS_WX_ROOT}/include;${STL_SLICER_WINDOWS_WX_ROOT}/include/msvc"
                INTERFACE_COMPILE_DEFINITIONS
                    "WXUSINGDLL;__WXMSW__;UNICODE;_UNICODE;wxMSVC_VERSION_ABI_COMPAT"
                INTERFACE_LINK_LIBRARIES
                    "${STL_SLICER_WINDOWS_WX_LIB_DIR}/wxmsw32${STL_SLICER_WINDOWS_WX_SUFFIX}_gl.lib;${STL_SLICER_WINDOWS_WX_LIB_DIR}/wxmsw32${STL_SLICER_WINDOWS_WX_SUFFIX}_core.lib;${STL_SLICER_WINDOWS_WX_LIB_DIR}/wxbase32${STL_SLICER_WINDOWS_WX_SUFFIX}.lib")
        endif()
    else()
        find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base gl)
        include(${wxWidgets_USE_FILE})
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)
        pkg_check_modules(GLU REQUIRED IMPORTED_TARGET glu)
    endif()

    add_executable(clip-slicer WIN32
        app/main.cpp
        app/main_frame.cpp
        app/document_frame.cpp
        app/app_settings.cpp
        app/settings_dialog.cpp
        app/model_transform_dialog.cpp
        app/embedded_assets.cpp
        app/gl_api.cpp
        app/gl_canvas.cpp
        app/slice_visualization.cpp)
    target_include_directories(clip-slicer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)

    if(WIN32)
        if(STL_SLICER_USE_LOCAL_WX_SOURCE)
            target_link_libraries(clip-slicer PRIVATE
                stl_slicer_lib
                wx::gl
                wx::core
                wx::base)
        else()
            target_link_directories(clip-slicer PRIVATE "${STL_SLICER_WINDOWS_WX_LIB_DIR}")
            target_link_libraries(clip-slicer PRIVATE
                stl_slicer_lib
                wxWidgets::wxWidgets
                opengl32
                glu32)
        endif()
    else()
        target_link_libraries(clip-slicer PRIVATE
            stl_slicer_lib
            ${wxWidgets_LIBRARIES}
            PkgConfig::EPOXY
            PkgConfig::GLU)
    endif()

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
    if(STL_SLICER_BUILD_GUI)
        add_executable(slice_visualization_tests
            tests/slice_visualization_tests.cpp
            app/slice_visualization.cpp)
        target_include_directories(slice_visualization_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
        if(WIN32)
            target_link_libraries(slice_visualization_tests PRIVATE
                stl_slicer_lib
                glu32)
        else()
            target_link_libraries(slice_visualization_tests PRIVATE
                stl_slicer_lib
                PkgConfig::GLU)
        endif()
        target_compile_features(slice_visualization_tests PRIVATE cxx_std_17)
        add_test(NAME slice_visualization_tests COMMAND slice_visualization_tests)
    endif()
endif()
