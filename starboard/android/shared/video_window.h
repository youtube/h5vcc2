// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_ANDROID_SHARED_VIDEO_WINDOW_H_
#define STARBOARD_ANDROID_SHARED_VIDEO_WINDOW_H_

#include <android/native_window.h>
#include <jni.h>

#include <condition_variable>
#include <mutex>

#include "starboard/common/ref_counted.h"
#include "starboard/decode_target.h"
#include "starboard/shared/starboard/player/job_queue.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {

class SurfaceDestroyNotifier;

class VideoSurfaceHolder {
 public:
  struct AcquiredSurface {
    scoped_refptr<SurfaceDestroyNotifier> destroy_notifier;
    jni_zero::ScopedJavaGlobalRef<jobject> surface;
  };

  // Return true only if the video surface is available.
  static bool IsVideoSurfaceAvailable();

  // OnSurfaceDestroyed() will be invoked when surface is destroyed. When this
  // function is called, the decoder no longer owns the surface. Calling
  // AcquireVideoSurface(), ReleaseVideoSurface(), GetVideoWindowSize() or
  // ClearVideoWindow() in this function may cause dead lock.
  virtual void OnSurfaceDestroyed() = 0;

  virtual ~VideoSurfaceHolder();

  // Returns the surface to which video should be rendered, along with an
  // optional SurfaceDestroyNotifier to safely handle surface destruction when
  // the feature is enabled.
  // The surface cannot be acquired if it's already held by another player.
  // |job_queue| is used by SurfaceDestroyNotifier to schedule the teardown
  // task on the player worker thread when the surface is destroyed.
  AcquiredSurface AcquireVideoSurface(JobQueue* job_queue);

  // Release the surface to make the surface available for other holder.
  void ReleaseVideoSurface();

  // Cleans up the video surface, and posts the task to |gpu_provider|.
  void CleanUpVideoSurface(SbDecodeTargetGraphicsContextProvider* gpu_provider);

  // Reset the video surface by re-creating video surface.
  void ResetVideoSurface();

 private:
  AcquiredSurface AcquireVideoSurfaceUsingSurfaceDestroyer(JobQueue* job_queue);
  void ReleaseVideoSurfaceUsingSurfaceDestroyer();

  scoped_refptr<SurfaceDestroyNotifier> active_notifier_;
};

// SurfaceDestroyNotifier is used to safely handle the destruction of the video
// surface from JNI without causing deadlocks.
//
// Purpose: Decouples the JNI thread (which notifies about surface destruction)
// from the player worker thread (which performs the teardown). It schedules
// the teardown task on the player worker thread and waits for it with a timeout
// to avoid hanging the JNI thread.
//
// Lifetime/Ownership: This class is RefCountedThreadSafe. It is created by
// VideoSurfaceHolder when acquiring the surface, and references are held by
// the global `GetGlobalSurfaceDestroyNotifier()` and the
// `MediaCodecVideoDecoder`. The task scheduled on the JobQueue also retains a
// reference to ensure the object stays alive until execution completes.
//
// Threading Model: `Notify()` is called from the JNI thread.
// `NotifyDestroyed()` executes on the player worker thread. Internal state is
// protected by its own mutex.
class SurfaceDestroyNotifier
    : public RefCountedThreadSafe<SurfaceDestroyNotifier> {
 public:
  SurfaceDestroyNotifier(VideoSurfaceHolder* holder, JobQueue* job_queue)
      : holder_(holder), job_queue_(job_queue) {}

  void Disconnect();
  void Notify();

  bool IsCurrentHolder(VideoSurfaceHolder* holder) const {
    std::lock_guard lock(mutex_);
    return holder_ == holder;
  }

 private:
  ~SurfaceDestroyNotifier() = default;
  friend class RefCountedThreadSafe<SurfaceDestroyNotifier>;

  void NotifyDestroyed();

  mutable std::mutex mutex_;
  std::condition_variable done_cv_;
  bool done_ = false;                 // Guarded by |mutex_|
  bool disconnected_ = false;         // Guarded by |mutex_|
  bool in_notify_destroyed_ = false;  // Guarded by |mutex_|
  VideoSurfaceHolder* holder_;        // Guarded by |mutex_|
  JobQueue* job_queue_;               // Guarded by |mutex_|
};

// Set the global video surface. Exposed for unit testing.
void SetVideoSurfaceForTesting(JNIEnv* env,
                               const jni_zero::JavaRef<jobject>& surface);

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_VIDEO_WINDOW_H_
