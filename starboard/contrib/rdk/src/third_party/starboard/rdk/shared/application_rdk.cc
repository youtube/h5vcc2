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
// SPDX-License-Identifier: Apache-2.0//
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

#include "third_party/starboard/rdk/shared/application_rdk.h"

#include "starboard/common/log.h"
#include "starboard/event.h"
#include "starboard/speech_synthesis.h"
#include "starboard/shared/starboard/audio_sink/audio_sink_internal.h"
#include "starboard/shared/starboard/media/key_system_supportability_cache.h"
#include "starboard/shared/starboard/media/mime_supportability_cache.h"
#include "third_party/starboard/rdk/shared/window/window_internal.h"
#include "third_party/starboard/rdk/shared/log_override.h"
#include "third_party/starboard/rdk/shared/time_constants.h"
#include "third_party/starboard/rdk/shared/platform/platform_interface.h"
#include "third_party/starboard/rdk/shared/libcobalt.h"

#if defined(ENABLE_RDKSERVICES_API)
#include "third_party/starboard/rdk/shared/rdkservices.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <cstring>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <malloc.h>
#include <chrono>

using namespace std::chrono;
using namespace std::chrono_literals;

namespace starboard {

void Initialize();
void Teardown();

void ForceStop();

EssTerminateListener ApplicationRdk::terminateListener = {
  //terminated
  [](void* data) { reinterpret_cast<ApplicationRdk*>(data)->OnTerminated(); }
};

EssKeyListener ApplicationRdk::keyListener = {
  // keyPressed
  [](void* data, unsigned int key) { reinterpret_cast<ApplicationRdk*>(data)->OnKeyPressed(key); },
  // keyReleased
  [](void* data, unsigned int key) { reinterpret_cast<ApplicationRdk*>(data)->OnKeyReleased(key); },
  // keyRepeat
  [](void* data, unsigned int key) { reinterpret_cast<ApplicationRdk*>(data)->OnKeyPressed(key); }
};

EssSettingsListener ApplicationRdk::settingsListener = {
  // displaySize
  [](void *data, int width, int height ) { reinterpret_cast<ApplicationRdk*>(data)->OnDisplaySize(width, height); },
  // displaySafeArea
  nullptr
};

constexpr auto kEssRunLoopPeriod = 16666us;

static void setTimerInterval(int fd, microseconds time) {
  struct itimerspec timeout;
  timeout.it_value.tv_sec = time.count() / kSbTimeSecond;
  timeout.it_value.tv_nsec = ( time.count() % kSbTimeSecond ) * kSbTimeNanosecondsPerMicrosecond;
  timeout.it_interval.tv_sec = timeout.it_value.tv_sec;
  timeout.it_interval.tv_nsec = timeout.it_value.tv_nsec;
  int rc = timerfd_settime(fd, 0, &timeout, NULL);
  if (rc == -1) {
    SB_LOG(ERROR) << "Failed to set timer interval, error: " << errno << " ("<< strerror(errno) << ')';
  }
}

ApplicationRdk::ApplicationRdk(SbEventHandleCallback sb_event_handle_callback)
  : QueueApplication(sb_event_handle_callback)
  , input_handler_(new EssInput)
  , hang_monitor_(new HangMonitor("ApplicationRdk")) {
  BuildEssosContext();
}

ApplicationRdk::~ApplicationRdk() {
  if (SbWindowIsValid(window_)) {
    DestroySbWindow(window_);
  }
  if (ctx_ && native_window_ != 0) {
    if (!EssContextDestroyNativeWindow(ctx_, native_window_)) {
      const char* detail = EssContextGetLastErrorDetail(ctx_);
      SB_LOG(ERROR) << "Essos error: '" << detail << '\'';
    }
    native_window_ = 0;
  }
  if (ctx_) {
    EssContextDestroy(ctx_);
    ctx_ = nullptr;
  }
}

void ApplicationRdk::Initialize() {
  wakeup_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if ( wakeup_fd_ == -1 ) {
    SB_LOG(ERROR) << "Failed to create eventfd, error: " << errno << " (" << strerror(errno) << ')';
  }

  ess_timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if ( ess_timer_fd_ == -1 ) {
    SB_LOG(ERROR) << "Failed to create timerfd, error: " << errno << " (" << strerror(errno) << ')';
  } else {
    setTimerInterval(ess_timer_fd_, kEssRunLoopPeriod);
  }

#if defined(COBALT_BUILD_TYPE_DEVEL)
  hang_monitor_.reset();
  SB_LOG(INFO) << "Disable application watch dog for devel build.";
#else
  monitor_timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if ( monitor_timer_fd_ == -1 ) {
    SB_LOG(ERROR) << "Failed to create timerfd, error: " << errno << " (" << strerror(errno) << ')';
    hang_monitor_.reset();
  } else {
    setTimerInterval(monitor_timer_fd_, hang_monitor_->GetResetInterval());
  }
#endif

  SbAudioSinkImpl::Initialize();
  libcobalt_api::Initialize();
  MimeSupportabilityCache::GetInstance()->SetCacheEnabled(true);
  KeySystemSupportabilityCache::GetInstance()->SetCacheEnabled(true);

  ScheduleMemoryUsageCheck(kSbTimeSecond);
}

void ApplicationRdk::Teardown() {
  SbAudioSinkImpl::TearDown();
  libcobalt_api::Teardown();
  platform::PlatformInterface::get().teardown();

  close(ess_timer_fd_);
  close(wakeup_fd_);
  if ( !(monitor_timer_fd_ < 0) )
      close(monitor_timer_fd_);
  ess_timer_fd_ = wakeup_fd_ = monitor_timer_fd_ = -1;
}

bool ApplicationRdk::MayHaveSystemEvents() {
  return true;
}

ApplicationRdk::Event* ApplicationRdk::PollNextSystemEvent() {
  auto now = steady_clock::now();
  if ((now - ess_loop_last_ts_) > kEssRunLoopPeriod) {
    ess_loop_last_ts_ = now;
    EssContextRunEventLoopOnce( ctx_ );
  }
  return NULL;
}

ApplicationRdk::Event* ApplicationRdk::WaitForSystemEventWithTimeout(int64_t time) {
  struct timespec timeout;
  struct pollfd fds[3];
  int fds_sz = 0;
  int rc = 0;

  if ( !(ess_timer_fd_ < 0) ) {
    fds[fds_sz].fd = ess_timer_fd_;
    fds[fds_sz].events = POLLIN;
    fds[fds_sz].revents = 0;
    ++fds_sz;
  }

  if ( !(wakeup_fd_ < 0) ) {
    fds[fds_sz].fd = wakeup_fd_;
    fds[fds_sz].events = POLLIN;
    fds[fds_sz].revents = 0;
    ++fds_sz;
  }

  if ( !(monitor_timer_fd_ < 0) ) {
    fds[fds_sz].fd = monitor_timer_fd_;
    fds[fds_sz].events = POLLIN;
    fds[fds_sz].revents = 0;
    ++fds_sz;
  }

  if ( fds_sz != 0 ) {
    timeout.tv_sec = time / kSbTimeSecond;
    timeout.tv_nsec = (time % kSbTimeSecond) * kSbTimeNanosecondsPerMicrosecond;
    rc = ppoll(fds, fds_sz, &timeout, NULL);
  }

  if ( rc > 0 ) {
    for (int i = 0; i < fds_sz; ++i) {
      if ( (fds[i].revents & POLLIN) != POLLIN )
        continue;
      // Ack timer or wakeup event
      uint64_t tmp;
      read(fds[i].fd, &tmp, sizeof(uint64_t));

      if ( fds[i].fd == monitor_timer_fd_ ) {
        hang_monitor_->Reset();
      }
    }
  }

  return NULL;
}

void ApplicationRdk::WakeSystemEventWait() {
  uint64_t u = 1;
  write(wakeup_fd_, &u, sizeof(uint64_t));
}

SbWindow ApplicationRdk::CreateSbWindow(const SbWindowOptions* options) {
  SB_DCHECK(window_ == nullptr);
  if (window_ != nullptr || !ctx_) {
    return kSbWindowInvalid;
  }

  bool error = false;

  if (native_window_ == 0) {
    if (!EssContextGetDisplaySize(ctx_, &window_width_, &window_height_)) {
      error = true;
    }

    if (!error && resize_pending_) {
      EssContextResizeWindow(ctx_, window_width_, window_height_);
      resize_pending_ = false;
    }

    if (!error) {
      if (!EssContextCreateNativeWindow(ctx_, window_width_, window_height_,
                                        &native_window_)) {
        error = true;
      } else if (!EssContextStart(ctx_)) {
        error = true;
        EssContextDestroyNativeWindow(ctx_, native_window_);
        native_window_ = 0;
      }
    }

    if (error) {
      const char* detail = EssContextGetLastErrorDetail(ctx_);
      SB_LOG(ERROR) << "Essos error: '" << detail << '\'';
      FatalError();
      return kSbWindowInvalid;
    }
  } else if (resize_pending_) {
    if (EssContextGetDisplaySize(ctx_, &window_width_, &window_height_)) {
      EssContextResizeWindow(ctx_, window_width_, window_height_);
    }
    resize_pending_ = false;
  }

  window_ = new SbWindowPrivate(options);
  return window_;
}

bool ApplicationRdk::DestroySbWindow(SbWindow window) {
  SB_CHECK(window == window_);
  if (!SbWindowIsValid(window)) {
    return false;
  }
  window_ = kSbWindowInvalid;
  delete window;
  return true;
}

void ApplicationRdk::InjectInputEvent(SbInputData* data) {
  if (native_window_ == 0) {
    ApplicationRdk::DeleteDestructor<SbInputData>(data);
    return;
  }

  data->window = window_;
  Inject(new Event(kSbEventTypeInput, data,
                   &ApplicationRdk::DeleteDestructor<SbInputData>));
}

void ApplicationRdk::Inject(Event* e) {
  if (e && e->event && e->event->type == kSbEventTypeFreeze) {
    ForceStop();
  }

  QueueApplication::Inject(e);
}

void ApplicationRdk::OnSuspend() {
  SbSpeechSynthesisCancel();

  if (!(monitor_timer_fd_ < 0)) {
    setTimerInterval(monitor_timer_fd_, 0s);
  }

  // Disarm Essos event loop timer while suspended to prevent periodic CPU wakeups.
  if (!(ess_timer_fd_ < 0)) {
    setTimerInterval(ess_timer_fd_, 0s);
  }

  if (ctx_) {
    // Unset the Essos terminate listener to prevent callback loops
    // when the window is destroyed during suspend.
    EssContextSetTerminateListener(ctx_, nullptr, nullptr);

    // Stop Essos event dispatching while keeping the native window plane alive.
    // Keeping the Wayland wl_surface handle registered prevents Westeros from
    // encountering invalid object protocol errors when dispatching background
    // state events to the client.
    if (native_window_ != 0) {
      EssContextStop(ctx_);
    }
  }

  platform::PlatformInterface::get().suspend();
}

void ApplicationRdk::OnResume() {
  if (ctx_) {
    EssContextSetTerminateListener(ctx_, this, &terminateListener);

    if (native_window_ != 0) {
      if (!EssContextStart(ctx_)) {
        const char* detail = EssContextGetLastErrorDetail(ctx_);
        SB_LOG(ERROR) << "Essos error on start: '" << detail << '\'';
        FatalError();
      }
    }
  }

  if (!(monitor_timer_fd_ < 0) && hang_monitor_) {
    setTimerInterval(monitor_timer_fd_, hang_monitor_->GetResetInterval());
  }

  setTimerInterval(ess_timer_fd_, kEssRunLoopPeriod);
  platform::PlatformInterface::get().resume();
}

void ApplicationRdk::OnTerminated() {
  Stop(0);
}

void ApplicationRdk::OnKeyPressed(unsigned int key) {
  input_handler_->OnKeyPressed(key);
}

void ApplicationRdk::OnKeyReleased(unsigned int key) {
  input_handler_->OnKeyReleased(key);
}

void ApplicationRdk::OnDisplaySize(int width, int height) {
  if (window_width_ == width && window_height_ == height) {
    resize_pending_ = false;
    return;
  }

  SB_DCHECK(native_window_ == 0);
  resize_pending_ = true;
}

void ApplicationRdk::DisplayInfoChanged() {
  if (state() != kStateStarted)
    return;

  SbWindowSize window_size;
  SbWindowGetSize(window_, &window_size);
  auto *data = new SbEventWindowSizeChangedData();
  data->size = window_size;
  data->window = window_;
  WindowSizeChanged(data, &ApplicationRdk::DeleteDestructor<SbEventWindowSizeChangedData>);
}

void ApplicationRdk::BuildEssosContext() {
  bool error = false;
  ctx_ = EssContextCreate();

  if ( !EssContextInit(ctx_) ) {
    error = true;
  }
  else if ( !EssContextSetTerminateListener(ctx_, this, &terminateListener) ) {
    error = true;
  }
  else if ( !EssContextSetKeyListener(ctx_, this, &keyListener) ) {
    error = true;
  }
  else if ( !EssContextSetSettingsListener(ctx_, this, &settingsListener) ) {
    error = true;
  }

  if ( error ) {
    const char *detail = EssContextGetLastErrorDetail(ctx_);
    SB_LOG(ERROR) << "Essos error: '" <<  detail << '\'';
    FatalError();
  }
}

void ApplicationRdk::FatalError() {
  SB_LOG(ERROR) << "Exiting due to fatal error.";
  ess_loop_last_ts_ = time_point<steady_clock>::max(); // Stop Essos run loop
  SbEventSchedule([](void* data) {
    ApplicationRdk::Get()->Stop(0);
  }, nullptr, 0);
}

void ApplicationRdk::ReleaseMemory() {
  Inject(new Event(kSbEventTypeLowMemory, NULL, [](void*) {
    malloc_trim(0);
  }));
}

void ApplicationRdk::ScheduleMemoryUsageCheck(int64_t delay) {
  SbEventSchedule([](void* data) {
    int64_t back_off_timeout = ApplicationRdk::Get()->CheckMemoryUsage();
    if (back_off_timeout && back_off_timeout != std::numeric_limits<int64_t>::max())
      ApplicationRdk::Get()->ScheduleMemoryUsageCheck(back_off_timeout);
  }, nullptr, delay);
}

int64_t ApplicationRdk::CheckMemoryUsage() {
  static const int64_t kCPUMemoryPressureLimit = ([]() -> int64_t {
    const char* env = std::getenv("COBALT_CPU_MEM_PRESSURE_IN_MB");
    // For C25, default memory pressure threshold was 400 MB, but has been
    // increased for C26 to provide more headroom.
    int64_t limit_in_mb = SB_INT64_C(500);
    if( env ) {
      int64_t t = strtol(env, nullptr, 0);
      if ( t >= 0 )
        limit_in_mb = t;
    }
    int64_t total_in_bytes = SbSystemGetTotalCPUMemory();
    return std::min(total_in_bytes, limit_in_mb * 1024 * 1024);
  })();

  if (!kCPUMemoryPressureLimit)
    return std::numeric_limits<int64_t>::max();

  int64_t usage_in_bytes = SbSystemGetUsedCPUMemory();
  if (kCPUMemoryPressureLimit < usage_in_bytes) {
    SB_LOG(INFO) << "Triggering memory pressure event. Current CPU mem. usage: "
                 << uint64_t(usage_in_bytes / 1024) << " kb, pressure limit: "
                 << uint64_t(kCPUMemoryPressureLimit / 1024) << " kb.";
    ReleaseMemory();
    return 5 * kSbTimeSecond;
  }

  return kSbTimeSecond;
}

void ApplicationRdk::InjectAccessibilitySettingsChanged() {
  // Event deprecated in Starboard 17+
}

void ApplicationRdk::InjectAccessibilityCaptionSettingsChanged() {
  // Event deprecated in Starboard 17+
}

void ApplicationRdk::InjectAccessibilityTextToSpeechSettingsChanged() {
  Inject(new Event(kSbEventTypeAccessibilityTextToSpeechSettingsChanged, NULL, NULL));
}

const char* ApplicationRdk::GetLocaleId() {
  std::lock_guard<std::mutex> lock(locale_mutex_);

  auto is_valid = [](const char* id) {
    return id != nullptr && id[0] != '\0' && strcmp(id, "C") != 0 &&
           strcmp(id, "POSIX") != 0;
  };

  auto normalize_locale = [](std::string& s) {
    // Handle inverted format: "US_en" or "US-en" -> "en_US"
    if (s.length() == 5 && (s[2] == '_' || s[2] == '-') &&
        std::isupper(static_cast<unsigned char>(s[0])) &&
        std::isupper(static_cast<unsigned char>(s[1])) &&
        std::islower(static_cast<unsigned char>(s[3])) &&
        std::islower(static_cast<unsigned char>(s[4]))) {
      s = s.substr(3, 2) + "_" + s.substr(0, 2);
      return;
    }
    // Handle standard BCP-47 "en-US" -> "en_US"
    std::replace(s.begin(), s.end(), '-', '_');
  };

  std::string new_locale;
#if defined(ENABLE_RDKSERVICES_API)
  std::string lang;
  if (UserPreferences::GetUILanguage(lang) && !lang.empty()) {
    normalize_locale(lang);
    if (is_valid(lang.c_str())) {
      new_locale = std::move(lang);
    }
  }
  if (new_locale.empty()) {
    if (UserSettings::GetPresentationLanguage(lang) && !lang.empty()) {
      normalize_locale(lang);
      if (is_valid(lang.c_str())) {
        new_locale = std::move(lang);
      }
    }
  }
#endif

  if (new_locale.empty()) {
    // Fallback to POSIX locale or environment variables.
    const char* posix_id = setlocale(LC_MESSAGES, NULL);
    if (!is_valid(posix_id)) {
      posix_id = getenv("LC_ALL");
      if (!is_valid(posix_id)) {
        posix_id = getenv("LC_MESSAGES");
        if (!is_valid(posix_id)) {
          posix_id = getenv("LANG");
        }
      }
    }

    if (is_valid(posix_id)) {
      new_locale = posix_id;
    } else {
      new_locale = "";
    }
  }

  // Store locales in a pool to guarantee pointer stability for the lifetime
  // of the application, as required by Starboard C API consumers holding
  // const char* references across dynamic runtime locale changes.
  return locale_pool_.insert(std::move(new_locale)).first->c_str();
}
}  // namespace starboard
