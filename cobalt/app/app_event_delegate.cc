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

#include "cobalt/app/app_event_delegate.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/run_loop.h"
#include "base/threading/platform_thread.h"
#include "cobalt/app/app_event_runner.h"
#include "cobalt/browser/lifecycle/cobalt_lifecycle_manager.h"
#include "content/public/browser/browser_thread.h"
#include "starboard/extension/crash_handler.h"
#include "starboard/system.h"

namespace cobalt {

namespace {

AppEventDelegate::ApplicationState SbEventToTargetApplicationState(
    SbEventType type) {
  switch (type) {
    case kSbEventTypeStart:
      return AppEventDelegate::ApplicationState::kStarted;
    case kSbEventTypePreload:
      return AppEventDelegate::ApplicationState::kConcealed;
    case kSbEventTypeBlur:
      return AppEventDelegate::ApplicationState::kBlurred;
    case kSbEventTypeFocus:
      return AppEventDelegate::ApplicationState::kStarted;
    case kSbEventTypeConceal:
      return AppEventDelegate::ApplicationState::kConcealed;
    case kSbEventTypeReveal:
      return AppEventDelegate::ApplicationState::kBlurred;
    case kSbEventTypeFreeze:
      return AppEventDelegate::ApplicationState::kFrozen;
    case kSbEventTypeUnfreeze:
      return AppEventDelegate::ApplicationState::kConcealed;
    case kSbEventTypeStop:
      return AppEventDelegate::ApplicationState::kStopped;
    default:
      // This part of the code should only be reached if an unknown SbEventType
      // is received.
      NOTREACHED() << "Unexpected SbEventType: " << type;
  }
}
}  // namespace

AppEventDelegate::AppEventDelegate(
    std::unique_ptr<AppEventRunner> runner,
    const CobaltExtensionCrashHandlerApi* crash_handler_extension)
    : runner_(std::move(runner)),
      crash_handler_extension_(crash_handler_extension) {
  base::AutoLock lock(lock_);
  if (!crash_handler_extension_) {
    // If a special extension implementation wasn't provided, use the default.
    crash_handler_extension_ =
        static_cast<const CobaltExtensionCrashHandlerApi*>(
            SbSystemGetExtension(kCobaltExtensionCrashHandlerName));
  }

  application_state_ = ApplicationState::kInitial;
  SetApplicationStateAnnotation(ApplicationState::kInitial);
  target_state_ = ApplicationState::kInitial;
  if (!runner_) {
    // If a special runner wasn't provided, use the default.
    runner_ = AppEventRunner::Create();
  }
}

AppEventDelegate::~AppEventDelegate() = default;

bool AppEventDelegate::IsRunning() const {
  base::AutoLock lock(lock_);
  return IsRunningLocked();
}

bool AppEventDelegate::IsRunningLocked() const {
  return application_state_ != ApplicationState::kInitial &&
         application_state_ != ApplicationState::kStopped;
}

bool AppEventDelegate::IsVisible() const {
  base::AutoLock lock(lock_);
  return IsVisibleLocked();
}

bool AppEventDelegate::IsVisibleLocked() const {
  return application_state_ == ApplicationState::kBlurred ||
         application_state_ == ApplicationState::kStarted;
}

bool AppEventDelegate::IsFocused() const {
  base::AutoLock lock(lock_);
  return IsFocusedLocked();
}

bool AppEventDelegate::IsFocusedLocked() const {
  return application_state_ == ApplicationState::kStarted;
}

bool AppEventDelegate::IsFrozen() const {
  base::AutoLock lock(lock_);
  return IsFrozenLocked();
}

AppEventDelegate::ApplicationState AppEventDelegate::GetState() const {
  base::AutoLock lock(lock_);
  return application_state_;
}

PendingAck AppEventDelegate::pending_ack() const {
  base::AutoLock lock(lock_);
  return runner_ ? runner_->pending_ack() : PendingAck::kNone;
}

void AppEventDelegate::SetQuitClosure(base::OnceClosure closure) {
  base::AutoLock lock(lock_);
  quit_closure_ = std::move(closure);
}

bool AppEventDelegate::IsFrozenLocked() const {
  return application_state_ == ApplicationState::kFrozen ||
         application_state_ == ApplicationState::kStopped ||
         application_state_ == ApplicationState::kInitial;
}

void AppEventDelegate::HandleEvent(const SbEvent* event) {
  // Use a lock to ensure thread safety as HandleEvent might be called from
  // different threads (e.g., Starboard thread, UI thread).
  base::AutoLock lock(lock_);
  HandleEventLocked(event);
}

void AppEventDelegate::HandleEventLocked(const SbEvent* event) {
  // Drop events received after the application stops.
  if (application_state_ == ApplicationState::kStopped ||
      target_state_ == ApplicationState::kStopped) {
    LOG(WARNING) << "Received event " << event->type
                 << " after stopping. Event is ignored.";
    return;
  }

  // Handle the first received event.
  if (application_state_ == ApplicationState::kInitial) {
    switch (event->type) {
      case kSbEventTypeStart:
      case kSbEventTypePreload:
        runner_->OnStart(event);
        SetApplicationState(SbEventToTargetApplicationState(event->type));
        target_state_ = application_state_;
        return;
      default:
        // Robustly handle events received before the application has started or
        // preloaded. This ensures that any early events are preceded by a
        // valid initial state transition.
        LOG(WARNING) << "Received event " << event->type
                     << " before start or preload. "
                     << "An implicit preload event was inserted.";
        SbEvent preload_event = {kSbEventTypePreload, event->timestamp,
                                 nullptr};
        HandleEventLocked(&preload_event);
        // Break here instead of return to handle the received event.
        break;
    }
  }

  // We're not in initial state anymore.
  CHECK_NE(application_state_, ApplicationState::kInitial);
  switch (event->type) {
    case kSbEventTypePreload:
    case kSbEventTypeStart: {
      // Redundant start/preload events are ignored, unless they contain a link.
      // If they contain a link, they are treated as deep links. This avoids
      // undefined behavior by providing defined transitions that maintain the
      // lifecycle contract.
      const SbEventStartData* data =
          static_cast<const SbEventStartData*>(event->data);
      if (data && data->link && data->link[0] != '\0') {
        LOG(WARNING) << "Received redundant "
                     << (event->type == kSbEventTypeStart ? "start" : "preload")
                     << " event with link after initialization. Treating as a "
                        "link event.";
        SbEvent link_event = {
            kSbEventTypeLink, event->timestamp,
            const_cast<void*>(static_cast<const void*>(data->link))};
        HandleEventLocked(&link_event);
      }
      if (event->type == kSbEventTypeStart) {
        // Redundant start events are treated as focus events.
        TransitionToLifeCycleState(ApplicationState::kStarted);
      }
      return;
    }
    case kSbEventTypeBlur:
    case kSbEventTypeFocus:
    case kSbEventTypeConceal:
    case kSbEventTypeStop:
    case kSbEventTypeReveal:
    case kSbEventTypeFreeze:
    case kSbEventTypeUnfreeze:
      // Ensure all intermediate state changes are triggered.
      TransitionToLifeCycleState(SbEventToTargetApplicationState(event->type));
      break;
    case kSbEventTypeInput:
      runner_->OnInput(event);
      break;
    case kSbEventTypeLink:
      runner_->OnLink(event);
      break;
    case kSbEventTypeLowMemory:
      runner_->OnLowMemory(event);
      break;
    case kSbEventTypeWindowSizeChanged:
      runner_->OnWindowSizeChanged(event);
      break;
    case kSbEventTypeOsNetworkConnected:
    case kSbEventTypeOsNetworkDisconnected:
      runner_->OnOsNetworkConnectedDisconnected(event);
      break;
    case kSbEventDateTimeConfigurationChanged:
      runner_->OnDateTimeConfigurationChanged(event);
      break;
    case kSbEventTypeAccessibilityTextToSpeechSettingsChanged:
      runner_->OnAccessibilityTextToSpeechSettingsChanged(event);
      break;
    default:
      break;
  }
}

void AppEventDelegate::ExecuteNextStep() {
  if (application_state_ == target_state_) {
    is_transitioning_ = false;
    if (quit_closure_) {
      std::move(quit_closure_).Run();
    }
    return;
  }

  bool is_activating = target_state_ < application_state_;
  ApplicationState next_state = GetNextState(application_state_, is_activating);

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&AppEventDelegate::ExecuteStepOnUIThread,
                     base::Unretained(this), next_state, is_activating));
}

