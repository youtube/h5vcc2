// Copyright 2026 The Cobalt Authors. All Rights Reserved.
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

#include <android/native_window.h>
#include <jni.h>

#include <algorithm>

#include "starboard/android/shared/starboard_bridge.h"
#include "starboard/aosp/shared/window_internal.h"
#include "starboard/common/log.h"
#include "starboard/window.h"

bool SbWindowGetSize(SbWindow window, SbWindowSize* size) {
  if (!SbWindowIsValid(window)) {
    SB_DLOG(ERROR) << __FUNCTION__ << ": Invalid window.";
    return false;
  }
  ANativeWindow* native_window = starboard::RefreshWindowSurface(window);
  if (native_window == nullptr) {
    SB_DLOG(ERROR) << __FUNCTION__ << ": Native window has been destroyed.";
    return false;
  }

  size->width = ANativeWindow_getWidth(native_window);
  size->height = ANativeWindow_getHeight(native_window);
  if (size->width <= 0 || size->height <= 0) {
    SB_DLOG(ERROR) << __FUNCTION__ << ": Native window has no size yet.";
    return false;
  }

  JNIEnv* env = jni_zero::AttachCurrentThread();
  starboard::Size display_size =
      starboard::StarboardBridge::GetInstance()->GetDeviceResolution(env);

  // In the off chance we have non-square pixels, use the max ratio so the
  // highest quality video suitable to the device gets selected.
  size->video_pixel_ratio =
      std::max(static_cast<float>(display_size.width) / size->width,
               static_cast<float>(display_size.height) / size->height);

  return true;
}
