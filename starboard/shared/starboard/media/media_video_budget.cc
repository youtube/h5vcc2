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

#include "starboard/shared/starboard/media/media_video_budget.h"

#include "build/build_config.h"
#include "starboard/shared/starboard/features.h"
#include "starboard/shared/starboard/media/resolutions.h"

namespace starboard {

namespace {
constexpr int kVideoBufferBudget1080p = 30 * 1024 * 1024;
constexpr int kVideoBufferBudget4KSdr = 100 * 1024 * 1024;
constexpr int kVideoBufferBudget4KHdr = 160 * 1024 * 1024;
// note - android has its own limit in:
// starboard/android/shared/media_get_video_buffer_budget.cc
constexpr int kVideoBufferBudgetAbove4K = 300 * 1024 * 1024;

int GetAreaBasedVideoBufferBudget(Size video_size, int bits_per_pixel) {
  if (video_size.IsEmpty() ||
      video_size.GetArea() <= Resolution::k1080p.GetArea()) {
    return kVideoBufferBudget1080p;
  }
  if (video_size.GetArea() <= Resolution::k4k.GetArea()) {
    if (bits_per_pixel <= 8) {
      return kVideoBufferBudget4KSdr;
    }
    return kVideoBufferBudget4KHdr;
  }
  return kVideoBufferBudgetAbove4K;
}

#if BUILDFLAG(IS_ANDROID)
int GetLegacyVideoBufferBudget(Size video_size, int bits_per_pixel) {
  if (video_size.FitsWithin(starboard::Resolution::k1080p) ||
      video_size.IsEmpty()) {
    return kVideoBufferBudget1080p;
  }
  if (video_size.FitsWithin(starboard::Resolution::k4k)) {
    if (bits_per_pixel <= 8) {
      return kVideoBufferBudget4KSdr;
    }
    return kVideoBufferBudget4KHdr;
  }
  return kVideoBufferBudgetAbove4K;
}
#endif

}  // namespace

int GetVideoBufferBudget(Size video_size, int bits_per_pixel) {
#if BUILDFLAG(IS_ANDROID)
#if SB_API_VERSION >= 17
  if (starboard::features::FeatureList::IsEnabled(
          starboard::features::kAreaBasedVideoBufferBudget)) {
    return GetAreaBasedVideoBufferBudget(video_size, bits_per_pixel);
  }
#endif
  return GetLegacyVideoBufferBudget(video_size, bits_per_pixel);
#else
  return GetAreaBasedVideoBufferBudget(video_size, bits_per_pixel);
#endif
}

}  // namespace starboard
