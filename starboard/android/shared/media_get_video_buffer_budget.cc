// Copyright 2022 The Cobalt Authors. All Rights Reserved.
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

#include <algorithm>

#include "starboard/android/shared/runtime_resource_overlay.h"
#include "starboard/common/log.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/media_video_budget.h"

// This is different than max budget defined in
// starboard/shared/starboard/media/media_video_budget.cc
// We set the maximum memory budget to 200MB on Android (and 300MB on other
// platforms), balancing the following factors:
//
// 1. 8K Video Playback
// Playing 8K video requires significant memory for decoded frames. For
// instance, a specific device needs to hold up to 6 frames inside its decoder,
// which can take almost 300MB. See b/405467220#comment46 for details. Because
// of this large requirement for decoded frames, we can't allocate too much
// budget for encoded frames.
//
// 2. Chromium's Budget
// Chromium has a max memory budget of 150MB.
// https://github.com/youtube/cobalt/blob/a3c966f929aabea1d71813c31d404e1b319c2fcd/media/base/demuxer_memory_limit.h#L44
constexpr int kMaxVideoBufferBudgetForAndroid = 200 * 1024 * 1024;

int SbMediaGetVideoBufferBudget(SbMediaVideoCodec codec,
                                int resolution_width,
                                int resolution_height,
                                int bits_per_pixel) {
  auto get_overlaid_video_buffer_budget = []() {
    int buffer_budget = starboard::RuntimeResourceOverlay::GetInstance()
                            ->max_video_buffer_budget();
    if (buffer_budget == 0) {
      return kMaxVideoBufferBudgetForAndroid;
    }
    SB_LOG(INFO) << "RRO \"max_video_buffer_budget\" is set to "
                 << buffer_budget << " MB.";
    return buffer_budget * 1024 * 1024;
  };

  static const int overlaid_video_buffer_budget =
      get_overlaid_video_buffer_budget();

  int video_buffer_budget = starboard::GetVideoBufferBudget(
      {resolution_width, resolution_height}, bits_per_pixel);

  return std::min(video_buffer_budget, overlaid_video_buffer_budget);
}
