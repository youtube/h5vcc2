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

#ifndef STARBOARD_ANDROID_SHARED_FRAME_RATE_ESTIMATOR_H_
#define STARBOARD_ANDROID_SHARED_FRAME_RATE_ESTIMATOR_H_

#include <stdint.h>

#include <optional>

#include "starboard/shared/internal_only.h"

namespace starboard {

// Frame rate estimator for MediaCodec.
// This is a C++ port of the Java implementation in
// https://github.com/youtube/cobalt/blob/main/cobalt/android/apk/app/src/main/java/dev/cobalt/media/MediaCodecFrameRateEstimator.java
//
// Lifetime and Ownership:
// Typically owned as a member variable of a MediaCodec implementation (e.g.,
// NdkMediaCodec).
//
// Threading Model:
// This class is not thread-safe and is expected to be called on a single thread
// (Thread-affine).
class FrameRateEstimator {
 public:
  FrameRateEstimator();
  ~FrameRateEstimator() = default;

  // Resets the estimator state.
  void Reset();

  // Records a new output buffer presentation timestamp in microseconds.
  void OnNewOutput(int64_t presentation_time_us);

  // Returns the estimated frame rate in frames per second, or std::nullopt
  // if not enough frames have been recorded.
  std::optional<int> GetEstimatedFrameRate() const;

 private:
  std::optional<int64_t> last_frame_timestamp_us_;
  int64_t number_of_frames_ = 0;
  int64_t total_duration_us_ = 0;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_FRAME_RATE_ESTIMATOR_H_
