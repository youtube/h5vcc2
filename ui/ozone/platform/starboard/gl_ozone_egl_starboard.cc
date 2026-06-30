// Copyright 2024 The Cobalt Authors. All Rights Reserved.
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

#include "ui/ozone/platform/starboard/gl_ozone_egl_starboard.h"

#include <memory>

#include "starboard/egl.h"
#include "starboard/gles.h"
#include "ui/gl/buildflags.h"

#if BUILDFLAG(USE_STATIC_ANGLE)
extern "C" {
// The ANGLE internal eglGetProcAddress
EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY
EGL_GetProcAddress(const char* procname);
}
#endif

namespace ui {

// Macro to simplify the lookup of function pointers from a given interface.
// It checks if 'name' matches '#func_name' and returns the corresponding
// function pointer from 'interface_ptr'.
#define GET_INTERFACE_PROC_ADDRESS_ENTRY(func_name, interface_ptr) \
  if (strcmp(name, #func_name) == 0) {                             \
    return reinterpret_cast<gl::GLFunctionPointerType>(            \
        interface_ptr->func_name);                                 \
  }

// Helper function to look up EGL function pointers from the SbEglInterface.
gl::GLFunctionPointerType GetEglInterfaceProcAddress(
    const char* name,
    const SbEglInterface* egl) {
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglChooseConfig, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCopyBuffers, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCreateContext, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCreatePbufferSurface, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCreatePixmapSurface, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCreateWindowSurface, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglDestroyContext, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglDestroySurface, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetConfigAttrib, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetConfigs, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetCurrentDisplay, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetCurrentSurface, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetDisplay, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetError, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetProcAddress, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglInitialize, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglMakeCurrent, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglQueryContext, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglQueryString, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglQuerySurface, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglSwapBuffers, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglTerminate, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglWaitGL, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglWaitNative, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglBindTexImage, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglReleaseTexImage, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglSurfaceAttrib, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglSwapInterval, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglBindAPI, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglQueryAPI, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCreatePbufferFromClientBuffer, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglReleaseThread, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglWaitClient, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetCurrentContext, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCreateSync, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglDestroySync, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglClientWaitSync, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetSyncAttrib, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCreateImage, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglDestroyImage, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglGetPlatformDisplay, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCreatePlatformWindowSurface, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglCreatePlatformPixmapSurface, egl);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(eglWaitSync, egl);
  return nullptr;
}

// Helper function to look up GLES2 function pointers from the SbGlesInterface.
gl::GLFunctionPointerType GetGlesInterfaceProcAddress(
    const char* name,
    const SbGlesInterface* gles) {
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glActiveTexture, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glAttachShader, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBindAttribLocation, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBindBuffer, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBindFramebuffer, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBindRenderbuffer, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBindTexture, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBlendColor, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBlendEquation, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBlendEquationSeparate, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBlendFunc, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBlendFuncSeparate, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBufferData, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glBufferSubData, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glCheckFramebufferStatus, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glClear, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glClearColor, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glClearDepthf, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glClearStencil, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glColorMask, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glCompileShader, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glCompressedTexImage2D, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glCompressedTexSubImage2D, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glCopyTexImage2D, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glCopyTexSubImage2D, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glCreateProgram, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glCreateShader, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glCullFace, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDeleteBuffers, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDeleteFramebuffers, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDeleteProgram, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDeleteRenderbuffers, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDeleteShader, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDeleteTextures, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDepthFunc, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDepthMask, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDepthRangef, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDetachShader, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDisable, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDisableVertexAttribArray, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDrawArrays, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glDrawElements, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glEnable, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glEnableVertexAttribArray, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glFinish, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glFlush, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glFramebufferRenderbuffer, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glFramebufferTexture2D, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glFrontFace, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGenBuffers, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGenerateMipmap, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGenFramebuffers, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGenRenderbuffers, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGenTextures, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetActiveAttrib, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetActiveUniform, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetAttachedShaders, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetAttribLocation, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetBooleanv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetBufferParameteriv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetError, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetFloatv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetFramebufferAttachmentParameteriv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetIntegerv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetProgramiv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetProgramInfoLog, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetRenderbufferParameteriv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetShaderiv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetShaderInfoLog, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetShaderPrecisionFormat, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetShaderSource, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetString, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetTexParameterfv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetTexParameteriv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetUniformfv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetUniformiv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetUniformLocation, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetVertexAttribfv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetVertexAttribiv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glGetVertexAttribPointerv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glHint, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glIsBuffer, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glIsEnabled, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glIsFramebuffer, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glIsProgram, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glIsRenderbuffer, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glIsShader, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glIsTexture, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glLineWidth, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glLinkProgram, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glPixelStorei, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glPolygonOffset, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glReadPixels, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glReleaseShaderCompiler, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glRenderbufferStorage, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glSampleCoverage, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glScissor, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glShaderBinary, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glShaderSource, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glStencilFunc, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glStencilFuncSeparate, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glStencilMask, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glStencilMaskSeparate, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glStencilOp, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glStencilOpSeparate, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glTexImage2D, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glTexParameterf, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glTexParameterfv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glTexParameteri, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glTexParameteriv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glTexSubImage2D, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform1f, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform1fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform1i, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform1iv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform2f, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform2fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform2i, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform2iv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform3f, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform3fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform3i, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform3iv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform4f, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform4fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform4i, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniform4iv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniformMatrix2fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniformMatrix3fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUniformMatrix4fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glUseProgram, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glValidateProgram, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glVertexAttrib1f, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glVertexAttrib1fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glVertexAttrib2f, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glVertexAttrib2fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glVertexAttrib3f, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glVertexAttrib3fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glVertexAttrib4f, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glVertexAttrib4fv, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glVertexAttribPointer, gles);
  GET_INTERFACE_PROC_ADDRESS_ENTRY(glViewport, gles);
  return nullptr;
}

