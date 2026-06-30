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

#include "starboard/media.h"
#include "starboard/shared/starboard/media/media_video_budget.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {
namespace {

// duplicated values from starboard/shared/starboard/media/media_video_budget.cc
constexpr int kVideoBufferBudget1080p = 30 * 1024 * 1024;
constexpr int kVideoBufferBudget4KSdr = 100 * 1024 * 1024;
constexpr int kVideoBufferBudget4KHdr = 160 * 1024 * 1024;
constexpr int kVideoBufferBudgetAbove4K = 300 * 1024 * 1024;

TEST(SbMediaGetVideoBufferBudgetTest, InvalidResolution) {
  // Invalid resolutions should fall back to 1080p budget.
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1,
                                        kSbMediaVideoResolutionDimensionInvalid,
                                        1080, 8),
            starboard::kVideoBufferBudget1080p);
  EXPECT_EQ(
      SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 1920,
                                  kSbMediaVideoResolutionDimensionInvalid, 8),
      starboard::kVideoBufferBudget1080p);
}

TEST(SbMediaGetVideoBufferBudgetTest, UnderOrEqual1080p) {
  // Standard 1080p
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 1920, 1080, 8),
            starboard::kVideoBufferBudget1080p);
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 1920, 1080, 10),
            starboard::kVideoBufferBudget1080p);

  // Lower resolution (720p)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 1280, 720, 8),
            starboard::kVideoBufferBudget1080p);
}

TEST(SbMediaGetVideoBufferBudgetTest, UnderOrEqual4K) {
  // Area between 1080p and 4K.
  // 1440p (2560x1440) -> area is 3,686,400 (> 2,073,600 and <= 8,294,400)
  // SDR (8 bits)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 2560, 1440, 8),
            starboard::kVideoBufferBudget4KSdr);
  // HDR (10 bits)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 2560, 1440, 10),
            starboard::kVideoBufferBudget4KHdr);

  // Standard 4K (3840x2160)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 3840, 2160, 8),
            starboard::kVideoBufferBudget4KSdr);
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 3840, 2160, 10),
            starboard::kVideoBufferBudget4KHdr);
}

TEST(SbMediaGetVideoBufferBudgetTest, Vertical1080pShorts) {
  // Vertical 1080p (Shorts) - area is same as 1080p, should be 1080p budget.
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 1080, 1920, 8),
            starboard::kVideoBufferBudget1080p);
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 1080, 1920, 10),
            starboard::kVideoBufferBudget1080p);
}

TEST(SbMediaGetVideoBufferBudgetTest, Vertical1440pShorts) {
  // Vertical 1440p (Shorts) -> area is same as 1440p, should be 4K budget.
  // SDR (8 bits)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 1440, 2560, 8),
            starboard::kVideoBufferBudget4KSdr);
  // HDR (10 bits)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 1440, 2560, 10),
            starboard::kVideoBufferBudget4KHdr);
}

TEST(SbMediaGetVideoBufferBudgetTest, HighResShorts) {
  // High-res square Shorts (2160x2160) -> area is 4,665,600.
  // This is > 1080p area and <= 4K area.
  // SDR (8 bits)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 2160, 2160, 8),
            starboard::kVideoBufferBudget4KSdr);
  // HDR (10 bits)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 2160, 2160, 10),
            starboard::kVideoBufferBudget4KHdr);

  // Vertical 4K Shorts (2160x3840) -> area is same as 4K, should be 4K budget.
  // SDR (8 bits)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 2160, 3840, 8),
            starboard::kVideoBufferBudget4KSdr);
  // HDR (10 bits)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 2160, 3840, 10),
            starboard::kVideoBufferBudget4KHdr);
}

TEST(SbMediaGetVideoBufferBudgetTest, Above4K) {
  // 8K (7680x4320)
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 7680, 4320, 8),
            starboard::kVideoBufferBudgetAbove4K);
  EXPECT_EQ(SbMediaGetVideoBufferBudget(kSbMediaVideoCodecAv1, 7680, 4320, 10),
            starboard::kVideoBufferBudgetAbove4K);
}

}  // namespace
}  // namespace starboard
