set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_RC_COMPILER llvm-rc)

set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

foreach(mode DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_EXE_LINKER_FLAGS_${mode}
        "${CMAKE_EXE_LINKER_FLAGS_${mode}} /machine:x64")
    set(CMAKE_SHARED_LINKER_FLAGS_${mode}
        "${CMAKE_SHARED_LINKER_FLAGS_${mode}} /machine:x64")
endforeach()
