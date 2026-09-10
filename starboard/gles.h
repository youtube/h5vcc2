// Copyright 2019 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Module Overview: Starboard GLES API
//
// The GLES API provides an interface along with type declarations and
// definitions. Together, these provide a single, consistent method for GLES
// usage across platforms.
//
// This API is designed to abstract differences between GLES implementations and
// versions across various systems. It also removes the need for other code to
// directly import and use system libraries.
//
// # GLES Version
//
// This API requires support for OpenGLES 3.2.

#ifndef STARBOARD_GLES_H_
#define STARBOARD_GLES_H_

#include <stddef.h>
#include <stdint.h>

#include "starboard/configuration.h"
#include "starboard/export.h"
#include "starboard/log.h"

#ifdef __cplusplus
extern "C" {
#endif

// The following type definitions were adapted from the types declared in
// [gl2.h](https://www.khronos.org/registry/OpenGL/api/GLES2/gl2.h).
typedef uint8_t SbGlBoolean;
typedef uint32_t SbGlBitfield;
typedef char SbGlChar;
typedef float SbGlClampf;
typedef uint32_t SbGlEnum;
typedef int32_t SbGlFixed;
typedef float SbGlFloat;
typedef int8_t SbGlInt8;
typedef int16_t SbGlInt16;
typedef int32_t SbGlInt32;
typedef int64_t SbGlInt64;
typedef int32_t SbGlSizei;
typedef struct __GLsync* SbGlSync;
typedef uint8_t SbGlUInt8;
typedef uint16_t SbGlUInt16;
typedef uint32_t SbGlUInt32;
typedef uint64_t SbGlUInt64;
typedef void SbGlVoid;

// Some compilers implicitly convert `intptr_t` to `int`. However, this is not
// equivalent to a `long int` or `long long int` from the compiler's
// perspective. Starboard checks the platform configuration and sets the types
// to match those used by OpenGL ES 2.0 (see
// [gl2ext.h](https://www.khronos.org/registry/OpenGL/api/GLES2/gl2ext.h)).
#if (SB_SIZE_OF(POINTER) == 8) && (SB_SIZE_OF(LONG) == 4)
typedef long long int SbGlIntPtr;    // NOLINT
typedef long long int SbGlSizeiPtr;  // NOLINT
#else   // (SB_SIZE_OF(POINTER) != 8) || (SB_SIZE_OF(LONG) != 4)
typedef long int SbGlIntPtr;    // NOLINT
typedef long int SbGlSizeiPtr;  // NOLINT
#endif  // (SB_SIZE_OF(POINTER) == 8) && (SB_SIZE_OF(LONG) == 4)

typedef struct SbGlesInterface {
  // The following prototypes were adapted from the prototypes declared in
  // https://www.khronos.org/registry/OpenGL/api/GLES2/gl2.h.
  void (*glActiveTexture)(SbGlEnum texture);
  void (*glAttachShader)(SbGlUInt32 program, SbGlUInt32 shader);
  void (*glBindAttribLocation)(SbGlUInt32 program,
                               SbGlUInt32 index,
                               const SbGlChar* name);
  void (*glBindBuffer)(SbGlEnum target, SbGlUInt32 buffer);
  void (*glBindFramebuffer)(SbGlEnum target, SbGlUInt32 framebuffer);
  void (*glBindRenderbuffer)(SbGlEnum target, SbGlUInt32 renderbuffer);
  void (*glBindTexture)(SbGlEnum target, SbGlUInt32 texture);
  void (*glBlendColor)(SbGlFloat red,
                       SbGlFloat green,
                       SbGlFloat blue,
                       SbGlFloat alpha);
  void (*glBlendEquation)(SbGlEnum mode);
  void (*glBlendEquationSeparate)(SbGlEnum modeRGB, SbGlEnum modeAlpha);
  void (*glBlendFunc)(SbGlEnum sfactor, SbGlEnum dfactor);
  void (*glBlendFuncSeparate)(SbGlEnum sfactorRGB,
                              SbGlEnum dfactorRGB,
                              SbGlEnum sfactorAlpha,
                              SbGlEnum dfactorAlpha);
  void (*glBufferData)(SbGlEnum target,
                       SbGlSizeiPtr size,
                       const void* data,
                       SbGlEnum usage);
  void (*glBufferSubData)(SbGlEnum target,
                          SbGlIntPtr offset,
                          SbGlSizeiPtr size,
                          const void* data);
  SbGlEnum (*glCheckFramebufferStatus)(SbGlEnum target);
  void (*glClear)(SbGlBitfield mask);
  void (*glClearColor)(SbGlFloat red,
                       SbGlFloat green,
                       SbGlFloat blue,
                       SbGlFloat alpha);
  void (*glClearDepthf)(SbGlFloat d);
  void (*glClearStencil)(SbGlInt32 s);
  void (*glColorMask)(SbGlBoolean red,
                      SbGlBoolean green,
                      SbGlBoolean blue,
                      SbGlBoolean alpha);
  void (*glCompileShader)(SbGlUInt32 shader);
  void (*glCompressedTexImage2D)(SbGlEnum target,
                                 SbGlInt32 level,
                                 SbGlEnum internalformat,
                                 SbGlSizei width,
                                 SbGlSizei height,
                                 SbGlInt32 border,
                                 SbGlSizei imageSize,
                                 const void* data);
  void (*glCompressedTexSubImage2D)(SbGlEnum target,
                                    SbGlInt32 level,
                                    SbGlInt32 xoffset,
                                    SbGlInt32 yoffset,
                                    SbGlSizei width,
                                    SbGlSizei height,
                                    SbGlEnum format,
                                    SbGlSizei imageSize,
                                    const void* data);
  void (*glCopyTexImage2D)(SbGlEnum target,
                           SbGlInt32 level,
                           SbGlEnum internalformat,
                           SbGlInt32 x,
                           SbGlInt32 y,
                           SbGlSizei width,
                           SbGlSizei height,
                           SbGlInt32 border);
  void (*glCopyTexSubImage2D)(SbGlEnum target,
                              SbGlInt32 level,
                              SbGlInt32 xoffset,
                              SbGlInt32 yoffset,
                              SbGlInt32 x,
                              SbGlInt32 y,
                              SbGlSizei width,
                              SbGlSizei height);
  SbGlUInt32 (*glCreateProgram)(void);
  SbGlUInt32 (*glCreateShader)(SbGlEnum type);
  void (*glCullFace)(SbGlEnum mode);
  void (*glDeleteBuffers)(SbGlSizei n, const SbGlUInt32* buffers);
  void (*glDeleteFramebuffers)(SbGlSizei n, const SbGlUInt32* framebuffers);
  void (*glDeleteProgram)(SbGlUInt32 program);
  void (*glDeleteRenderbuffers)(SbGlSizei n, const SbGlUInt32* renderbuffers);
  void (*glDeleteShader)(SbGlUInt32 shader);
  void (*glDeleteTextures)(SbGlSizei n, const SbGlUInt32* textures);
  void (*glDepthFunc)(SbGlEnum func);
  void (*glDepthMask)(SbGlBoolean flag);
  void (*glDepthRangef)(SbGlFloat n, SbGlFloat f);
  void (*glDetachShader)(SbGlUInt32 program, SbGlUInt32 shader);
  void (*glDisable)(SbGlEnum cap);
  void (*glDisableVertexAttribArray)(SbGlUInt32 index);
  void (*glDrawArrays)(SbGlEnum mode, SbGlInt32 first, SbGlSizei count);
  void (*glDrawElements)(SbGlEnum mode,
                         SbGlSizei count,
                         SbGlEnum type,
                         const void* indices);
  void (*glEnable)(SbGlEnum cap);
  void (*glEnableVertexAttribArray)(SbGlUInt32 index);
  void (*glFinish)(void);
  void (*glFlush)(void);
  void (*glFramebufferRenderbuffer)(SbGlEnum target,
                                    SbGlEnum attachment,
                                    SbGlEnum renderbuffertarget,
                                    SbGlUInt32 renderbuffer);
  void (*glFramebufferTexture2D)(SbGlEnum target,
                                 SbGlEnum attachment,
                                 SbGlEnum textarget,
                                 SbGlUInt32 texture,
                                 SbGlInt32 level);
  void (*glFrontFace)(SbGlEnum mode);
  void (*glGenBuffers)(SbGlSizei n, SbGlUInt32* buffers);
  void (*glGenerateMipmap)(SbGlEnum target);
  void (*glGenFramebuffers)(SbGlSizei n, SbGlUInt32* framebuffers);
  void (*glGenRenderbuffers)(SbGlSizei n, SbGlUInt32* renderbuffers);
  void (*glGenTextures)(SbGlSizei n, SbGlUInt32* textures);
  void (*glGetActiveAttrib)(SbGlUInt32 program,
                            SbGlUInt32 index,
                            SbGlSizei bufSize,
                            SbGlSizei* length,
                            SbGlInt32* size,
                            SbGlEnum* type,
                            SbGlChar* name);
  void (*glGetActiveUniform)(SbGlUInt32 program,
                             SbGlUInt32 index,
                             SbGlSizei bufSize,
                             SbGlSizei* length,
                             SbGlInt32* size,
                             SbGlEnum* type,
                             SbGlChar* name);
  void (*glGetAttachedShaders)(SbGlUInt32 program,
                               SbGlSizei maxCount,
                               SbGlSizei* count,
                               SbGlUInt32* shaders);
  SbGlInt32 (*glGetAttribLocation)(SbGlUInt32 program, const SbGlChar* name);
  void (*glGetBooleanv)(SbGlEnum pname, SbGlBoolean* data);
  void (*glGetBufferParameteriv)(SbGlEnum target,
                                 SbGlEnum pname,
                                 SbGlInt32* params);
  SbGlEnum (*glGetError)(void);
  void (*glGetFloatv)(SbGlEnum pname, SbGlFloat* data);
  void (*glGetFramebufferAttachmentParameteriv)(SbGlEnum target,
                                                SbGlEnum attachment,
                                                SbGlEnum pname,
                                                SbGlInt32* params);
  void (*glGetIntegerv)(SbGlEnum pname, SbGlInt32* data);
  void (*glGetProgramiv)(SbGlUInt32 program, SbGlEnum pname, SbGlInt32* params);
  void (*glGetProgramInfoLog)(SbGlUInt32 program,
                              SbGlSizei bufSize,
                              SbGlSizei* length,
                              SbGlChar* infoLog);
  void (*glGetRenderbufferParameteriv)(SbGlEnum target,
                                       SbGlEnum pname,
                                       SbGlInt32* params);
  void (*glGetShaderiv)(SbGlUInt32 shader, SbGlEnum pname, SbGlInt32* params);
  void (*glGetShaderInfoLog)(SbGlUInt32 shader,
                             SbGlSizei bufSize,
                             SbGlSizei* length,
                             SbGlChar* infoLog);
  void (*glGetShaderPrecisionFormat)(SbGlEnum shadertype,
                                     SbGlEnum precisiontype,
                                     SbGlInt32* range,
                                     SbGlInt32* precision);
  void (*glGetShaderSource)(SbGlUInt32 shader,
                            SbGlSizei bufSize,
                            SbGlSizei* length,
                            SbGlChar* source);
  const SbGlUInt8* (*glGetString)(SbGlEnum name);
  void (*glGetTexParameterfv)(SbGlEnum target,
                              SbGlEnum pname,
                              SbGlFloat* params);
  void (*glGetTexParameteriv)(SbGlEnum target,
                              SbGlEnum pname,
                              SbGlInt32* params);
  void (*glGetUniformfv)(SbGlUInt32 program,
                         SbGlInt32 location,
                         SbGlFloat* params);
  void (*glGetUniformiv)(SbGlUInt32 program,
                         SbGlInt32 location,
                         SbGlInt32* params);
  SbGlInt32 (*glGetUniformLocation)(SbGlUInt32 program, const SbGlChar* name);
  void (*glGetVertexAttribfv)(SbGlUInt32 index,
                              SbGlEnum pname,
                              SbGlFloat* params);
  void (*glGetVertexAttribiv)(SbGlUInt32 index,
                              SbGlEnum pname,
                              SbGlInt32* params);
  void (*glGetVertexAttribPointerv)(SbGlUInt32 index,
                                    SbGlEnum pname,
                                    void** pointer);
  void (*glHint)(SbGlEnum target, SbGlEnum mode);
  SbGlBoolean (*glIsBuffer)(SbGlUInt32 buffer);
  SbGlBoolean (*glIsEnabled)(SbGlEnum cap);
  SbGlBoolean (*glIsFramebuffer)(SbGlUInt32 framebuffer);
  SbGlBoolean (*glIsProgram)(SbGlUInt32 program);
  SbGlBoolean (*glIsRenderbuffer)(SbGlUInt32 renderbuffer);
  SbGlBoolean (*glIsShader)(SbGlUInt32 shader);
  SbGlBoolean (*glIsTexture)(SbGlUInt32 texture);
  void (*glLineWidth)(SbGlFloat width);
  void (*glLinkProgram)(SbGlUInt32 program);
  void (*glPixelStorei)(SbGlEnum pname, SbGlInt32 param);
  void (*glPolygonOffset)(SbGlFloat factor, SbGlFloat units);
  void (*glReadPixels)(SbGlInt32 x,
                       SbGlInt32 y,
                       SbGlSizei width,
                       SbGlSizei height,
                       SbGlEnum format,
                       SbGlEnum type,
                       void* pixels);
  void (*glReleaseShaderCompiler)(void);
  void (*glRenderbufferStorage)(SbGlEnum target,
                                SbGlEnum internalformat,
                                SbGlSizei width,
                                SbGlSizei height);
  void (*glSampleCoverage)(SbGlFloat value, SbGlBoolean invert);
  void (*glScissor)(SbGlInt32 x,
                    SbGlInt32 y,
                    SbGlSizei width,
                    SbGlSizei height);
  void (*glShaderBinary)(SbGlSizei count,
                         const SbGlUInt32* shaders,
                         SbGlEnum binaryformat,
                         const void* binary,
                         SbGlSizei length);
  void (*glShaderSource)(SbGlUInt32 shader,
                         SbGlSizei count,
                         const SbGlChar* const* string,  // NOLINT
                         const SbGlInt32* length);
  void (*glStencilFunc)(SbGlEnum func, SbGlInt32 ref, SbGlUInt32 mask);
  void (*glStencilFuncSeparate)(SbGlEnum face,
                                SbGlEnum func,
                                SbGlInt32 ref,
                                SbGlUInt32 mask);
  void (*glStencilMask)(SbGlUInt32 mask);
  void (*glStencilMaskSeparate)(SbGlEnum face, SbGlUInt32 mask);
  void (*glStencilOp)(SbGlEnum fail, SbGlEnum zfail, SbGlEnum zpass);
  void (*glStencilOpSeparate)(SbGlEnum face,
                              SbGlEnum sfail,
                              SbGlEnum dpfail,
                              SbGlEnum dppass);
  void (*glTexImage2D)(SbGlEnum target,
                       SbGlInt32 level,
                       SbGlInt32 internalformat,
                       SbGlSizei width,
                       SbGlSizei height,
                       SbGlInt32 border,
                       SbGlEnum format,
                       SbGlEnum type,
                       const void* pixels);
  void (*glTexParameterf)(SbGlEnum target, SbGlEnum pname, SbGlFloat param);
  void (*glTexParameterfv)(SbGlEnum target,
                           SbGlEnum pname,
                           const SbGlFloat* params);
  void (*glTexParameteri)(SbGlEnum target, SbGlEnum pname, SbGlInt32 param);
  void (*glTexParameteriv)(SbGlEnum target,
                           SbGlEnum pname,
                           const SbGlInt32* params);
  void (*glTexSubImage2D)(SbGlEnum target,
                          SbGlInt32 level,
                          SbGlInt32 xoffset,
                          SbGlInt32 yoffset,
                          SbGlSizei width,
                          SbGlSizei height,
                          SbGlEnum format,
                          SbGlEnum type,
                          const void* pixels);
  void (*glUniform1f)(SbGlInt32 location, SbGlFloat v0);
  void (*glUniform1fv)(SbGlInt32 location,
                       SbGlSizei count,
                       const SbGlFloat* value);
  void (*glUniform1i)(SbGlInt32 location, SbGlInt32 v0);
  void (*glUniform1iv)(SbGlInt32 location,
                       SbGlSizei count,
                       const SbGlInt32* value);
  void (*glUniform2f)(SbGlInt32 location, SbGlFloat v0, SbGlFloat v1);
  void (*glUniform2fv)(SbGlInt32 location,
                       SbGlSizei count,
                       const SbGlFloat* value);
  void (*glUniform2i)(SbGlInt32 location, SbGlInt32 v0, SbGlInt32 v1);
  void (*glUniform2iv)(SbGlInt32 location,
                       SbGlSizei count,
                       const SbGlInt32* value);
  void (*glUniform3f)(SbGlInt32 location,
                      SbGlFloat v0,
                      SbGlFloat v1,
                      SbGlFloat v2);
  void (*glUniform3fv)(SbGlInt32 location,
                       SbGlSizei count,
                       const SbGlFloat* value);
  void (*glUniform3i)(SbGlInt32 location,
                      SbGlInt32 v0,
                      SbGlInt32 v1,
                      SbGlInt32 v2);
  void (*glUniform3iv)(SbGlInt32 location,
                       SbGlSizei count,
                       const SbGlInt32* value);
  void (*glUniform4f)(SbGlInt32 location,
                      SbGlFloat v0,
                      SbGlFloat v1,
                      SbGlFloat v2,
                      SbGlFloat v3);
  void (*glUniform4fv)(SbGlInt32 location,
                       SbGlSizei count,
                       const SbGlFloat* value);
  void (*glUniform4i)(SbGlInt32 location,
                      SbGlInt32 v0,
                      SbGlInt32 v1,
                      SbGlInt32 v2,
                      SbGlInt32 v3);
  void (*glUniform4iv)(SbGlInt32 location,
                       SbGlSizei count,
                       const SbGlInt32* value);
  void (*glUniformMatrix2fv)(SbGlInt32 location,
                             SbGlSizei count,
                             SbGlBoolean transpose,
                             const SbGlFloat* value);
  void (*glUniformMatrix3fv)(SbGlInt32 location,
                             SbGlSizei count,
                             SbGlBoolean transpose,
                             const SbGlFloat* value);
  void (*glUniformMatrix4fv)(SbGlInt32 location,
                             SbGlSizei count,
                             SbGlBoolean transpose,
                             const SbGlFloat* value);
  void (*glUseProgram)(SbGlUInt32 program);
  void (*glValidateProgram)(SbGlUInt32 program);
  void (*glVertexAttrib1f)(SbGlUInt32 index, SbGlFloat x);
  void (*glVertexAttrib1fv)(SbGlUInt32 index, const SbGlFloat* v);
  void (*glVertexAttrib2f)(SbGlUInt32 index, SbGlFloat x, SbGlFloat y);
  void (*glVertexAttrib2fv)(SbGlUInt32 index, const SbGlFloat* v);
  void (*glVertexAttrib3f)(SbGlUInt32 index,
                           SbGlFloat x,
                           SbGlFloat y,
                           SbGlFloat z);
  void (*glVertexAttrib3fv)(SbGlUInt32 index, const SbGlFloat* v);
  void (*glVertexAttrib4f)(SbGlUInt32 index,
                           SbGlFloat x,
                           SbGlFloat y,
                           SbGlFloat z,
                           SbGlFloat w);
  void (*glVertexAttrib4fv)(SbGlUInt32 index, const SbGlFloat* v);
  void (*glVertexAttribPointer)(SbGlUInt32 index,
                                SbGlInt32 size,
                                SbGlEnum type,
                                SbGlBoolean normalized,
                                SbGlSizei stride,
                                const void* pointer);
  void (*glViewport)(SbGlInt32 x,
                     SbGlInt32 y,
                     SbGlSizei width,
                     SbGlSizei height);

  // The following prototypes were adapted from the prototypes declared in
  // https://www.khronos.org/registry/OpenGL/api/GLES3/gl3.h.
  void (*glReadBuffer)(SbGlEnum src);
  void (*glDrawRangeElements)(SbGlEnum mode,
                              SbGlUInt32 start,
                              SbGlUInt32 end,
                              SbGlSizei count,
                              SbGlEnum type,
                              const void* indices);
  void (*glTexImage3D)(SbGlEnum target,
                       SbGlInt32 level,
                       SbGlInt32 internalformat,
                       SbGlSizei width,
                       SbGlSizei height,
                       SbGlSizei depth,
                       SbGlInt32 border,
                       SbGlEnum format,
                       SbGlEnum type,
                       const void* pixels);
  void (*glTexSubImage3D)(SbGlEnum target,
                          SbGlInt32 level,
                          SbGlInt32 xoffset,
                          SbGlInt32 yoffset,
                          SbGlInt32 zoffset,
                          SbGlSizei width,
                          SbGlSizei height,
                          SbGlSizei depth,
                          SbGlEnum format,
                          SbGlEnum type,
                          const void* pixels);
  void (*glCopyTexSubImage3D)(SbGlEnum target,
                              SbGlInt32 level,
                              SbGlInt32 xoffset,
                              SbGlInt32 yoffset,
                              SbGlInt32 zoffset,
                              SbGlInt32 x,
                              SbGlInt32 y,
                              SbGlSizei width,
                              SbGlSizei height);
  void (*glCompressedTexImage3D)(SbGlEnum target,
                                 SbGlInt32 level,
                                 SbGlEnum internalformat,
                                 SbGlSizei width,
                                 SbGlSizei height,
                                 SbGlSizei depth,
                                 SbGlInt32 border,
                                 SbGlSizei imageSize,
                                 const void* data);
  void (*glCompressedTexSubImage3D)(SbGlEnum target,
                                    SbGlInt32 level,
                                    SbGlInt32 xoffset,
                                    SbGlInt32 yoffset,
                                    SbGlInt32 zoffset,
                                    SbGlSizei width,
                                    SbGlSizei height,
                                    SbGlSizei depth,
                                    SbGlEnum format,
                                    SbGlSizei imageSize,
                                    const void* data);
  void (*glGenQueries)(SbGlSizei n, SbGlUInt32* ids);
  void (*glDeleteQueries)(SbGlSizei n, const SbGlUInt32* ids);
  SbGlBoolean (*glIsQuery)(SbGlUInt32 id);
  void (*glBeginQuery)(SbGlEnum target, SbGlUInt32 id);
  void (*glEndQuery)(SbGlEnum target);
  void (*glGetQueryiv)(SbGlEnum target, SbGlEnum pname, SbGlInt32* params);
  void (*glGetQueryObjectuiv)(SbGlUInt32 id,
                              SbGlEnum pname,
                              SbGlUInt32* params);
  SbGlBoolean (*glUnmapBuffer)(SbGlEnum target);
  void (*glGetBufferPointerv)(SbGlEnum target, SbGlEnum pname, void** params);
  void (*glDrawBuffers)(SbGlSizei n, const SbGlEnum* bufs);
  void (*glUniformMatrix2x3fv)(SbGlInt32 location,
                               SbGlSizei count,
                               SbGlBoolean transpose,
                               const SbGlFloat* value);
  void (*glUniformMatrix3x2fv)(SbGlInt32 location,
                               SbGlSizei count,
                               SbGlBoolean transpose,
                               const SbGlFloat* value);
  void (*glUniformMatrix2x4fv)(SbGlInt32 location,
                               SbGlSizei count,
                               SbGlBoolean transpose,
                               const SbGlFloat* value);
  void (*glUniformMatrix4x2fv)(SbGlInt32 location,
                               SbGlSizei count,
                               SbGlBoolean transpose,
                               const SbGlFloat* value);
  void (*glUniformMatrix3x4fv)(SbGlInt32 location,
                               SbGlSizei count,
                               SbGlBoolean transpose,
                               const SbGlFloat* value);
  void (*glUniformMatrix4x3fv)(SbGlInt32 location,
                               SbGlSizei count,
                               SbGlBoolean transpose,
                               const SbGlFloat* value);
  void (*glBlitFramebuffer)(SbGlInt32 srcX0,
                            SbGlInt32 srcY0,
                            SbGlInt32 srcX1,
                            SbGlInt32 srcY1,
                            SbGlInt32 dstX0,
                            SbGlInt32 dstY0,
                            SbGlInt32 dstX1,
                            SbGlInt32 dstY1,
                            SbGlBitfield mask,
                            SbGlEnum filter);
  void (*glRenderbufferStorageMultisample)(SbGlEnum target,
                                           SbGlSizei samples,
                                           SbGlEnum internalformat,
                                           SbGlSizei width,
                                           SbGlSizei height);
  void (*glFramebufferTextureLayer)(SbGlEnum target,
                                    SbGlEnum attachment,
                                    SbGlUInt32 texture,
                                    SbGlInt32 level,
                                    SbGlInt32 layer);
  void* (*glMapBufferRange)(SbGlEnum target,
                            SbGlIntPtr offset,
                            SbGlSizeiPtr length,
                            SbGlBitfield access);
  void (*glFlushMappedBufferRange)(SbGlEnum target,
                                   SbGlIntPtr offset,
                                   SbGlSizeiPtr length);
  void (*glBindVertexArray)(SbGlUInt32 array);
  void (*glDeleteVertexArrays)(SbGlSizei n, const SbGlUInt32* arrays);
  void (*glGenVertexArrays)(SbGlSizei n, SbGlUInt32* arrays);
  SbGlBoolean (*glIsVertexArray)(SbGlUInt32 array);
  void (*glGetIntegeri_v)(SbGlEnum target, SbGlUInt32 index, SbGlInt32* data);
  void (*glBeginTransformFeedback)(SbGlEnum primitiveMode);
  void (*glEndTransformFeedback)(void);
  void (*glBindBufferRange)(SbGlEnum target,
                            SbGlUInt32 index,
                            SbGlUInt32 buffer,
                            SbGlIntPtr offset,
                            SbGlSizeiPtr size);
  void (*glBindBufferBase)(SbGlEnum target,
                           SbGlUInt32 index,
                           SbGlUInt32 buffer);
  void (*glTransformFeedbackVaryings)(SbGlUInt32 program,
                                      SbGlSizei count,
                                      const SbGlChar* const* varyings,
                                      SbGlEnum bufferMode);
  void (*glGetTransformFeedbackVarying)(SbGlUInt32 program,
                                        SbGlUInt32 index,
                                        SbGlSizei bufSize,
                                        SbGlSizei* length,
                                        SbGlSizei* size,
                                        SbGlEnum* type,
                                        SbGlChar* name);
  void (*glVertexAttribIPointer)(SbGlUInt32 index,
                                 SbGlInt32 size,
                                 SbGlEnum type,
                                 SbGlSizei stride,
                                 const void* pointer);
  void (*glGetVertexAttribIiv)(SbGlUInt32 index,
                               SbGlEnum pname,
                               SbGlInt32* params);
  void (*glGetVertexAttribIuiv)(SbGlUInt32 index,
                                SbGlEnum pname,
                                SbGlUInt32* params);
  void (*glVertexAttribI4i)(SbGlUInt32 index,
                            SbGlInt32 x,
                            SbGlInt32 y,
                            SbGlInt32 z,
                            SbGlInt32 w);
  void (*glVertexAttribI4ui)(SbGlUInt32 index,
                             SbGlUInt32 x,
                             SbGlUInt32 y,
                             SbGlUInt32 z,
                             SbGlUInt32 w);
  void (*glVertexAttribI4iv)(SbGlUInt32 index, const SbGlInt32* v);
  void (*glVertexAttribI4uiv)(SbGlUInt32 index, const SbGlUInt32* v);
  void (*glGetUniformuiv)(SbGlUInt32 program,
                          SbGlInt32 location,
                          SbGlUInt32* params);
  SbGlInt32 (*glGetFragDataLocation)(SbGlUInt32 program, const SbGlChar* name);
  void (*glUniform1ui)(SbGlInt32 location, SbGlUInt32 v0);
  void (*glUniform2ui)(SbGlInt32 location, SbGlUInt32 v0, SbGlUInt32 v1);
  void (*glUniform3ui)(SbGlInt32 location,
                       SbGlUInt32 v0,
                       SbGlUInt32 v1,
                       SbGlUInt32 v2);
  void (*glUniform4ui)(SbGlInt32 location,
                       SbGlUInt32 v0,
                       SbGlUInt32 v1,
                       SbGlUInt32 v2,
                       SbGlUInt32 v3);
  void (*glUniform1uiv)(SbGlInt32 location,
                        SbGlSizei count,
                        const SbGlUInt32* value);
  void (*glUniform2uiv)(SbGlInt32 location,
                        SbGlSizei count,
                        const SbGlUInt32* value);
  void (*glUniform3uiv)(SbGlInt32 location,
                        SbGlSizei count,
                        const SbGlUInt32* value);
  void (*glUniform4uiv)(SbGlInt32 location,
                        SbGlSizei count,
                        const SbGlUInt32* value);
  void (*glClearBufferiv)(SbGlEnum buffer,
                          SbGlInt32 drawbuffer,
                          const SbGlInt32* value);
  void (*glClearBufferuiv)(SbGlEnum buffer,
                           SbGlInt32 drawbuffer,
                           const SbGlUInt32* value);
  void (*glClearBufferfv)(SbGlEnum buffer,
                          SbGlInt32 drawbuffer,
                          const SbGlFloat* value);
  void (*glClearBufferfi)(SbGlEnum buffer,
                          SbGlInt32 drawbuffer,
                          SbGlFloat depth,
                          SbGlInt32 stencil);
  const SbGlUInt8* (*glGetStringi)(SbGlEnum name, SbGlUInt32 index);
  void (*glCopyBufferSubData)(SbGlEnum readTarget,
                              SbGlEnum writeTarget,
                              SbGlIntPtr readOffset,
                              SbGlIntPtr writeOffset,
                              SbGlSizeiPtr size);
  void (*glGetUniformIndices)(SbGlUInt32 program,
                              SbGlSizei uniformCount,
                              const SbGlChar* const* uniformNames,
                              SbGlUInt32* uniformIndices);
  void (*glGetActiveUniformsiv)(SbGlUInt32 program,
                                SbGlSizei uniformCount,
                                const SbGlUInt32* uniformIndices,
                                SbGlEnum pname,
                                SbGlInt32* params);
  SbGlUInt32 (*glGetUniformBlockIndex)(SbGlUInt32 program,
                                       const SbGlChar* uniformBlockName);
  void (*glGetActiveUniformBlockiv)(SbGlUInt32 program,
                                    SbGlUInt32 uniformBlockIndex,
                                    SbGlEnum pname,
                                    SbGlInt32* params);
  void (*glGetActiveUniformBlockName)(SbGlUInt32 program,
                                      SbGlUInt32 uniformBlockIndex,
                                      SbGlSizei bufSize,
                                      SbGlSizei* length,
                                      SbGlChar* uniformBlockName);
  void (*glUniformBlockBinding)(SbGlUInt32 program,
                                SbGlUInt32 uniformBlockIndex,
                                SbGlUInt32 uniformBlockBinding);
  void (*glDrawArraysInstanced)(SbGlEnum mode,
                                SbGlInt32 first,
                                SbGlSizei count,
                                SbGlSizei instancecount);
  void (*glDrawElementsInstanced)(SbGlEnum mode,
                                  SbGlSizei count,
                                  SbGlEnum type,
                                  const void* indices,
                                  SbGlSizei instancecount);
  SbGlSync (*glFenceSync)(SbGlEnum condition, SbGlBitfield flags);
  SbGlBoolean (*glIsSync)(SbGlSync sync);
  void (*glDeleteSync)(SbGlSync sync);
  SbGlEnum (*glClientWaitSync)(SbGlSync sync,
                               SbGlBitfield flags,
                               SbGlUInt64 timeout);
  void (*glWaitSync)(SbGlSync sync, SbGlBitfield flags, SbGlUInt64 timeout);
  void (*glGetInteger64v)(SbGlEnum pname, SbGlInt64* data);
  void (*glGetSynciv)(SbGlSync sync,
                      SbGlEnum pname,
                      SbGlSizei bufSize,
                      SbGlSizei* length,
                      SbGlInt32* values);
  void (*glGetInteger64i_v)(SbGlEnum target, SbGlUInt32 index, SbGlInt64* data);
  void (*glGetBufferParameteri64v)(SbGlEnum target,
                                   SbGlEnum pname,
                                   SbGlInt64* params);
  void (*glGenSamplers)(SbGlSizei count, SbGlUInt32* samplers);
  void (*glDeleteSamplers)(SbGlSizei count, const SbGlUInt32* samplers);
  SbGlBoolean (*glIsSampler)(SbGlUInt32 sampler);
  void (*glBindSampler)(SbGlUInt32 unit, SbGlUInt32 sampler);
  void (*glSamplerParameteri)(SbGlUInt32 sampler,
                              SbGlEnum pname,
                              SbGlInt32 param);
  void (*glSamplerParameteriv)(SbGlUInt32 sampler,
                               SbGlEnum pname,
                               const SbGlInt32* param);
  void (*glSamplerParameterf)(SbGlUInt32 sampler,
                              SbGlEnum pname,
                              SbGlFloat param);
  void (*glSamplerParameterfv)(SbGlUInt32 sampler,
                               SbGlEnum pname,
                               const SbGlFloat* param);
  void (*glGetSamplerParameteriv)(SbGlUInt32 sampler,
                                  SbGlEnum pname,
                                  SbGlInt32* params);
  void (*glGetSamplerParameterfv)(SbGlUInt32 sampler,
                                  SbGlEnum pname,
                                  SbGlFloat* params);
  void (*glVertexAttribDivisor)(SbGlUInt32 index, SbGlUInt32 divisor);
  void (*glBindTransformFeedback)(SbGlEnum target, SbGlUInt32 id);
  void (*glDeleteTransformFeedbacks)(SbGlSizei n, const SbGlUInt32* ids);
  void (*glGenTransformFeedbacks)(SbGlSizei n, SbGlUInt32* ids);
  SbGlBoolean (*glIsTransformFeedback)(SbGlUInt32 id);
  void (*glPauseTransformFeedback)(void);
  void (*glResumeTransformFeedback)(void);
  void (*glGetProgramBinary)(SbGlUInt32 program,
                             SbGlSizei bufSize,
                             SbGlSizei* length,
                             SbGlEnum* binaryFormat,
                             void* binary);
  void (*glProgramBinary)(SbGlUInt32 program,
                          SbGlEnum binaryFormat,
                          const void* binary,
                          SbGlSizei length);
  void (*glProgramParameteri)(SbGlUInt32 program,
                              SbGlEnum pname,
                              SbGlInt32 value);
  void (*glInvalidateFramebuffer)(SbGlEnum target,
                                  SbGlSizei numAttachments,
                                  const SbGlEnum* attachments);
  void (*glInvalidateSubFramebuffer)(SbGlEnum target,
                                     SbGlSizei numAttachments,
                                     const SbGlEnum* attachments,
                                     SbGlInt32 x,
                                     SbGlInt32 y,
                                     SbGlSizei width,
                                     SbGlSizei height);
  void (*glTexStorage2D)(SbGlEnum target,
                         SbGlSizei levels,
                         SbGlEnum internalformat,
                         SbGlSizei width,
                         SbGlSizei height);
  void (*glTexStorage3D)(SbGlEnum target,
                         SbGlSizei levels,
                         SbGlEnum internalformat,
                         SbGlSizei width,
                         SbGlSizei height,
                         SbGlSizei depth);
  void (*glGetInternalformativ)(SbGlEnum target,
                                SbGlEnum internalformat,
                                SbGlEnum pname,
                                SbGlSizei bufSize,
                                SbGlInt32* params);
} SbGlesInterface;

SB_EXPORT const SbGlesInterface* SbGetGlesInterface();

// Previously defined
// in[gl2.h](https://www.khronos.org/registry/OpenGL/api/GLES2/gl2.h)
#define SB_GL_DEPTH_BUFFER_BIT 0x00000100
#define SB_GL_STENCIL_BUFFER_BIT 0x00000400
#define SB_GL_COLOR_BUFFER_BIT 0x00004000

#define SB_GL_FALSE 0
#define SB_GL_TRUE 1

#define SB_GL_POINTS 0x0000
#define SB_GL_LINES 0x0001
#define SB_GL_LINE_LOOP 0x0002
#define SB_GL_LINE_STRIP 0x0003
#define SB_GL_TRIANGLES 0x0004
#define SB_GL_TRIANGLE_STRIP 0x0005
#define SB_GL_TRIANGLE_FAN 0x0006
#define SB_GL_ZERO 0
#define SB_GL_ONE 1
#define SB_GL_SRC_COLOR 0x0300
#define SB_GL_ONE_MINUS_SRC_COLOR 0x0301
#define SB_GL_SRC_ALPHA 0x0302
#define SB_GL_ONE_MINUS_SRC_ALPHA 0x0303
#define SB_GL_DST_ALPHA 0x0304
#define SB_GL_ONE_MINUS_DST_ALPHA 0x0305
#define SB_GL_DST_COLOR 0x0306
#define SB_GL_ONE_MINUS_DST_COLOR 0x0307
#define SB_GL_SRC_ALPHA_SATURATE 0x0308
#define SB_GL_FUNC_ADD 0x8006
#define SB_GL_BLEND_EQUATION 0x8009
#define SB_GL_BLEND_EQUATION_RGB 0x8009
#define SB_GL_BLEND_EQUATION_ALPHA 0x883D
#define SB_GL_FUNC_SUBTRACT 0x800A
#define SB_GL_FUNC_REVERSE_SUBTRACT 0x800B
#define SB_GL_BLEND_DST_RGB 0x80C8
#define SB_GL_BLEND_SRC_RGB 0x80C9
#define SB_GL_BLEND_DST_ALPHA 0x80CA
#define SB_GL_BLEND_SRC_ALPHA 0x80CB
#define SB_GL_CONSTANT_COLOR 0x8001
#define SB_GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define SB_GL_CONSTANT_ALPHA 0x8003
#define SB_GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define SB_GL_BLEND_COLOR 0x8005
#define SB_GL_ARRAY_BUFFER 0x8892
#define SB_GL_ELEMENT_ARRAY_BUFFER 0x8893
#define SB_GL_ARRAY_BUFFER_BINDING 0x8894
#define SB_GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define SB_GL_STREAM_DRAW 0x88E0
#define SB_GL_STATIC_DRAW 0x88E4
#define SB_GL_DYNAMIC_DRAW 0x88E8
#define SB_GL_BUFFER_SIZE 0x8764
#define SB_GL_BUFFER_USAGE 0x8765
#define SB_GL_CURRENT_VERTEX_ATTRIB 0x8626
#define SB_GL_FRONT 0x0404
#define SB_GL_BACK 0x0405
#define SB_GL_FRONT_AND_BACK 0x0408
#define SB_GL_TEXTURE_2D 0x0DE1
#define SB_GL_CULL_FACE 0x0B44
#define SB_GL_BLEND 0x0BE2
#define SB_GL_DITHER 0x0BD0
#define SB_GL_STENCIL_TEST 0x0B90
#define SB_GL_DEPTH_TEST 0x0B71
#define SB_GL_SCISSOR_TEST 0x0C11
#define SB_GL_POLYGON_OFFSET_FILL 0x8037
#define SB_GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define SB_GL_SAMPLE_COVERAGE 0x80A0
#define SB_GL_NO_ERROR 0
#define SB_GL_INVALID_ENUM 0x0500
#define SB_GL_INVALID_VALUE 0x0501
#define SB_GL_INVALID_OPERATION 0x0502
#define SB_GL_OUT_OF_MEMORY 0x0505
#define SB_GL_CW 0x0900
#define SB_GL_CCW 0x0901
#define SB_GL_LINE_WIDTH 0x0B21
#define SB_GL_ALIASED_POINT_SIZE_RANGE 0x846D
#define SB_GL_ALIASED_LINE_WIDTH_RANGE 0x846E
#define SB_GL_CULL_FACE_MODE 0x0B45
#define SB_GL_FRONT_FACE 0x0B46
#define SB_GL_DEPTH_RANGE 0x0B70
#define SB_GL_DEPTH_WRITEMASK 0x0B72
#define SB_GL_DEPTH_CLEAR_VALUE 0x0B73
#define SB_GL_DEPTH_FUNC 0x0B74
#define SB_GL_STENCIL_CLEAR_VALUE 0x0B91
#define SB_GL_STENCIL_FUNC 0x0B92
#define SB_GL_STENCIL_FAIL 0x0B94
#define SB_GL_STENCIL_PASS_DEPTH_FAIL 0x0B95
#define SB_GL_STENCIL_PASS_DEPTH_PASS 0x0B96
#define SB_GL_STENCIL_REF 0x0B97
#define SB_GL_STENCIL_VALUE_MASK 0x0B93
#define SB_GL_STENCIL_WRITEMASK 0x0B98
#define SB_GL_STENCIL_BACK_FUNC 0x8800
#define SB_GL_STENCIL_BACK_FAIL 0x8801
#define SB_GL_STENCIL_BACK_PASS_DEPTH_FAIL 0x8802
#define SB_GL_STENCIL_BACK_PASS_DEPTH_PASS 0x8803
#define SB_GL_STENCIL_BACK_REF 0x8CA3
#define SB_GL_STENCIL_BACK_VALUE_MASK 0x8CA4
#define SB_GL_STENCIL_BACK_WRITEMASK 0x8CA5
#define SB_GL_VIEWPORT 0x0BA2
#define SB_GL_SCISSOR_BOX 0x0C10
#define SB_GL_COLOR_CLEAR_VALUE 0x0C22
#define SB_GL_COLOR_WRITEMASK 0x0C23
#define SB_GL_UNPACK_ALIGNMENT 0x0CF5
#define SB_GL_PACK_ALIGNMENT 0x0D05
#define SB_GL_MAX_TEXTURE_SIZE 0x0D33
#define SB_GL_MAX_VIEWPORT_DIMS 0x0D3A
#define SB_GL_SUBPIXEL_BITS 0x0D50
#define SB_GL_RED_BITS 0x0D52
#define SB_GL_GREEN_BITS 0x0D53
#define SB_GL_BLUE_BITS 0x0D54
#define SB_GL_ALPHA_BITS 0x0D55
#define SB_GL_DEPTH_BITS 0x0D56
#define SB_GL_STENCIL_BITS 0x0D57
#define SB_GL_POLYGON_OFFSET_UNITS 0x2A00
#define SB_GL_POLYGON_OFFSET_FACTOR 0x8038
#define SB_GL_TEXTURE_BINDING_2D 0x8069
#define SB_GL_SAMPLE_BUFFERS 0x80A8
#define SB_GL_SAMPLES 0x80A9
#define SB_GL_SAMPLE_COVERAGE_VALUE 0x80AA
#define SB_GL_SAMPLE_COVERAGE_INVERT 0x80AB
#define SB_GL_NUM_COMPRESSED_TEXTURE_FORMATS 0x86A2
#define SB_GL_COMPRESSED_TEXTURE_FORMATS 0x86A3
#define SB_GL_DONT_CARE 0x1100
#define SB_GL_FASTEST 0x1101
#define SB_GL_NICEST 0x1102
#define SB_GL_GENERATE_MIPMAP_HINT 0x8192
#define SB_GL_BYTE 0x1400
#define SB_GL_UNSIGNED_BYTE 0x1401
#define SB_GL_SHORT 0x1402
#define SB_GL_UNSIGNED_SHORT 0x1403
#define SB_GL_INT 0x1404
#define SB_GL_UNSIGNED_INT 0x1405
#define SB_GL_FLOAT 0x1406
#define SB_GL_FIXED 0x140C
#define SB_GL_DEPTH_COMPONENT 0x1902
#define SB_GL_ALPHA 0x1906
#define SB_GL_RGB 0x1907
#define SB_GL_RGBA 0x1908
#define SB_GL_LUMINANCE 0x1909
#define SB_GL_LUMINANCE_ALPHA 0x190A
#define SB_GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define SB_GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define SB_GL_UNSIGNED_SHORT_5_6_5 0x8363
#define SB_GL_FRAGMENT_SHADER 0x8B30
#define SB_GL_VERTEX_SHADER 0x8B31
#define SB_GL_MAX_VERTEX_ATTRIBS 0x8869
#define SB_GL_MAX_VERTEX_UNIFORM_VECTORS 0x8DFB
#define SB_GL_MAX_VARYING_VECTORS 0x8DFC
#define SB_GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define SB_GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#define SB_GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#define SB_GL_MAX_FRAGMENT_UNIFORM_VECTORS 0x8DFD
#define SB_GL_SHADER_TYPE 0x8B4F
#define SB_GL_DELETE_STATUS 0x8B80
#define SB_GL_LINK_STATUS 0x8B82
#define SB_GL_VALIDATE_STATUS 0x8B83
#define SB_GL_ATTACHED_SHADERS 0x8B85
#define SB_GL_ACTIVE_UNIFORMS 0x8B86
#define SB_GL_ACTIVE_UNIFORM_MAX_LENGTH 0x8B87
#define SB_GL_ACTIVE_ATTRIBUTES 0x8B89
#define SB_GL_ACTIVE_ATTRIBUTE_MAX_LENGTH 0x8B8A
#define SB_GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define SB_GL_CURRENT_PROGRAM 0x8B8D
#define SB_GL_NEVER 0x0200
#define SB_GL_LESS 0x0201
#define SB_GL_EQUAL 0x0202
#define SB_GL_LEQUAL 0x0203
#define SB_GL_GREATER 0x0204
#define SB_GL_NOTEQUAL 0x0205
#define SB_GL_GEQUAL 0x0206
#define SB_GL_ALWAYS 0x0207
#define SB_GL_KEEP 0x1E00
#define SB_GL_REPLACE 0x1E01
#define SB_GL_INCR 0x1E02
#define SB_GL_DECR 0x1E03
#define SB_GL_INVERT 0x150A
#define SB_GL_INCR_WRAP 0x8507
#define SB_GL_DECR_WRAP 0x8508
#define SB_GL_VENDOR 0x1F00
#define SB_GL_RENDERER 0x1F01
#define SB_GL_VERSION 0x1F02
#define SB_GL_EXTENSIONS 0x1F03
#define SB_GL_NEAREST 0x2600
#define SB_GL_LINEAR 0x2601
#define SB_GL_NEAREST_MIPMAP_NEAREST 0x2700
#define SB_GL_LINEAR_MIPMAP_NEAREST 0x2701
#define SB_GL_NEAREST_MIPMAP_LINEAR 0x2702
#define SB_GL_LINEAR_MIPMAP_LINEAR 0x2703
#define SB_GL_TEXTURE_MAG_FILTER 0x2800
#define SB_GL_TEXTURE_MIN_FILTER 0x2801
#define SB_GL_TEXTURE_WRAP_S 0x2802
#define SB_GL_TEXTURE_WRAP_T 0x2803
#define SB_GL_TEXTURE 0x1702
#define SB_GL_TEXTURE_CUBE_MAP 0x8513
#define SB_GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#define SB_GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define SB_GL_TEXTURE_CUBE_MAP_NEGATIVE_X 0x8516
#define SB_GL_TEXTURE_CUBE_MAP_POSITIVE_Y 0x8517
#define SB_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y 0x8518
#define SB_GL_TEXTURE_CUBE_MAP_POSITIVE_Z 0x8519
#define SB_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#define SB_GL_MAX_CUBE_MAP_TEXTURE_SIZE 0x851C
#define SB_GL_TEXTURE0 0x84C0
#define SB_GL_TEXTURE1 0x84C1
#define SB_GL_TEXTURE2 0x84C2
#define SB_GL_TEXTURE3 0x84C3
#define SB_GL_TEXTURE4 0x84C4
#define SB_GL_TEXTURE5 0x84C5
#define SB_GL_TEXTURE6 0x84C6
#define SB_GL_TEXTURE7 0x84C7
#define SB_GL_TEXTURE8 0x84C8
#define SB_GL_TEXTURE9 0x84C9
#define SB_GL_TEXTURE10 0x84CA
#define SB_GL_TEXTURE11 0x84CB
#define SB_GL_TEXTURE12 0x84CC
#define SB_GL_TEXTURE13 0x84CD
#define SB_GL_TEXTURE14 0x84CE
#define SB_GL_TEXTURE15 0x84CF
#define SB_GL_TEXTURE16 0x84D0
#define SB_GL_TEXTURE17 0x84D1
#define SB_GL_TEXTURE18 0x84D2
#define SB_GL_TEXTURE19 0x84D3
#define SB_GL_TEXTURE20 0x84D4
#define SB_GL_TEXTURE21 0x84D5
#define SB_GL_TEXTURE22 0x84D6
#define SB_GL_TEXTURE23 0x84D7
#define SB_GL_TEXTURE24 0x84D8
#define SB_GL_TEXTURE25 0x84D9
#define SB_GL_TEXTURE26 0x84DA
#define SB_GL_TEXTURE27 0x84DB
#define SB_GL_TEXTURE28 0x84DC
#define SB_GL_TEXTURE29 0x84DD
#define SB_GL_TEXTURE30 0x84DE
#define SB_GL_TEXTURE31 0x84DF
#define SB_GL_ACTIVE_TEXTURE 0x84E0
#define SB_GL_REPEAT 0x2901
#define SB_GL_CLAMP_TO_EDGE 0x812F
#define SB_GL_MIRRORED_REPEAT 0x8370
#define SB_GL_FLOAT_VEC2 0x8B50
#define SB_GL_FLOAT_VEC3 0x8B51
#define SB_GL_FLOAT_VEC4 0x8B52
#define SB_GL_INT_VEC2 0x8B53
#define SB_GL_INT_VEC3 0x8B54
#define SB_GL_INT_VEC4 0x8B55
#define SB_GL_BOOL 0x8B56
#define SB_GL_BOOL_VEC2 0x8B57
#define SB_GL_BOOL_VEC3 0x8B58
#define SB_GL_BOOL_VEC4 0x8B59
#define SB_GL_FLOAT_MAT2 0x8B5A
#define SB_GL_FLOAT_MAT3 0x8B5B
#define SB_GL_FLOAT_MAT4 0x8B5C
#define SB_GL_SAMPLER_2D 0x8B5E
#define SB_GL_SAMPLER_CUBE 0x8B60
#define SB_GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define SB_GL_VERTEX_ATTRIB_ARRAY_SIZE 0x8623
#define SB_GL_VERTEX_ATTRIB_ARRAY_STRIDE 0x8624
#define SB_GL_VERTEX_ATTRIB_ARRAY_TYPE 0x8625
#define SB_GL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886A
#define SB_GL_VERTEX_ATTRIB_ARRAY_POINTER 0x8645
#define SB_GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING 0x889F
#define SB_GL_IMPLEMENTATION_COLOR_READ_TYPE 0x8B9A
#define SB_GL_IMPLEMENTATION_COLOR_READ_FORMAT 0x8B9B
#define SB_GL_COMPILE_STATUS 0x8B81
#define SB_GL_INFO_LOG_LENGTH 0x8B84
#define SB_GL_SHADER_SOURCE_LENGTH 0x8B88
#define SB_GL_SHADER_COMPILER 0x8DFA
#define SB_GL_SHADER_BINARY_FORMATS 0x8DF8
#define SB_GL_NUM_SHADER_BINARY_FORMATS 0x8DF9
#define SB_GL_LOW_FLOAT 0x8DF0
#define SB_GL_MEDIUM_FLOAT 0x8DF1
#define SB_GL_HIGH_FLOAT 0x8DF2
#define SB_GL_LOW_INT 0x8DF3
#define SB_GL_MEDIUM_INT 0x8DF4
#define SB_GL_HIGH_INT 0x8DF5
#define SB_GL_FRAMEBUFFER 0x8D40
#define SB_GL_RENDERBUFFER 0x8D41
#define SB_GL_RGBA4 0x8056
#define SB_GL_RGB5_A1 0x8057
#define SB_GL_RGB565 0x8D62
#define SB_GL_DEPTH_COMPONENT16 0x81A5
#define SB_GL_STENCIL_INDEX8 0x8D48
#define SB_GL_RENDERBUFFER_WIDTH 0x8D42
#define SB_GL_RENDERBUFFER_HEIGHT 0x8D43
#define SB_GL_RENDERBUFFER_INTERNAL_FORMAT 0x8D44
#define SB_GL_RENDERBUFFER_RED_SIZE 0x8D50
#define SB_GL_RENDERBUFFER_GREEN_SIZE 0x8D51
#define SB_GL_RENDERBUFFER_BLUE_SIZE 0x8D52
#define SB_GL_RENDERBUFFER_ALPHA_SIZE 0x8D53
#define SB_GL_RENDERBUFFER_DEPTH_SIZE 0x8D54
#define SB_GL_RENDERBUFFER_STENCIL_SIZE 0x8D55
#define SB_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE 0x8CD0
#define SB_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1
#define SB_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL 0x8CD2
#define SB_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE 0x8CD3
#define SB_GL_COLOR_ATTACHMENT0 0x8CE0
#define SB_GL_DEPTH_ATTACHMENT 0x8D00
#define SB_GL_STENCIL_ATTACHMENT 0x8D20
#define SB_GL_NONE 0
#define SB_GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define SB_GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#define SB_GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define SB_GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS 0x8CD9
#define SB_GL_FRAMEBUFFER_UNSUPPORTED 0x8CDD
#define SB_GL_FRAMEBUFFER_BINDING 0x8CA6
#define SB_GL_RENDERBUFFER_BINDING 0x8CA7
#define SB_GL_MAX_RENDERBUFFER_SIZE 0x84E8
#define SB_GL_INVALID_FRAMEBUFFER_OPERATION 0x0506

// Previously defined in
// [gl3.h](https://www.khronos.org/registry/OpenGL/api/GLES3/gl3.h).
#define SB_GL_READ_BUFFER 0x0C02
#define SB_GL_UNPACK_ROW_LENGTH 0x0CF2
#define SB_GL_UNPACK_SKIP_ROWS 0x0CF3
#define SB_GL_UNPACK_SKIP_PIXELS 0x0CF4
#define SB_GL_PACK_ROW_LENGTH 0x0D02
#define SB_GL_PACK_SKIP_ROWS 0x0D03
#define SB_GL_PACK_SKIP_PIXELS 0x0D04
#define SB_GL_COLOR 0x1800
#define SB_GL_DEPTH 0x1801
#define SB_GL_STENCIL 0x1802
#define SB_GL_RED 0x1903
#define SB_GL_RGB8 0x8051
#define SB_GL_RGBA8 0x8058
#define SB_GL_RGB10_A2 0x8059
#define SB_GL_TEXTURE_BINDING_3D 0x806A
#define SB_GL_UNPACK_SKIP_IMAGES 0x806D
#define SB_GL_UNPACK_IMAGE_HEIGHT 0x806E
#define SB_GL_TEXTURE_3D 0x806F
#define SB_GL_TEXTURE_WRAP_R 0x8072
#define SB_GL_MAX_3D_TEXTURE_SIZE 0x8073
#define SB_GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#define SB_GL_MAX_ELEMENTS_VERTICES 0x80E8
#define SB_GL_MAX_ELEMENTS_INDICES 0x80E9
#define SB_GL_TEXTURE_MIN_LOD 0x813A
#define SB_GL_TEXTURE_MAX_LOD 0x813B
#define SB_GL_TEXTURE_BASE_LEVEL 0x813C
#define SB_GL_TEXTURE_MAX_LEVEL 0x813D
#define SB_GL_MIN 0x8007
#define SB_GL_MAX 0x8008
#define SB_GL_DEPTH_COMPONENT24 0x81A6
#define SB_GL_MAX_TEXTURE_LOD_BIAS 0x84FD
#define SB_GL_TEXTURE_COMPARE_MODE 0x884C
#define SB_GL_TEXTURE_COMPARE_FUNC 0x884D
#define SB_GL_CURRENT_QUERY 0x8865
#define SB_GL_QUERY_RESULT 0x8866
#define SB_GL_QUERY_RESULT_AVAILABLE 0x8867
#define SB_GL_BUFFER_MAPPED 0x88BC
#define SB_GL_BUFFER_MAP_POINTER 0x88BD
#define SB_GL_STREAM_READ 0x88E1
#define SB_GL_STREAM_COPY 0x88E2
#define SB_GL_STATIC_READ 0x88E5
#define SB_GL_STATIC_COPY 0x88E6
#define SB_GL_DYNAMIC_READ 0x88E9
#define SB_GL_DYNAMIC_COPY 0x88EA
#define SB_GL_MAX_DRAW_BUFFERS 0x8824
#define SB_GL_DRAW_BUFFER0 0x8825
#define SB_GL_DRAW_BUFFER1 0x8826
#define SB_GL_DRAW_BUFFER2 0x8827
#define SB_GL_DRAW_BUFFER3 0x8828
#define SB_GL_DRAW_BUFFER4 0x8829
#define SB_GL_DRAW_BUFFER5 0x882A
#define SB_GL_DRAW_BUFFER6 0x882B
#define SB_GL_DRAW_BUFFER7 0x882C
#define SB_GL_DRAW_BUFFER8 0x882D
#define SB_GL_DRAW_BUFFER9 0x882E
#define SB_GL_DRAW_BUFFER10 0x882F
#define SB_GL_DRAW_BUFFER11 0x8830
#define SB_GL_DRAW_BUFFER12 0x8831
#define SB_GL_DRAW_BUFFER13 0x8832
#define SB_GL_DRAW_BUFFER14 0x8833
#define SB_GL_DRAW_BUFFER15 0x8834
#define SB_GL_MAX_FRAGMENT_UNIFORM_COMPONENTS 0x8B49
#define SB_GL_MAX_VERTEX_UNIFORM_COMPONENTS 0x8B4A
#define SB_GL_SAMPLER_3D 0x8B5F
#define SB_GL_SAMPLER_2D_SHADOW 0x8B62
#define SB_GL_FRAGMENT_SHADER_DERIVATIVE_HINT 0x8B8B
#define SB_GL_PIXEL_PACK_BUFFER 0x88EB
#define SB_GL_PIXEL_UNPACK_BUFFER 0x88EC
#define SB_GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#define SB_GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define SB_GL_FLOAT_MAT2x3 0x8B65
#define SB_GL_FLOAT_MAT2x4 0x8B66
#define SB_GL_FLOAT_MAT3x2 0x8B67
#define SB_GL_FLOAT_MAT3x4 0x8B68
#define SB_GL_FLOAT_MAT4x2 0x8B69
#define SB_GL_FLOAT_MAT4x3 0x8B6A
#define SB_GL_SRGB 0x8C40
#define SB_GL_SRGB8 0x8C41
#define SB_GL_SRGB8_ALPHA8 0x8C43
#define SB_GL_COMPARE_REF_TO_TEXTURE 0x884E
#define SB_GL_MAJOR_VERSION 0x821B
#define SB_GL_MINOR_VERSION 0x821C
#define SB_GL_NUM_EXTENSIONS 0x821D
#define SB_GL_RGBA32F 0x8814
#define SB_GL_RGB32F 0x8815
#define SB_GL_RGBA16F 0x881A
#define SB_GL_RGB16F 0x881B
#define SB_GL_VERTEX_ATTRIB_ARRAY_INTEGER 0x88FD
#define SB_GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#define SB_GL_MIN_PROGRAM_TEXEL_OFFSET 0x8904
#define SB_GL_MAX_PROGRAM_TEXEL_OFFSET 0x8905
#define SB_GL_MAX_VARYING_COMPONENTS 0x8B4B
#define SB_GL_TEXTURE_2D_ARRAY 0x8C1A
#define SB_GL_TEXTURE_BINDING_2D_ARRAY 0x8C1D
#define SB_GL_R11F_G11F_B10F 0x8C3A
#define SB_GL_UNSIGNED_INT_10F_11F_11F_REV 0x8C3B
#define SB_GL_RGB9_E5 0x8C3D
#define SB_GL_UNSIGNED_INT_5_9_9_9_REV 0x8C3E
#define SB_GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH 0x8C76
#define SB_GL_TRANSFORM_FEEDBACK_BUFFER_MODE 0x8C7F
#define SB_GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS 0x8C80
#define SB_GL_TRANSFORM_FEEDBACK_VARYINGS 0x8C83
#define SB_GL_TRANSFORM_FEEDBACK_BUFFER_START 0x8C84
#define SB_GL_TRANSFORM_FEEDBACK_BUFFER_SIZE 0x8C85
#define SB_GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN 0x8C88
#define SB_GL_RASTERIZER_DISCARD 0x8C89
#define SB_GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS 0x8C8A
#define SB_GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS 0x8C8B
#define SB_GL_INTERLEAVED_ATTRIBS 0x8C8C
#define SB_GL_SEPARATE_ATTRIBS 0x8C8D
#define SB_GL_TRANSFORM_FEEDBACK_BUFFER 0x8C8E
#define SB_GL_TRANSFORM_FEEDBACK_BUFFER_BINDING 0x8C8F
#define SB_GL_RGBA32UI 0x8D70
#define SB_GL_RGB32UI 0x8D71
#define SB_GL_RGBA16UI 0x8D76
#define SB_GL_RGB16UI 0x8D77
#define SB_GL_RGBA8UI 0x8D7C
#define SB_GL_RGB8UI 0x8D7D
#define SB_GL_RGBA32I 0x8D82
#define SB_GL_RGB32I 0x8D83
#define SB_GL_RGBA16I 0x8D88
#define SB_GL_RGB16I 0x8D89
#define SB_GL_RGBA8I 0x8D8E
#define SB_GL_RGB8I 0x8D8F
#define SB_GL_RED_INTEGER 0x8D94
#define SB_GL_RGB_INTEGER 0x8D98
#define SB_GL_RGBA_INTEGER 0x8D99
#define SB_GL_SAMPLER_2D_ARRAY 0x8DC1
#define SB_GL_SAMPLER_2D_ARRAY_SHADOW 0x8DC4
#define SB_GL_SAMPLER_CUBE_SHADOW 0x8DC5
#define SB_GL_UNSIGNED_INT_VEC2 0x8DC6
#define SB_GL_UNSIGNED_INT_VEC3 0x8DC7
#define SB_GL_UNSIGNED_INT_VEC4 0x8DC8
#define SB_GL_INT_SAMPLER_2D 0x8DCA
#define SB_GL_INT_SAMPLER_3D 0x8DCB
#define SB_GL_INT_SAMPLER_CUBE 0x8DCC
#define SB_GL_INT_SAMPLER_2D_ARRAY 0x8DCF
#define SB_GL_UNSIGNED_INT_SAMPLER_2D 0x8DD2
#define SB_GL_UNSIGNED_INT_SAMPLER_3D 0x8DD3
#define SB_GL_UNSIGNED_INT_SAMPLER_CUBE 0x8DD4
#define SB_GL_UNSIGNED_INT_SAMPLER_2D_ARRAY 0x8DD7
#define SB_GL_BUFFER_ACCESS_FLAGS 0x911F
#define SB_GL_BUFFER_MAP_LENGTH 0x9120
#define SB_GL_BUFFER_MAP_OFFSET 0x9121
#define SB_GL_DEPTH_COMPONENT32F 0x8CAC
#define SB_GL_DEPTH32F_STENCIL8 0x8CAD
#define SB_GL_FLOAT_32_UNSIGNED_INT_24_8_REV 0x8DAD
#define SB_GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING 0x8210
#define SB_GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE 0x8211
#define SB_GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE 0x8212
#define SB_GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE 0x8213
#define SB_GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE 0x8214
#define SB_GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE 0x8215
#define SB_GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE 0x8216
#define SB_GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE 0x8217
#define SB_GL_FRAMEBUFFER_DEFAULT 0x8218
#define SB_GL_FRAMEBUFFER_UNDEFINED 0x8219
#define SB_GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define SB_GL_DEPTH_STENCIL 0x84F9
#define SB_GL_UNSIGNED_INT_24_8 0x84FA
#define SB_GL_DEPTH24_STENCIL8 0x88F0
#define SB_GL_UNSIGNED_NORMALIZED 0x8C17
#define SB_GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define SB_GL_READ_FRAMEBUFFER 0x8CA8
#define SB_GL_DRAW_FRAMEBUFFER 0x8CA9
#define SB_GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define SB_GL_RENDERBUFFER_SAMPLES 0x8CAB
#define SB_GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER 0x8CD4
#define SB_GL_MAX_COLOR_ATTACHMENTS 0x8CDF
#define SB_GL_COLOR_ATTACHMENT1 0x8CE1
#define SB_GL_COLOR_ATTACHMENT2 0x8CE2
#define SB_GL_COLOR_ATTACHMENT3 0x8CE3
#define SB_GL_COLOR_ATTACHMENT4 0x8CE4
#define SB_GL_COLOR_ATTACHMENT5 0x8CE5
#define SB_GL_COLOR_ATTACHMENT6 0x8CE6
#define SB_GL_COLOR_ATTACHMENT7 0x8CE7
#define SB_GL_COLOR_ATTACHMENT8 0x8CE8
#define SB_GL_COLOR_ATTACHMENT9 0x8CE9
#define SB_GL_COLOR_ATTACHMENT10 0x8CEA
#define SB_GL_COLOR_ATTACHMENT11 0x8CEB
#define SB_GL_COLOR_ATTACHMENT12 0x8CEC
#define SB_GL_COLOR_ATTACHMENT13 0x8CED
#define SB_GL_COLOR_ATTACHMENT14 0x8CEE
#define SB_GL_COLOR_ATTACHMENT15 0x8CEF
#define SB_GL_COLOR_ATTACHMENT16 0x8CF0
#define SB_GL_COLOR_ATTACHMENT17 0x8CF1
#define SB_GL_COLOR_ATTACHMENT18 0x8CF2
#define SB_GL_COLOR_ATTACHMENT19 0x8CF3
#define SB_GL_COLOR_ATTACHMENT20 0x8CF4
#define SB_GL_COLOR_ATTACHMENT21 0x8CF5
#define SB_GL_COLOR_ATTACHMENT22 0x8CF6
#define SB_GL_COLOR_ATTACHMENT23 0x8CF7
#define SB_GL_COLOR_ATTACHMENT24 0x8CF8
#define SB_GL_COLOR_ATTACHMENT25 0x8CF9
#define SB_GL_COLOR_ATTACHMENT26 0x8CFA
#define SB_GL_COLOR_ATTACHMENT27 0x8CFB
#define SB_GL_COLOR_ATTACHMENT28 0x8CFC
#define SB_GL_COLOR_ATTACHMENT29 0x8CFD
#define SB_GL_COLOR_ATTACHMENT30 0x8CFE
#define SB_GL_COLOR_ATTACHMENT31 0x8CFF
#define SB_GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE 0x8D56
#define SB_GL_MAX_SAMPLES 0x8D57
#define SB_GL_HALF_FLOAT 0x140B
#define SB_GL_MAP_READ_BIT 0x0001
#define SB_GL_MAP_WRITE_BIT 0x0002
#define SB_GL_MAP_INVALIDATE_RANGE_BIT 0x0004
#define SB_GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#define SB_GL_MAP_FLUSH_EXPLICIT_BIT 0x0010
#define SB_GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#define SB_GL_RG 0x8227
#define SB_GL_RG_INTEGER 0x8228
#define SB_GL_R8 0x8229
#define SB_GL_RG8 0x822B
#define SB_GL_R16F 0x822D
#define SB_GL_R32F 0x822E
#define SB_GL_RG16F 0x822F
#define SB_GL_RG32F 0x8230
#define SB_GL_R8I 0x8231
#define SB_GL_R8UI 0x8232
#define SB_GL_R16I 0x8233
#define SB_GL_R16UI 0x8234
#define SB_GL_R32I 0x8235
#define SB_GL_R32UI 0x8236
#define SB_GL_RG8I 0x8237
#define SB_GL_RG8UI 0x8238
#define SB_GL_RG16I 0x8239
#define SB_GL_RG16UI 0x823A
#define SB_GL_RG32I 0x823B
#define SB_GL_RG32UI 0x823C
#define SB_GL_VERTEX_ARRAY_BINDING 0x85B5
#define SB_GL_R8_SNORM 0x8F94
#define SB_GL_RG8_SNORM 0x8F95
#define SB_GL_RGB8_SNORM 0x8F96
#define SB_GL_RGBA8_SNORM 0x8F97
#define SB_GL_SIGNED_NORMALIZED 0x8F9C
#define SB_GL_PRIMITIVE_RESTART_FIXED_INDEX 0x8D69
#define SB_GL_COPY_READ_BUFFER 0x8F36
#define SB_GL_COPY_WRITE_BUFFER 0x8F37
#define SB_GL_COPY_READ_BUFFER_BINDING 0x8F36
#define SB_GL_COPY_WRITE_BUFFER_BINDING 0x8F37
#define SB_GL_UNIFORM_BUFFER 0x8A11
#define SB_GL_UNIFORM_BUFFER_BINDING 0x8A28
#define SB_GL_UNIFORM_BUFFER_START 0x8A29
#define SB_GL_UNIFORM_BUFFER_SIZE 0x8A2A
#define SB_GL_MAX_VERTEX_UNIFORM_BLOCKS 0x8A2B
#define SB_GL_MAX_FRAGMENT_UNIFORM_BLOCKS 0x8A2D
#define SB_GL_MAX_COMBINED_UNIFORM_BLOCKS 0x8A2E
#define SB_GL_MAX_UNIFORM_BUFFER_BINDINGS 0x8A2F
#define SB_GL_MAX_UNIFORM_BLOCK_SIZE 0x8A30
#define SB_GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS 0x8A31
#define SB_GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS 0x8A33
#define SB_GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#define SB_GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH 0x8A35
#define SB_GL_ACTIVE_UNIFORM_BLOCKS 0x8A36
#define SB_GL_UNIFORM_TYPE 0x8A37
#define SB_GL_UNIFORM_SIZE 0x8A38
#define SB_GL_UNIFORM_NAME_LENGTH 0x8A39
#define SB_GL_UNIFORM_BLOCK_INDEX 0x8A3A
#define SB_GL_UNIFORM_OFFSET 0x8A3B
#define SB_GL_UNIFORM_ARRAY_STRIDE 0x8A3C
#define SB_GL_UNIFORM_MATRIX_STRIDE 0x8A3D
#define SB_GL_UNIFORM_IS_ROW_MAJOR 0x8A3E
#define SB_GL_UNIFORM_BLOCK_BINDING 0x8A3F
#define SB_GL_UNIFORM_BLOCK_DATA_SIZE 0x8A40
#define SB_GL_UNIFORM_BLOCK_NAME_LENGTH 0x8A41
#define SB_GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS 0x8A42
#define SB_GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES 0x8A43
#define SB_GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER 0x8A44
#define SB_GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER 0x8A46
#define SB_GL_INVALID_INDEX 0xFFFFFFFFu
#define SB_GL_MAX_VERTEX_OUTPUT_COMPONENTS 0x9122
#define SB_GL_MAX_FRAGMENT_INPUT_COMPONENTS 0x9125
#define SB_GL_MAX_SERVER_WAIT_TIMEOUT 0x9111
#define SB_GL_OBJECT_TYPE 0x9112
#define SB_GL_SYNC_CONDITION 0x9113
#define SB_GL_SYNC_STATUS 0x9114
#define SB_GL_SYNC_FLAGS 0x9115
#define SB_GL_SYNC_FENCE 0x9116
#define SB_GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define SB_GL_UNSIGNALED 0x9118
#define SB_GL_SIGNALED 0x9119
#define SB_GL_ALREADY_SIGNALED 0x911A
#define SB_GL_TIMEOUT_EXPIRED 0x911B
#define SB_GL_CONDITION_SATISFIED 0x911C
#define SB_GL_WAIT_FAILED 0x911D
#define SB_GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#define SB_GL_TIMEOUT_IGNORED 0xFFFFFFFFFFFFFFFFull
#define SB_GL_VERTEX_ATTRIB_ARRAY_DIVISOR 0x88FE
#define SB_GL_ANY_SAMPLES_PASSED 0x8C2F
#define SB_GL_ANY_SAMPLES_PASSED_CONSERVATIVE 0x8D6A
#define SB_GL_SAMPLER_BINDING 0x8919
#define SB_GL_RGB10_A2UI 0x906F
#define SB_GL_TEXTURE_SWIZZLE_R 0x8E42
#define SB_GL_TEXTURE_SWIZZLE_G 0x8E43
#define SB_GL_TEXTURE_SWIZZLE_B 0x8E44
#define SB_GL_TEXTURE_SWIZZLE_A 0x8E45
#define SB_GL_GREEN 0x1904
#define SB_GL_BLUE 0x1905
#define SB_GL_INT_2_10_10_10_REV 0x8D9F
#define SB_GL_TRANSFORM_FEEDBACK 0x8E22
#define SB_GL_TRANSFORM_FEEDBACK_PAUSED 0x8E23
#define SB_GL_TRANSFORM_FEEDBACK_ACTIVE 0x8E24
#define SB_GL_TRANSFORM_FEEDBACK_BINDING 0x8E25
#define SB_GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#define SB_GL_PROGRAM_BINARY_LENGTH 0x8741
#define SB_GL_NUM_PROGRAM_BINARY_FORMATS 0x87FE
#define SB_GL_PROGRAM_BINARY_FORMATS 0x87FF
#define SB_GL_COMPRESSED_R11_EAC 0x9270
#define SB_GL_COMPRESSED_SIGNED_R11_EAC 0x9271
#define SB_GL_COMPRESSED_RG11_EAC 0x9272
#define SB_GL_COMPRESSED_SIGNED_RG11_EAC 0x9273
#define SB_GL_COMPRESSED_RGB8_ETC2 0x9274
#define SB_GL_COMPRESSED_SRGB8_ETC2 0x9275
#define SB_GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 0x9276
#define SB_GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2 0x9277
#define SB_GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#define SB_GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC 0x9279
#define SB_GL_TEXTURE_IMMUTABLE_FORMAT 0x912F
#define SB_GL_MAX_ELEMENT_INDEX 0x8D6B
#define SB_GL_NUM_SAMPLE_COUNTS 0x9380
#define SB_GL_TEXTURE_IMMUTABLE_LEVELS 0x82DF

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // STARBOARD_GLES_H_
