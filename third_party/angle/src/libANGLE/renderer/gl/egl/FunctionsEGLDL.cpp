//
// Copyright 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// FunctionsEGLDL.cpp: Implements the FunctionsEGLDL class.

#include "libANGLE/renderer/gl/egl/FunctionsEGLDL.h"

#include <dlfcn.h>

#if defined(ANGLE_USE_STARBOARD)
#include "starboard/egl.h"
#endif

namespace rx
{
namespace
{
#if !defined(ANGLE_USE_STARBOARD)
// In ideal world, we would want this to be a member of FunctionsEGLDL,
// and call dlclose() on it in ~FunctionsEGLDL().
// However, some GL implementations are broken and don't allow multiple
// load/unload cycles, but only static linking with them.
// That's why we dlopen() this handle once and never dlclose() it.
// This is consistent with Chromium's CleanupNativeLibraries() code,
// referencing crbug.com/250813 and http://www.xfree86.org/4.3.0/DRI11.html
void *nativeEGLHandle;
#endif
}  // anonymous namespace

FunctionsEGLDL::FunctionsEGLDL() : mGetProcAddressPtr(nullptr) {}

FunctionsEGLDL::~FunctionsEGLDL() {}

egl::Error FunctionsEGLDL::initialize(EGLAttrib platformType,
                                      EGLNativeDisplayType nativeDisplay,
                                      const char *libName,
                                      void *eglHandle)
{
#if defined(ANGLE_USE_STARBOARD)
    const SbEglInterface* sb_egl = SbGetEglInterface();
    if (!sb_egl || !sb_egl->eglGetProcAddress)
    {
        return egl::Error(EGL_NOT_INITIALIZED, "Could not find SbGetEglInterface or eglGetProcAddress");
    }
    mGetProcAddressPtr =
        reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(sb_egl->eglGetProcAddress);
#else
    if (eglHandle)
    {
        // If the handle is provided, use it.
        // Caller has already dlopened the vendor library.
        nativeEGLHandle = eglHandle;
    }

    if (!nativeEGLHandle)
    {
        nativeEGLHandle = dlopen(libName, RTLD_NOW);
        if (!nativeEGLHandle)
        {
            std::ostringstream err;
            err << "Could not dlopen native EGL: " << dlerror();
            return egl::Error(EGL_NOT_INITIALIZED, err.str());
        }
    }

    mGetProcAddressPtr =
        reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(dlsym(nativeEGLHandle, "eglGetProcAddress"));
    if (!mGetProcAddressPtr)
    {
        return egl::Error(EGL_NOT_INITIALIZED, "Could not find eglGetProcAddress");
    }
#endif

    return FunctionsEGL::initialize(platformType, nativeDisplay);
}

void *FunctionsEGLDL::getProcAddress(const char *name) const
{
    if (mGetProcAddressPtr)
    {
        void *f = reinterpret_cast<void *>(mGetProcAddressPtr(name));
        if (f)
        {
            return f;
        }
    }
#if defined(ANGLE_USE_STARBOARD)
    const SbEglInterface* egl = SbGetEglInterface();
    if (egl)
    {
        if (strcmp(name, "eglChooseConfig") == 0) return reinterpret_cast<void*>(egl->eglChooseConfig);
        if (strcmp(name, "eglCopyBuffers") == 0) return reinterpret_cast<void*>(egl->eglCopyBuffers);
        if (strcmp(name, "eglCreateContext") == 0) return reinterpret_cast<void*>(egl->eglCreateContext);
        if (strcmp(name, "eglCreatePbufferSurface") == 0) return reinterpret_cast<void*>(egl->eglCreatePbufferSurface);
        if (strcmp(name, "eglCreatePixmapSurface") == 0) return reinterpret_cast<void*>(egl->eglCreatePixmapSurface);
        if (strcmp(name, "eglCreateWindowSurface") == 0) return reinterpret_cast<void*>(egl->eglCreateWindowSurface);
        if (strcmp(name, "eglDestroyContext") == 0) return reinterpret_cast<void*>(egl->eglDestroyContext);
        if (strcmp(name, "eglDestroySurface") == 0) return reinterpret_cast<void*>(egl->eglDestroySurface);
        if (strcmp(name, "eglGetConfigAttrib") == 0) return reinterpret_cast<void*>(egl->eglGetConfigAttrib);
        if (strcmp(name, "eglGetConfigs") == 0) return reinterpret_cast<void*>(egl->eglGetConfigs);
        if (strcmp(name, "eglGetCurrentDisplay") == 0) return reinterpret_cast<void*>(egl->eglGetCurrentDisplay);
        if (strcmp(name, "eglGetCurrentSurface") == 0) return reinterpret_cast<void*>(egl->eglGetCurrentSurface);
        if (strcmp(name, "eglGetDisplay") == 0) return reinterpret_cast<void*>(egl->eglGetDisplay);
        if (strcmp(name, "eglGetError") == 0) return reinterpret_cast<void*>(egl->eglGetError);
        if (strcmp(name, "eglGetProcAddress") == 0) return reinterpret_cast<void*>(egl->eglGetProcAddress);
        if (strcmp(name, "eglInitialize") == 0) return reinterpret_cast<void*>(egl->eglInitialize);
        if (strcmp(name, "eglMakeCurrent") == 0) return reinterpret_cast<void*>(egl->eglMakeCurrent);
        if (strcmp(name, "eglQueryContext") == 0) return reinterpret_cast<void*>(egl->eglQueryContext);
        if (strcmp(name, "eglQueryString") == 0) return reinterpret_cast<void*>(egl->eglQueryString);
        if (strcmp(name, "eglQuerySurface") == 0) return reinterpret_cast<void*>(egl->eglQuerySurface);
        if (strcmp(name, "eglSwapBuffers") == 0) return reinterpret_cast<void*>(egl->eglSwapBuffers);
        if (strcmp(name, "eglTerminate") == 0) return reinterpret_cast<void*>(egl->eglTerminate);
        if (strcmp(name, "eglWaitGL") == 0) return reinterpret_cast<void*>(egl->eglWaitGL);
        if (strcmp(name, "eglWaitNative") == 0) return reinterpret_cast<void*>(egl->eglWaitNative);
        if (strcmp(name, "eglBindTexImage") == 0) return reinterpret_cast<void*>(egl->eglBindTexImage);
        if (strcmp(name, "eglReleaseTexImage") == 0) return reinterpret_cast<void*>(egl->eglReleaseTexImage);
        if (strcmp(name, "eglSurfaceAttrib") == 0) return reinterpret_cast<void*>(egl->eglSurfaceAttrib);
        if (strcmp(name, "eglSwapInterval") == 0) return reinterpret_cast<void*>(egl->eglSwapInterval);
        if (strcmp(name, "eglBindAPI") == 0) return reinterpret_cast<void*>(egl->eglBindAPI);
        if (strcmp(name, "eglQueryAPI") == 0) return reinterpret_cast<void*>(egl->eglQueryAPI);
        if (strcmp(name, "eglCreatePbufferFromClientBuffer") == 0) return reinterpret_cast<void*>(egl->eglCreatePbufferFromClientBuffer);
        if (strcmp(name, "eglReleaseThread") == 0) return reinterpret_cast<void*>(egl->eglReleaseThread);
        if (strcmp(name, "eglWaitClient") == 0) return reinterpret_cast<void*>(egl->eglWaitClient);
        if (strcmp(name, "eglGetCurrentContext") == 0) return reinterpret_cast<void*>(egl->eglGetCurrentContext);
    }
    return nullptr;
#else
    return dlsym(nativeEGLHandle, name);
#endif
}

}  // namespace rx

