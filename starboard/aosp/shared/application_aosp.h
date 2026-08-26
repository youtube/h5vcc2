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

#ifndef STARBOARD_AOSP_SHARED_APPLICATION_AOSP_H_
#define STARBOARD_AOSP_SHARED_APPLICATION_AOSP_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "starboard/shared/starboard/queue_application.h"
#include "starboard/window.h"

namespace starboard {

// Minimal QueueApplication for AOSP.
//
// It owns the Starboard event loop and is the base application the AOSP window
// and input handling hang off. ApplicationAndroid aborts on the event-loop
// methods, so AOSP cannot use it.
//
// Lifetime and ownership: a single instance is created on the stack by
// SbRunStarboardMain() and lives for as long as Starboard runs.
//
// Threading: the event queue is thread-safe, so events may be injected from any
// thread, including the Android UI thread. Callers running on Android threads
// must reach the instance through GetIfExists(), because the Activity can call
// into Starboard before this object exists and after it is gone.
class ApplicationAOSP : public QueueApplication {
 public:
  explicit ApplicationAOSP(SbEventHandleCallback sb_event_handle_callback)
      : QueueApplication(sb_event_handle_callback) {
    g_instance.store(this, std::memory_order_release);
  }
  ~ApplicationAOSP() override {
    // Clear here instead of letting ~Application clear it because it runs only
    // after ~QueueApplication has already destroyed the event queue. If a JNI
    // thread would Inject() between the queue destruction and the application
    // destruction could inject into a destroyed queue. So we destroy the
    // application instance here to prevent it.
    g_instance.store(nullptr, std::memory_order_release);
  }

  // Returns the live application, or nullptr when there is none.
  static ApplicationAOSP* GetIfExists() {
    return g_instance.load(std::memory_order_acquire);
  }

  // Aborts if there is no application. Only use this from the Starboard thread,
  // (from code that runs inside SbRunStarboardMain)
  static ApplicationAOSP* Get() {
    return static_cast<ApplicationAOSP*>(Application::Get());
  }

  // proxies for SbWindowCreate/SbWindowDestroy
  SbWindow CreateWindow(const SbWindowOptions* options);
  bool DestroyWindow(SbWindow window);

  // Converts an Android key event into a Starboard input event and injects it
  // into the engine.
  bool InjectKeyEvent(int key_code,
                      int action,
                      int unicode_char,
                      int meta_state);

  // Injects kSbEventTypeConceal and blocks the caller until the engine has
  // destroyed the Starboard window, or times out.
  //
  // Android invalidates the Surface as soon as SurfaceHolder.surfaceDestroyed()
  // returns, so the engine has to let go of the ANativeWindow first, otherwise
  // it could use a stale surface.
  bool ReleaseWindowSurfaceAndWait(int64_t timeout_usec);

  // Reports that the engine has let go of the Android surface, releasing a
  // caller blocked in ReleaseWindowSurfaceAndWait().
  void NotifySurfaceReleased();

  // Same, but only when the conceal found no window to destroy and nothing
  // will ever reach DestroyWindow(). Avoids waiting for the whole timeout if
  // the window was already destroyed. Starboard thread only.
  void NotifySurfaceReleaseIfNoWindow();

 protected:
  // Creates the platform audio sink. Android TV does it from the
  // CobaltActivity, which AOSP doesn't have.
  void Initialize() override;

  // AOSP has no native event queue for the to poll, Android delivers
  // key events to the Activity and MainActivity forwards them over JNI
  // to Inject()
  //
  // From Chromium side it happens in the same way
  // via MessagePumpUIStarboard::ScheduleWork() calling SbEventSchedule().
  //
  // So GetNextEvent() only waits on the injected queue.
  bool MayHaveSystemEvents() override { return false; }
  Event* WaitForSystemEventWithTimeout(int64_t /*time*/) override {
    return nullptr;
  }
  void WakeSystemEventWait() override {}

 private:
  // The live instance, or nullptr when there is none.
  static inline std::atomic<ApplicationAOSP*> g_instance{nullptr};

  // The window CreateWindow() handed out, so injected input events can name the
  // window they belong to.
  SbWindow window_ = kSbWindowInvalid;

  std::mutex surface_release_mutex_;
  std::condition_variable surface_release_cv_;
  // Tells if the engine has let go of the Android surface. Guarded by
  // |surface_release_mutex_|.
  bool surface_released_ = false;
};

}  // namespace starboard

#endif  // STARBOARD_AOSP_SHARED_APPLICATION_AOSP_H_
