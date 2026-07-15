#pragma once

#include <string>
#include <vector>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <stddef.h>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

#if !defined(__gl_h_) && !defined(__GL_H__)
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLdouble = double;
using GLboolean = unsigned char;
using GLvoid = void;
#endif

using GLchar = char;
using GLsizeiptr = ptrdiff_t;

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL 0x8037
#endif
#ifndef GL_STENCIL_TEST
#define GL_STENCIL_TEST 0x0B90
#endif
#ifndef GL_FRONT
#define GL_FRONT 0x0404
#endif
#ifndef GL_BACK
#define GL_BACK 0x0405
#endif
#ifndef GL_INCR_WRAP
#define GL_INCR_WRAP 0x8507
#endif
#ifndef GL_DECR_WRAP
#define GL_DECR_WRAP 0x8508
#endif

typedef GLuint(APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void(APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader,
                                              GLsizei count,
                                              const GLchar *const *string,
                                              const GLint *length);
typedef void(APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void(APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void(APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader,
                                                  GLsizei bufSize,
                                                  GLsizei *length,
                                                  GLchar *infoLog);
typedef void(APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef GLuint(APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void(APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void(APIENTRY *PFNGLBINDATTRIBLOCATIONPROC)(GLuint program,
                                                    GLuint index,
                                                    const GLchar *name);
typedef void(APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void(APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void(APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint(APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void(APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void(APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint location,
                                                  GLsizei count,
                                                  GLboolean transpose,
                                                  const GLfloat *value);
typedef void(APIENTRY *PFNGLUNIFORM4FVPROC)(GLint location, GLsizei count, const GLfloat *value);
typedef void(APIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void(APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void(APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef void(APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void(APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target,
                                            GLsizeiptr size,
                                            const void *data,
                                            GLenum usage);
typedef void(APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void(APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index,
                                                     GLint size,
                                                     GLenum type,
                                                     GLboolean normalized,
                                                     GLsizei stride,
                                                     const void *pointer);
typedef void(APIENTRY *PFNGLSTENCILOPSEPARATEPROC)(GLenum face,
                                                   GLenum sfail,
                                                   GLenum dpfail,
                                                   GLenum dppass);

extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
extern PFNGLUNIFORM4FVPROC glUniform4fv;
extern PFNGLUNIFORM1IPROC glUniform1i;
extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLSTENCILOPSEPARATEPROC glStencilOpSeparate;

bool InitializeOpenGlFunctions();
const std::vector<std::string> &MissingOpenGlFunctions();

#else

#include <epoxy/gl.h>

inline bool InitializeOpenGlFunctions() {
    return true;
}

inline const std::vector<std::string> &MissingOpenGlFunctions() {
    static const std::vector<std::string> missing;
    return missing;
}

#endif
