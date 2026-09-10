// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/android/shared/video_window.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <mutex>
#include <vector>

#include "cobalt/android/jni_headers/VideoSurfaceView_jni.h"
#include "starboard/android/shared/starboard_bridge.h"
#include "starboard/android/shared/surface_destroy_notifier.h"
#include "starboard/common/log.h"
#include "starboard/common/no_destructor.h"
#include "starboard/common/once.h"
#include "starboard/configuration.h"
#include "starboard/shared/gles/gl_call.h"
#include "starboard/shared/starboard/features.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {

using jni_zero::AttachCurrentThread;
using jni_zero::JavaParamRef;

namespace {

// Global video surface pointer mutex.
SB_ONCE_INITIALIZE_FUNCTION(std::mutex, GetViewSurfaceMutex)

jni_zero::ScopedJavaGlobalRef<jobject>& GetGlobalVideoSurface() {
  static NoDestructor<jni_zero::ScopedJavaGlobalRef<jobject>> instance;
  return *instance;
}

scoped_refptr<SurfaceDestroyNotifier>& GetGlobalSurfaceDestroyNotifier() {
  static NoDestructor<scoped_refptr<SurfaceDestroyNotifier>> notifier;
  return *notifier;
}

// Global pointer to the single video window.
ANativeWindow* g_native_video_window = nullptr;
// Global video surface pointer holder.
VideoSurfaceHolder* g_video_surface_holder = nullptr;

void ClearNativeWindow(void* raw_context) {
  ANativeWindow* native_window = static_cast<ANativeWindow*>(raw_context);
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY) {
    SB_LOG(ERROR) << "Found no EGL display in ClearNativeWindow";
    return;
  }

  // Save pre-existing EGL context state to restore at the end of this function.
  // This prevents unbinding the shared GPU thread's context from other active
  // players.
  EGLDisplay old_display = eglGetCurrentDisplay();
  EGLSurface old_draw_surface = eglGetCurrentSurface(EGL_DRAW);
  EGLSurface old_read_surface = eglGetCurrentSurface(EGL_READ);
  EGLContext old_context = eglGetCurrentContext();

  const EGLint kAttributeList[] = {
      EGL_RED_SIZE,
      8,
      EGL_GREEN_SIZE,
      8,
      EGL_BLUE_SIZE,
      8,
      EGL_ALPHA_SIZE,
      8,
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT,
      EGL_NONE,
  };
  // First, query how many configs match the given attribute list.
  EGLint num_configs = 0;
  EGL_CALL(eglChooseConfig(display, kAttributeList, NULL, 0, &num_configs));
  if (num_configs == 0) {
    SB_LOG(ERROR) << "No matching EGL configs found in ClearNativeWindow";
    return;
  }

  // Allocate space to receive the matching configs and retrieve them.
  std::vector<EGLConfig> configs(num_configs);
  EGL_CALL(eglChooseConfig(display, kAttributeList, configs.data(), num_configs,
                           &num_configs));
  EGLNativeWindowType egl_native_window =
      static_cast<EGLNativeWindowType>(native_window);
  EGLConfig config;

  // Find the first config that successfully allow a window surface to be
  // created.
  EGLSurface surface = EGL_NO_SURFACE;
  for (int config_number = 0; config_number < num_configs; ++config_number) {
    config = configs[config_number];
    surface = eglCreateWindowSurface(display, config, egl_native_window, NULL);
    if (surface != EGL_NO_SURFACE) {
      break;
    }
  }
  if (surface == EGL_NO_SURFACE) {
    SB_LOG(ERROR) << "Found no EGL surface in ClearNativeWindow";
    return;
  }

  // Create an OpenGL ES 2.0 context.
  EGLContext context = EGL_NO_CONTEXT;
  EGLint context_attrib_list[] = {
      EGL_CONTEXT_CLIENT_VERSION,
      2,
      EGL_NONE,
  };
  context =
      eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrib_list);
  if (context == EGL_NO_CONTEXT) {
    SB_LOG(ERROR) << "Failed to create EGL context in ClearNativeWindow";
    EGL_CALL(eglDestroySurface(display, surface));
    return;
  }

  /* connect the context to the surface */
  EGL_CALL(eglMakeCurrent(display, surface, surface, context));

  GL_CALL(glClearColor(0, 0, 0, 1));
  GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
  GL_CALL(glFlush());

  EGL_CALL(eglSwapBuffers(display, surface));

  // Restore pre-existing EGL state and destroy temporary cleanup resources.
  EGL_CALL(eglMakeCurrent(old_context == EGL_NO_CONTEXT ? display : old_display,
                          old_draw_surface, old_read_surface, old_context));
  EGL_CALL(eglDestroyContext(display, context));
  EGL_CALL(eglDestroySurface(display, surface));
}

bool IsSurfaceDestroyNotifierEnabled() {
  return features::FeatureList::IsEnabled(
      features::kEnableSurfaceDestroyNotifier);
}

}  // namespace

