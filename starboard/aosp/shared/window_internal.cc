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

#include "starboard/aosp/shared/window_internal.h"

#include <android/native_window.h>

#include "starboard/aosp/shared/window_surface.h"

namespace starboard {

ANativeWindow* RefreshWindowSurface(SbWindow window) {
  // The Android UI thread can drop the last reference to the current surface
  // at any point so the window has to own one for as long as it points at it.
  ANativeWindow* current = android::shared::AcquireWindowSurface();
  if (current == window->native_window) {
    // Already the one being held; drop the extra reference just taken.
    if (current != nullptr) {
      ANativeWindow_release(current);
    }
    return current;
  }

  if (window->native_window != nullptr) {
    ANativeWindow_release(window->native_window);
  }
  window->native_window = current;
  return current;
}

}  // namespace starboard