AppEventDelegate::ApplicationState AppEventDelegate::GetNextState(
    ApplicationState current_state,
    bool is_activating) const {
  if (is_activating) {
    switch (current_state) {
      case ApplicationState::kFrozen:
        return ApplicationState::kConcealed;
      case ApplicationState::kConcealed:
        return ApplicationState::kBlurred;
      case ApplicationState::kBlurred:
        return ApplicationState::kStarted;
      default:
        NOTREACHED();
    }
  } else {
    switch (current_state) {
      case ApplicationState::kStarted:
        return ApplicationState::kBlurred;
      case ApplicationState::kBlurred:
        return ApplicationState::kConcealed;
      case ApplicationState::kConcealed:
        return ApplicationState::kFrozen;
      case ApplicationState::kFrozen:
        return ApplicationState::kStopped;
      default:
        NOTREACHED();
    }
  }
}

void AppEventDelegate::ExecuteEventRunner(ApplicationState next_state,
                                          bool is_activating) {
  if (is_activating) {
    switch (next_state) {
      case ApplicationState::kConcealed:
        runner_->OnUnfreeze();
        break;
      case ApplicationState::kBlurred:
        runner_->OnReveal();
        break;
      case ApplicationState::kStarted:
        runner_->OnFocus();
        break;
      default:
        NOTREACHED();
    }
  } else {
    switch (next_state) {
      case ApplicationState::kBlurred:
        runner_->OnBlur();
        break;
      case ApplicationState::kConcealed:
        runner_->OnConceal();
        break;
      case ApplicationState::kFrozen:
        runner_->OnFreeze(base::DoNothing());
        break;
      case ApplicationState::kStopped:
        break;
      default:
        NOTREACHED();
    }
  }
}

