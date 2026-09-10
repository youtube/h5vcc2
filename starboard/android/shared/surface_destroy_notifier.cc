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

#include "starboard/android/shared/surface_destroy_notifier.h"

#include <chrono>
#include <utility>

#include "starboard/android/shared/video_window.h"
#include "starboard/common/log.h"
#include "starboard/shared/starboard/player/job_queue.h"

namespace starboard {

void SurfaceDestroyNotifier::Disconnect() {
  {
    std::lock_guard lock(mutex_);
    holder_ = nullptr;
    job_queue_ = nullptr;
    if (state_ != State::kExecuting) {
      state_ = State::kDone;
    }
  }
  cv_.notify_one();
}

void SurfaceDestroyNotifier::Notify() {
  std::unique_lock lock(mutex_);
  if (state_ != State::kIdle || !holder_ || !job_queue_) {
    return;
  }
  state_ = State::kWaiting;

  scoped_refptr<SurfaceDestroyNotifier> self(this);
  auto task = [self]() { self->NotifyDestroyed(); };
  if (!job_queue_->Schedule(std::move(task))) {
    SB_LOG(ERROR) << "Failed to schedule NotifyDestroyed on JobQueue.";
    state_ = State::kDone;
    return;
  }

  constexpr std::chrono::seconds kTeardownTimeout(1);
  if (!cv_.wait_for(lock, kTeardownTimeout,
                    [this] { return state_ == State::kDone; })) {
    SB_LOG(WARNING)
        << "SurfaceDestroyNotifier::Notify timed out waiting for teardown!";
  }
}

void SurfaceDestroyNotifier::NotifyDestroyed() {
  VideoSurfaceHolder* holder_to_notify = nullptr;
  {
    std::lock_guard lock(mutex_);
    if (state_ != State::kWaiting) {
      return;
    }
    state_ = State::kExecuting;
    holder_to_notify = holder_;
  }
  if (holder_to_notify) {
    holder_to_notify->OnSurfaceDestroyed();
  }

  {
    std::lock_guard lock(mutex_);
    state_ = State::kDone;
    holder_ = nullptr;
    job_queue_ = nullptr;
  }
  cv_.notify_one();
}

}  // namespace starboard
