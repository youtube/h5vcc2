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

#include "starboard/aosp/shared/application_aosp.h"

#include <android/input.h>
#include <android/native_window.h>

#include <chrono>

#include "starboard/aosp/shared/key_map.h"
#include "starboard/aosp/shared/window_internal.h"
#include "starboard/aosp/shared/window_surface.h"
#include "starboard/common/log.h"
#include "starboard/event.h"
#include "starboard/input.h"
#include "starboard/key.h"
#include "starboard/shared/starboard/audio_sink/audio_sink_internal.h"
#include "starboard/window.h"

namespace starboard {

namespace {

// If Conceal left no window behind there is nothing for DestroyWindow() to
// release and the caller would wait for the whole timeout.
void OnConcealDispatched(void* context) {
  static_cast<ApplicationAOSP*>(context)->NotifySurfaceReleaseIfNoWindow();
}

unsigned int MetaStateToSbKeyModifiers(int meta_state) {
  unsigned int modifiers = kSbKeyModifiersNone;
  if (meta_state & AMETA_ALT_ON) {
    modifiers |= kSbKeyModifiersAlt;
  }
  if (meta_state & AMETA_CTRL_ON) {
    modifiers |= kSbKeyModifiersCtrl;
  }
  if (meta_state & AMETA_META_ON) {
    modifiers |= kSbKeyModifiersMeta;
  }
  if (meta_state & AMETA_SHIFT_ON) {
    modifiers |= kSbKeyModifiersShift;
  }
  return modifiers;
}

}  // namespace

void ApplicationAOSP::Initialize() {
  SbAudioSinkImpl::Initialize();
}

SbWindow ApplicationAOSP::CreateWindow(const SbWindowOptions* /*options*/) {
  ANativeWindow* native_window = android::shared::AcquireWindowSurface();
  if (native_window == nullptr) {
    SB_LOG(ERROR) << "SbWindowCreate: no Android surface available.";
    return kSbWindowInvalid;
  }
  SbWindow window = new SbWindowPrivate();
  window->native_window = native_window;
  window_ = window;
  return window;
}

bool ApplicationAOSP::DestroyWindow(SbWindow window) {
  if (!SbWindowIsValid(window)) {
    return false;
  }
  if (window_ == window) {
    window_ = kSbWindowInvalid;
  }
  // Null when RefreshWindowSurface() last ran while Android had no surface.
  if (window->native_window != nullptr) {
    ANativeWindow_release(window->native_window);
  }
  delete window;

  // The engine has let go of the surface, so a surfaceDestroyed() blocked in
  // ReleaseWindowSurfaceAndWait() can continue.
  NotifySurfaceReleased();
  return true;
}

bool ApplicationAOSP::InjectKeyEvent(int key_code,
                                     int action,
                                     int unicode_char,
                                     int meta_state) {
  SbKey sb_key = AndroidKeyCodeToSbKey(key_code);
  if (sb_key == kSbKeyUnknown) {
    return false;
  }

  SbInputData* data = new SbInputData();
  data->window = window_;
  data->device_type = kSbInputDeviceTypeRemote;
  // Android delivers repeats as additional ACTION_DOWN events; treat anything
  // that isn't an explicit ACTION_UP as a press.
  data->type = action != AKEY_EVENT_ACTION_UP ? kSbInputEventTypePress
                                              : kSbInputEventTypeUnpress;
  data->key = sb_key;
  data->character = static_cast<wchar_t>(unicode_char);
  data->key_modifiers = MetaStateToSbKeyModifiers(meta_state);
  Inject(new Event(kSbEventTypeInput, data,
                   &ApplicationAOSP::DeleteDestructor<SbInputData>));
  // The volume keys are reported to the app but not consumed, so Android
  // still changes the volume and shows its own indicator.
  return !SystemHandlesKeyCode(key_code);
}

void ApplicationAOSP::NotifySurfaceReleased() {
  {
    std::lock_guard<std::mutex> lock(surface_release_mutex_);
    surface_released_ = true;
  }
  surface_release_cv_.notify_all();
}

void ApplicationAOSP::NotifySurfaceReleaseIfNoWindow() {
  if (SbWindowIsValid(window_)) {
    return;
  }
  NotifySurfaceReleased();
}

bool ApplicationAOSP::ReleaseWindowSurfaceAndWait(int64_t timeout_usec) {
  {
    std::lock_guard<std::mutex> lock(surface_release_mutex_);
    surface_released_ = false;
  }

  Conceal(this, &OnConcealDispatched);

  bool released;
  {
    std::unique_lock<std::mutex> lock(surface_release_mutex_);
    released = surface_release_cv_.wait_for(
        lock, std::chrono::microseconds(timeout_usec),
        [this] { return surface_released_; });
  }
  if (!released) {
    SB_LOG(WARNING) << "Timed out waiting to release the Android surface.";
  }

  // Even if the release timed out, null the surface reference so a stale ref
  // won't be used.
  android::shared::SetWindowSurface(nullptr);
  return released;
}

}  // namespace starboard
