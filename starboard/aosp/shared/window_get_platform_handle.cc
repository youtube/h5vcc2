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
#include "starboard/window.h"

void* SbWindowGetPlatformHandle(SbWindow window) {
  if (!SbWindowIsValid(window)) {
    return nullptr;
  }
  // EGLNativeWindowType and ANativeWindow* are the same on Android, so it
  // can be handed straight to eglCreateWindowSurface(). Refreshed first so a
  // window that outlived a background/foreground cycle won't use surfaces
  // that Android already destroyed.
  return starboard::RefreshWindowSurface(window);
}