GLOzoneEGLStarboard::GLOzoneEGLStarboard() = default;

GLOzoneEGLStarboard::~GLOzoneEGLStarboard() = default;

scoped_refptr<gl::GLSurface> GLOzoneEGLStarboard::CreateViewGLSurface(
    gl::GLDisplay* display,
    gfx::AcceleratedWidget window) {
  CHECK(window != gfx::kNullAcceleratedWidget);
  // TODO(b/371272304): Verify widget dimensions match our expected display size
  // (likely full screen for Cobalt).
  return gl::InitializeGLSurface(new gl::NativeViewGLSurfaceEGL(
      display->GetAs<gl::GLDisplayEGL>(), window, nullptr));
}

scoped_refptr<gl::GLSurface> GLOzoneEGLStarboard::CreateOffscreenGLSurface(
    gl::GLDisplay* display,
    const gfx::Size& size) {
  return gl::InitializeGLSurface(
      new gl::PbufferGLSurfaceEGL(display->GetAs<gl::GLDisplayEGL>(), size));
}

gl::EGLDisplayPlatform GLOzoneEGLStarboard::GetNativeDisplay() {
  CreateDisplayTypeIfNeeded();
  return gl::EGLDisplayPlatform(
      reinterpret_cast<EGLNativeDisplayType>(display_type_));
}

bool GLOzoneEGLStarboard::LoadGLES2Bindings(
    const gl::GLImplementationParts& implementation) {
  DCHECK_EQ(implementation.gl, gl::kGLImplementationEGLANGLE)
      << "Not supported: " << implementation.ToString();
#if BUILDFLAG(USE_STATIC_ANGLE)
  gl::SetGLGetProcAddressProc(
      reinterpret_cast<gl::GLGetProcAddressProc>(&EGL_GetProcAddress));
  return true;
#else
#if BUILDFLAG(IS_COBALT_HERMETIC_BUILD)
  gl::GLGetProcAddressProc gl_proc =
      [](const char* name) -> gl::GLFunctionPointerType {
    // Retrieve EGL core functions from the SbEglInterface, since
    // some EGL functions might be overridden by the Starboard
    // implementation. If that fails then fallback on direct call to
    // eglGetProcAddress.
    const SbEglInterface* egl = SbGetEglInterface();
    gl::GLFunctionPointerType proc = GetEglInterfaceProcAddress(name, egl);
    if (proc) {
      return proc;
    }

    proc = reinterpret_cast<gl::GLFunctionPointerType>(
        SbGetEglInterface()->eglGetProcAddress(name));
    if (proc) {
      return proc;
    }

    // If still not found, try to retrieve GLES2 functions directly from the
    // SbGlesInterface as a fallback.
    const SbGlesInterface* gles = SbGetGlesInterface();
    return GetGlesInterfaceProcAddress(name, gles);
  };
#else
  gl::GLGetProcAddressProc gl_proc = reinterpret_cast<gl::GLGetProcAddressProc>(
      SbGetEglInterface()->eglGetProcAddress);
#endif

  if (!gl_proc) {
    LOG(ERROR) << "GLOzoneEglStarboard::LoadGLES2Bindings no gl_proc";
    return false;
  }

  gl::SetGLGetProcAddressProc(gl_proc);
  return true;
#endif
}

void GLOzoneEGLStarboard::CreateDisplayTypeIfNeeded() {
  if (!have_display_type_) {
    display_type_ = reinterpret_cast<void*>(SB_EGL_DEFAULT_DISPLAY);
    have_display_type_ = true;
  }
}

}  // namespace ui
