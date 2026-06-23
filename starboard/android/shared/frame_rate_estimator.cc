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

#include "starboard/android/shared/frame_rate_estimator.h"

#include <cmath>

#include "starboard/common/log.h"

namespace starboard {
namespace {
// Matches MINIMUM_REQUIRED_FRAMES in Java OutputFrameRateEstimator.
// https://github.com/youtube/cobalt/blob/73f08b6187bed6a30a8b5753fcd27aa69fafdc8b/cobalt/android/apk/app/src/main/java/dev/cobalt/media/MediaCodecFrameRateEstimator.java#L44
constexpr int kMinRequiredFrames = 4;
}  // namespace

FrameRateEstimator::FrameRateEstimator() {
  Reset();
}

void FrameRateEstimator::Reset() {
  last_frame_timestamp_us_ = std::nullopt;
  number_of_frames_ = 0;
  total_duration_us_ = 0;
}

void FrameRateEstimator::OnNewOutput(int64_t presentation_time_us) {
  if (!last_frame_timestamp_us_.has_value()) {
    last_frame_timestamp_us_ = presentation_time_us;
    number_of_frames_ = 1;
    return;
  }
  if (presentation_time_us <= last_frame_timestamp_us_.value()) {
    SB_LOG(WARNING) << "Invalid output presentation timestamp: "
                    << presentation_time_us
                    << " <= " << last_frame_timestamp_us_.value();
    return;
  }

  number_of_frames_++;
  total_duration_us_ += presentation_time_us - last_frame_timestamp_us_.value();
  last_frame_timestamp_us_ = presentation_time_us;
}

std::optional<int> FrameRateEstimator::GetEstimatedFrameRate() const {
  if (number_of_frames_ < kMinRequiredFrames || total_duration_us_ <= 0) {
    return std::nullopt;
  }
  return std::round((number_of_frames_ - 1) * 1000000.0 / total_duration_us_);
}

}  // namespace starboard
