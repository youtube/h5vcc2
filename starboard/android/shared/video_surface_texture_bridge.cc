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

#include "starboard/android/shared/video_surface_texture_bridge.h"

#include "starboard/android/shared/safe_jni.h"  // Required for std::array JNI conversion
#include "third_party/jni_zero/jni_zero.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "cobalt/android/jni_headers/VideoSurfaceTexture_jni.h"

namespace starboard {
namespace {
using jni_zero::JavaParamRef;
using jni_zero::JavaRef;
using jni_zero::ScopedJavaGlobalRef;
using jni_zero::ScopedJavaLocalRef;
}  // namespace

void VideoSurfaceTextureBridge::SetOnFrameAvailableListener(
    JNIEnv* env,
    const JavaRef<jobject>& surface_texture) const {
  Java_VideoSurfaceTexture_setOnFrameAvailableListener(
      env, surface_texture, reinterpret_cast<jlong>(this));
}

void VideoSurfaceTextureBridge::RemoveOnFrameAvailableListener(
    JNIEnv* env,
    const JavaRef<jobject>& surface_texture) const {
  Java_VideoSurfaceTexture_removeOnFrameAvailableListener(env, surface_texture);
}

// static
ScopedJavaGlobalRef<jobject>
VideoSurfaceTextureBridge::CreateVideoSurfaceTexture(JNIEnv* env,
                                                     int gl_texture_id) {
  SB_CHECK(env);
  return ScopedJavaGlobalRef<jobject>(
      Java_VideoSurfaceTexture_Constructor(env, gl_texture_id));
}

// static
ScopedJavaGlobalRef<jobject> VideoSurfaceTextureBridge::CreateSurface(
    JNIEnv* env,
    const JavaRef<jobject>& surface_texture) {
  return ScopedJavaGlobalRef<jobject>(
      Java_VideoSurfaceTexture_createSurface(env, surface_texture));
}

// static
ScopedJavaGlobalRef<jobject> VideoSurfaceTextureBridge::CreateSurfaceForTesting(
    JNIEnv* env) {
  return ScopedJavaGlobalRef<jobject>(
      Java_VideoSurfaceTexture_createSurfaceForTesting(env));
}

// static
void VideoSurfaceTextureBridge::UpdateTexImage(
    JNIEnv* env,
    const JavaRef<jobject>& surface_texture) {
  Java_VideoSurfaceTexture_updateTexImage(env, surface_texture);
}

// static
std::array<float, 16> VideoSurfaceTextureBridge::GetTransformMatrix(
    JNIEnv* env,
    const JavaRef<jobject>& surface_texture) {
  return Java_VideoSurfaceTexture_getTransformMatrix(env, surface_texture);
}

}  // namespace starboard