void JNI_VideoSurfaceView_OnVideoSurfaceChanged(
    JNIEnv* env,
    const JavaParamRef<jobject>& surface) {
  if (IsSurfaceDestroyNotifierEnabled()) {
    scoped_refptr<SurfaceDestroyNotifier> notifier_to_notify;
    {
      std::lock_guard local(*GetViewSurfaceMutex());
      notifier_to_notify = GetGlobalSurfaceDestroyNotifier();
      GetGlobalSurfaceDestroyNotifier() = nullptr;
      GetGlobalVideoSurface().Reset();
      if (g_native_video_window) {
        ANativeWindow_release(g_native_video_window);
        g_native_video_window = nullptr;
      }
      if (surface) {
        GetGlobalVideoSurface().Reset(env, surface);
        g_native_video_window = ANativeWindow_fromSurface(env, surface.obj());
      }
    }  // Lock released before Notify()
    if (notifier_to_notify) {
      notifier_to_notify->Notify();
    }
    return;
  } else {
    std::lock_guard lock(*GetViewSurfaceMutex());
    if (g_video_surface_holder) {
      g_video_surface_holder->OnSurfaceDestroyed();
      g_video_surface_holder = nullptr;
    }
    GetGlobalVideoSurface().Reset();
    if (g_native_video_window) {
      ANativeWindow_release(g_native_video_window);
      g_native_video_window = nullptr;
    }
    if (surface) {
      GetGlobalVideoSurface().Reset(env, surface);
      g_native_video_window = ANativeWindow_fromSurface(env, surface.obj());
    }
  }
}

// static
bool VideoSurfaceHolder::IsVideoSurfaceAvailable() {
  // We only consider video surface is available when there is a video
  // surface and it is not held by any decoder, i.e.
  // g_video_surface_holder is NULL.
  std::lock_guard lock(*GetViewSurfaceMutex());
  if (IsSurfaceDestroyNotifierEnabled()) {
    return !GetGlobalSurfaceDestroyNotifier() && GetGlobalVideoSurface();
  }
  return !g_video_surface_holder && GetGlobalVideoSurface();
}

jni_zero::ScopedJavaLocalRef<jobject>
VideoSurfaceHolder::AcquireVideoSurface() {
  std::lock_guard lock(*GetViewSurfaceMutex());
  if (g_video_surface_holder != NULL) {
    return {};
  }
  if (!GetGlobalVideoSurface()) {
    return {};
  }
  g_video_surface_holder = this;
  JNIEnv* env = jni_zero::AttachCurrentThread();
  return jni_zero::ScopedJavaLocalRef<jobject>(env, GetGlobalVideoSurface());
}

VideoSurfaceHolder::AcquiredSurface VideoSurfaceHolder::AcquireVideoSurface(
    JobQueue* job_queue) {
  if (IsSurfaceDestroyNotifierEnabled()) {
    std::lock_guard lock(*GetViewSurfaceMutex());
    if (GetGlobalSurfaceDestroyNotifier() != nullptr ||
        !GetGlobalVideoSurface()) {
      return {};
    }
    GetGlobalSurfaceDestroyNotifier() =
        make_scoped_refptr<SurfaceDestroyNotifier>(this, job_queue);
    active_notifier_ = GetGlobalSurfaceDestroyNotifier();
    return {active_notifier_, GetGlobalVideoSurface()};
  }

  // non-experiment fallback;
  auto local_surface = AcquireVideoSurface();
  if (!local_surface) {
    return {};
  }
  return {nullptr, GetGlobalVideoSurface()};
}

void VideoSurfaceHolder::ReleaseVideoSurface() {
  if (IsSurfaceDestroyNotifierEnabled()) {
    scoped_refptr<SurfaceDestroyNotifier> notifier_to_disconnect;
    {
      std::lock_guard lock(*GetViewSurfaceMutex());
      notifier_to_disconnect = std::move(active_notifier_);
      auto& global_notifier = GetGlobalSurfaceDestroyNotifier();
      if (global_notifier && global_notifier->IsCurrentHolder(this)) {
        global_notifier = nullptr;
      }
      if (notifier_to_disconnect) {
        notifier_to_disconnect->Disconnect();
      }
      return;
    }
  }
  std::lock_guard lock(*GetViewSurfaceMutex());
  if (g_video_surface_holder == this) {
    g_video_surface_holder = NULL;
  }
}

void VideoSurfaceHolder::CleanUpVideoSurface(
    SbDecodeTargetGraphicsContextProvider* gpu_provider) {
  // Lock *GetViewSurfaceMutex() here, to avoid releasing g_native_video_window
  // during painting.
  std::lock_guard lock(*GetViewSurfaceMutex());

  if (!g_native_video_window) {
    SB_LOG(INFO) << "Tried to clean up video window when it was null.";
    return;
  }

  SB_CHECK(gpu_provider);
  gpu_provider->gles_context_runner(gpu_provider, &ClearNativeWindow,
                                    g_native_video_window);
  SB_LOG(INFO) << "Video surface has been cleared.";
}

void VideoSurfaceHolder::ResetVideoSurface() {
  // Lock *GetViewSurfaceMutex() here, to avoid releasing g_native_video_window
  // during painting.
  std::lock_guard lock(*GetViewSurfaceMutex());

  JNIEnv* env = AttachCurrentThread();
  if (!env) {
    SB_LOG(INFO) << "Tried to reset video window when JNIEnv was null.";
    return;
  }

  StarboardBridge::GetInstance()->ResetVideoSurface(env);
  SB_LOG(INFO) << "Video surface has been reset.";
}

}  // namespace starboard