void AppEventDelegate::ExecuteStepOnUIThread(ApplicationState next_state,
                                             bool is_activating) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  {
    base::AutoLock lock(lock_);
    if (is_tearing_down_) {
      LOG(INFO) << "Ignoring ExecuteStepOnUIThread during teardown.";
      return;
    }
  }
  // Note: ExecuteEventRunner does not change state nor rely on state of this
  // object, and always runs as a posted task on the UI thread, and therefore
  // the lock does not need to be held yet here.
  ExecuteEventRunner(next_state, is_activating);

  base::OnceClosure quit_closure;
  bool should_execute_next_step = false;
  {
    base::AutoLock lock(lock_);
    SetApplicationState(next_state);
    if (next_state == ApplicationState::kStopped) {
      quit_closure = std::move(quit_closure_);
    } else {
      should_execute_next_step = true;
    }
  }

  if (quit_closure) {
    std::move(quit_closure).Run();
  }
  if (should_execute_next_step) {
    base::AutoLock lock(lock_);
    ExecuteNextStep();
  }
}

void AppEventDelegate::DoTeardown() {
  {
    base::AutoLock lock(lock_);
    if (is_tearing_down_) {
      return;
    }
    is_tearing_down_ = true;
  }
  runner_->OnStop();
  base::AutoLock lock(lock_);
  SetApplicationState(ApplicationState::kStopped);
}

void AppEventDelegate::TransitionToLifeCycleState(ApplicationState state) {
  // TransitionToLifeCycleState ensures that the application moves from its
  // current state to the target |state| by traversing all intermediate states
  // in strict linear order. Each state transition triggers its corresponding
  // side effects via the runner. This logic guarantees that no lifecycle events
  // are skipped and that side effects are executed in a consistent, predictable
  // sequence, regardless of whether the system events were received out of
  // order, duplicated, or missing.
  CHECK_GT(state, ApplicationState::kInitial);
  CHECK_LE(state, ApplicationState::kStopped);

  LOG(INFO) << "AppEventDelegate::TransitionToLifeCycleState called, current="
            << static_cast<int>(application_state_) << " ("
            << GetStateString(application_state_)
            << ") target=" << static_cast<int>(state) << " ("
            << GetStateString(state) << ")";

  target_state_ = state;

  if (is_transitioning_) {
    LOG(INFO) << "Transition already in progress. Updated target_state_ to "
              << static_cast<int>(state) << " (" << GetStateString(state)
              << ")";
    return;
  } else {
    is_transitioning_ = true;
    ExecuteNextStep();
  }
}

// static
const char* AppEventDelegate::GetStateString(ApplicationState state) {
  switch (state) {
    case ApplicationState::kInitial:
      return "kInitial";
    case ApplicationState::kStarted:
      return "kStarted";
    case ApplicationState::kBlurred:
      return "kBlurred";
    case ApplicationState::kConcealed:
      return "kConcealed";
    case ApplicationState::kFrozen:
      return "kFrozen";
    case ApplicationState::kStopped:
      return "kStopped";
    default:
      NOTREACHED();
  }
}

void AppEventDelegate::SetApplicationState(ApplicationState state) {
  if (application_state_ == state) {
    return;
  }
  application_state_ = state;
  SetApplicationStateAnnotation(state);
}

void AppEventDelegate::SetApplicationStateAnnotation(ApplicationState state) {
  if (!crash_handler_extension_ || crash_handler_extension_->version < 2) {
    LOG(WARNING)
        << "Crash handler extension not implemented or version too low.";
    return;
  }

  if (!crash_handler_extension_->SetString("application_state",
                                           GetStateString(state))) {
    LOG(WARNING) << "Failed to set application_state annotation.";
  }
}

}  // namespace cobalt

#if BUILDFLAG(IS_ANDROID)
#include "cobalt/android/browser_jni_headers/AppEventBridge_jni.h"
#include "starboard/event.h"

static void JNI_AppEventBridge_HandleLifecycleEvent(JNIEnv* env, jint type) {
  SbEvent event;
  event.type = static_cast<SbEventType>(type);
  event.timestamp = 0;
  event.data = nullptr;

  static cobalt::AppEventDelegate* s_lifecycle_delegate =
      new cobalt::AppEventDelegate();

  if (!s_lifecycle_delegate) {
    return;
  }

  if (event.type == kSbEventTypeStop) {
    s_lifecycle_delegate->HandleEvent(&event);
    s_lifecycle_delegate->DoTeardown();
    delete s_lifecycle_delegate;
    s_lifecycle_delegate = nullptr;
  } else {
    s_lifecycle_delegate->HandleEvent(&event);
  }
}
#endif  // BUILDFLAG(IS_ANDROID)
