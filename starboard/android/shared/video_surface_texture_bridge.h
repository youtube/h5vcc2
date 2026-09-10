// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_ANDROID_SHARED_VIDEO_SURFACE_TEXTURE_BRIDGE_H_
#define STARBOARD_ANDROID_SHARED_VIDEO_SURFACE_TEXTURE_BRIDGE_H_

#include <jni.h>

#include <array>

#include "base/memory/raw_ref.h"
#include "starboard/common/log.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {

class VideoSurfaceTextureBridge {
 public:
  class Host {
   public:
    Host() = default;
    ~Host() = default;

    virtual void OnFrameAvailable() = 0;
  };

  VideoSurfaceTextureBridge(Host* host)
      : host_(raw_ref<Host>::from_ptr(host)) {}
  ~VideoSurfaceTextureBridge() = default;

  // Disallow copy and assign.
  VideoSurfaceTextureBridge(const VideoSurfaceTextureBridge&) = delete;
  VideoSurfaceTextureBridge& operator=(const VideoSurfaceTextureBridge&) =
      delete;

  void SetOnFrameAvailableListener(
      JNIEnv* env,
      const jni_zero::JavaRef<jobject>& surface_texture) const;
  void RemoveOnFrameAvailableListener(
      JNIEnv* env,
      const jni_zero::JavaRef<jobject>& surface_texture) const;

  static jni_zero::ScopedJavaGlobalRef<jobject> CreateVideoSurfaceTexture(
      JNIEnv* env,
      int gl_texture_id);
  static jni_zero::ScopedJavaGlobalRef<jobject> CreateSurface(
      JNIEnv* env,
      const jni_zero::JavaRef<jobject>& surface_texture);
  static jni_zero::ScopedJavaGlobalRef<jobject> CreateSurfaceForTesting(
      JNIEnv* env);

  static void UpdateTexImage(JNIEnv* env,
                             const jni_zero::JavaRef<jobject>& surface_texture);
  static std::array<float, 16> GetTransformMatrix(
      JNIEnv* env,
      const jni_zero::JavaRef<jobject>& surface_texture);

  void OnFrameAvailable(JNIEnv*) { host_->OnFrameAvailable(); }

 private:
  const raw_ref<Host> host_;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_VIDEO_SURFACE_TEXTURE_BRIDGE_H_
