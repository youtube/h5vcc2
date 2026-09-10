//
// Copyright 2020 Comcast Cable Communications Management, LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0
//
// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#ifndef THIRD_PARTY_STARBOARD_RDK_SHARED_APPLICATION_RDK_H_
#define THIRD_PARTY_STARBOARD_RDK_SHARED_APPLICATION_RDK_H_

#include "starboard/configuration.h"
#include "starboard/input.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/application.h"
#include "starboard/shared/starboard/queue_application.h"

#include "third_party/starboard/rdk/shared/ess_input.h"
#include "third_party/starboard/rdk/shared/hang_detector.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <essos-app.h>
#include <chrono>

namespace starboard {

class ApplicationRdk : public QueueApplication {
 public:
  explicit ApplicationRdk(SbEventHandleCallback sb_event_handle_callback);
  ~ApplicationRdk() override;

  static ApplicationRdk* Get() {
    return static_cast<ApplicationRdk*>(Application::Get());
  }

  SbWindow CreateSbWindow(const SbWindowOptions* options);
  bool DestroySbWindow(SbWindow window);
  void InjectInputEvent(SbInputData* data);

  EssCtx *GetEssCtx() const { return ctx_; }
  NativeWindowType GetNativeWindow() const { return native_window_; }
  int GetWindowWidth() const { return window_width_; }
  int GetWindowHeight() const { return window_height_; }
  void DisplayInfoChanged();

  bool IsStartImmediate() override { return !HasPreloadSwitch(); }
  bool IsPreloadImmediate() override { return HasPreloadSwitch(); }

  void InjectAccessibilitySettingsChanged();
  void InjectAccessibilityCaptionSettingsChanged();
  void InjectAccessibilityTextToSpeechSettingsChanged();

  const char* GetLocaleId();

 protected:
  // --- Application overrides ---
  void Initialize() override;
  void Teardown() override;
  void Inject(Event* e) override;
  void OnSuspend() override;
  void OnResume() override;

  // --- QueueApplication overrides ---
  bool MayHaveSystemEvents() override;
  Event* PollNextSystemEvent() override;
  Event* WaitForSystemEventWithTimeout(int64_t time) override;
  void WakeSystemEventWait() override;

  // --- Essos ---
  void OnTerminated();
  void OnKeyPressed(unsigned int key);
  void OnKeyReleased(unsigned int key);
  void OnDisplaySize(int width, int height);

 private:
  void BuildEssosContext();
  void FatalError();

  void ScheduleMemoryUsageCheck(int64_t delay);
  int64_t CheckMemoryUsage();
  void ReleaseMemory();

  static EssTerminateListener terminateListener;
  static EssKeyListener keyListener;
  static EssSettingsListener settingsListener;

  EssCtx *ctx_ { nullptr };
  std::unique_ptr<EssInput> input_handler_ { nullptr };
  SbWindow window_ { kSbWindowInvalid };
  NativeWindowType native_window_ { 0 };
  int window_width_ { 0 };
  int window_height_ { 0 };
  bool resize_pending_ { false };

  std::chrono::time_point<std::chrono::steady_clock> ess_loop_last_ts_;
  int ess_timer_fd_ { -1 };
  int wakeup_fd_ { -1 };
  int monitor_timer_fd_ { -1 };

  std::unique_ptr<HangMonitor> hang_monitor_ { nullptr };

  mutable std::mutex locale_mutex_;
  std::unordered_set<std::string> locale_pool_;
};

}  // namespace starboard

#endif  // THIRD_PARTY_STARBOARD_RDK_SHARED_APPLICATION_RDK_H_
