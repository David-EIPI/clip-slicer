// SPDX-FileCopyrightText: 2026 David Shirvanyants
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "gl_api.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLUNIFORM4FVPROC glUniform4fv = nullptr;
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLSTENCILOPSEPARATEPROC glStencilOpSeparate = nullptr;

namespace {

bool loadAttempted = false;
bool loadSucceeded = false;
std::vector<std::string> missingFunctions;

template <typename Proc> bool loadProc(Proc &proc, HMODULE module, const char *name) {
    PROC address = wglGetProcAddress(name);
    if (address && address != reinterpret_cast<PROC>(1) && address != reinterpret_cast<PROC>(2) &&
        address != reinterpret_cast<PROC>(3) && address != reinterpret_cast<PROC>(-1)) {
        proc = reinterpret_cast<Proc>(address);
        return true;
    }

    proc = reinterpret_cast<Proc>(GetProcAddress(module, name));
    return proc != nullptr;
}

template <typename Proc> void loadRequired(Proc &proc, HMODULE module, const char *name) {
    if (!loadProc(proc, module, name))
        missingFunctions.emplace_back(name);
}

} // namespace

bool InitializeOpenGlFunctions() {
    if (loadAttempted)
        return loadSucceeded;
    loadAttempted = true;

    const HMODULE module = LoadLibraryA("opengl32.dll");
    if (!module) {
        missingFunctions.emplace_back("opengl32.dll (library could not be loaded)");
        return false;
    }

    loadRequired(glCreateShader, module, "glCreateShader");
    loadRequired(glShaderSource, module, "glShaderSource");
    loadRequired(glCompileShader, module, "glCompileShader");
    loadRequired(glGetShaderiv, module, "glGetShaderiv");
    loadRequired(glGetShaderInfoLog, module, "glGetShaderInfoLog");
    loadRequired(glDeleteShader, module, "glDeleteShader");
    loadRequired(glCreateProgram, module, "glCreateProgram");
    loadRequired(glAttachShader, module, "glAttachShader");
    loadRequired(glBindAttribLocation, module, "glBindAttribLocation");
    loadRequired(glLinkProgram, module, "glLinkProgram");
    loadRequired(glGetProgramiv, module, "glGetProgramiv");
    loadRequired(glDeleteProgram, module, "glDeleteProgram");
    loadRequired(glGetUniformLocation, module, "glGetUniformLocation");
    loadRequired(glUseProgram, module, "glUseProgram");
    loadRequired(glUniformMatrix4fv, module, "glUniformMatrix4fv");
    loadRequired(glUniform4fv, module, "glUniform4fv");
    loadRequired(glUniform1i, module, "glUniform1i");
    loadRequired(glGenBuffers, module, "glGenBuffers");
    loadRequired(glDeleteBuffers, module, "glDeleteBuffers");
    loadRequired(glBindBuffer, module, "glBindBuffer");
    loadRequired(glBufferData, module, "glBufferData");
    loadRequired(glEnableVertexAttribArray, module, "glEnableVertexAttribArray");
    loadRequired(glVertexAttribPointer, module, "glVertexAttribPointer");
    loadRequired(glStencilOpSeparate, module, "glStencilOpSeparate");

    loadSucceeded = missingFunctions.empty();
    return loadSucceeded;
}

const std::vector<std::string> &MissingOpenGlFunctions() {
    return missingFunctions;
}

#endif
