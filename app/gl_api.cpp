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

template <typename Proc>
bool loadProc(Proc &proc, HMODULE module, const char *name) {
    proc = reinterpret_cast<Proc>(wglGetProcAddress(name));
    if (proc)
        return true;

    proc = reinterpret_cast<Proc>(GetProcAddress(module, name));
    return proc != nullptr;
}

} // namespace

bool InitializeOpenGlFunctions() {
    static bool initialized = false;
    if (initialized)
        return true;

    const HMODULE module = LoadLibraryA("opengl32.dll");
    if (!module)
        return false;

    const bool ok =
        loadProc(glCreateShader, module, "glCreateShader") &&
        loadProc(glShaderSource, module, "glShaderSource") &&
        loadProc(glCompileShader, module, "glCompileShader") &&
        loadProc(glGetShaderiv, module, "glGetShaderiv") &&
        loadProc(glGetShaderInfoLog, module, "glGetShaderInfoLog") &&
        loadProc(glDeleteShader, module, "glDeleteShader") &&
        loadProc(glCreateProgram, module, "glCreateProgram") &&
        loadProc(glAttachShader, module, "glAttachShader") &&
        loadProc(glBindAttribLocation, module, "glBindAttribLocation") &&
        loadProc(glLinkProgram, module, "glLinkProgram") &&
        loadProc(glGetProgramiv, module, "glGetProgramiv") &&
        loadProc(glDeleteProgram, module, "glDeleteProgram") &&
        loadProc(glGetUniformLocation, module, "glGetUniformLocation") &&
        loadProc(glUseProgram, module, "glUseProgram") &&
        loadProc(glUniformMatrix4fv, module, "glUniformMatrix4fv") &&
        loadProc(glUniform4fv, module, "glUniform4fv") &&
        loadProc(glUniform1i, module, "glUniform1i") &&
        loadProc(glGenBuffers, module, "glGenBuffers") &&
        loadProc(glDeleteBuffers, module, "glDeleteBuffers") &&
        loadProc(glBindBuffer, module, "glBindBuffer") &&
        loadProc(glBufferData, module, "glBufferData") &&
        loadProc(glEnableVertexAttribArray, module, "glEnableVertexAttribArray") &&
        loadProc(glVertexAttribPointer, module, "glVertexAttribPointer") &&
        loadProc(glStencilOpSeparate, module, "glStencilOpSeparate");

    initialized = ok;
    return ok;
}

#endif
