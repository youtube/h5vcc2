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

#ifndef STARBOARD_ANDROID_SHARED_SURFACE_DESTROY_NOTIFIER_H_
#define STARBOARD_ANDROID_SHARED_SURFACE_DESTROY_NOTIFIER_H_

#include <condition_variable>
#include <mutex>

#include "starboard/common/ref_counted.h"

namespace starboard {

class VideoSurfaceHolder;
class JobQueue;

// This class coordinates the destruction of the global video surface between
// the Android JNI thread and the player decoder thread. It allows the JNI
// thread to trigger surface destruction and wait for the teardown to complete
// without holding the global view surface mutex.
//
// Thread safety & Lifetime:
// - RefCountedThreadSafe with internal mutex guarding state transitions.
// - Created in VideoSurfaceHolder::AcquireVideoSurface(), where references are
//   stored globally in GetGlobalSurfaceDestroyNotifier() and locally in
//   the decoder (VideoSurfaceHolder::active_notifier_ /
//   MediaCodecVideoDecoder).
// - When Notify() is invoked from the JNI thread, an in-flight task on the
//   JobQueue also holds a reference to ensure the object stays alive until
//   NotifyDestroyed() completes on the decoder thread.
class SurfaceDestroyNotifier
    : public RefCountedThreadSafe<SurfaceDestroyNotifier> {
 public:
  SurfaceDestroyNotifier(VideoSurfaceHolder* holder, JobQueue* job_queue)
      : holder_(holder), job_queue_(job_queue) {}

  void Disconnect();
  void Notify();

  bool IsCurrentHolder(VideoSurfaceHolder* holder) {
    std::lock_guard lock(mutex_);
    return holder_ == holder;
  }

 private:
  ~SurfaceDestroyNotifier() = default;
  friend class RefCountedThreadSafe<SurfaceDestroyNotifier>;

  void NotifyDestroyed();

  enum class State {
    kIdle,       // initial state
    kWaiting,    // JNI thread waiting  on cv_
    kExecuting,  // NotifyDestroyed running on player
    kDone,       // completed or disconnected
  };

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  State state_ = State::kIdle;
  VideoSurfaceHolder* holder_ = nullptr;
  JobQueue* job_queue_ = nullptr;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_SURFACE_DESTROY_NOTIFIER_H_
