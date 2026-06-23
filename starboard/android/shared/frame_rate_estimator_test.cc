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

#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {
namespace {

TEST(FrameRateEstimatorTest, InitialState) {
  FrameRateEstimator estimator;
  EXPECT_FALSE(estimator.GetEstimatedFrameRate().has_value());
}

TEST(FrameRateEstimatorTest, NotEnoughFrames) {
  FrameRateEstimator estimator;

  // 1 frame
  estimator.OnNewOutput(0);
  EXPECT_FALSE(estimator.GetEstimatedFrameRate().has_value());

  // 2 frames
  estimator.OnNewOutput(33'333);
  EXPECT_FALSE(estimator.GetEstimatedFrameRate().has_value());

  // 3 frames
  estimator.OnNewOutput(66'666);
  EXPECT_FALSE(estimator.GetEstimatedFrameRate().has_value());
}

TEST(FrameRateEstimatorTest, ValidEstimate30Fps) {
  FrameRateEstimator estimator;

  // 4 frames with ~33.3ms intervals (30 fps)
  estimator.OnNewOutput(0);
  estimator.OnNewOutput(33'333);
  estimator.OnNewOutput(66'666);
  estimator.OnNewOutput(100'000);

  auto estimate = estimator.GetEstimatedFrameRate();
  ASSERT_TRUE(estimate.has_value());
  EXPECT_EQ(estimate.value(), 30);
}

TEST(FrameRateEstimatorTest, ValidEstimate60Fps) {
  FrameRateEstimator estimator;

  // 4 frames with ~16.6ms intervals (60 fps)
  estimator.OnNewOutput(0);
  estimator.OnNewOutput(16'666);
  estimator.OnNewOutput(33'333);
  estimator.OnNewOutput(50'000);

  auto estimate = estimator.GetEstimatedFrameRate();
  ASSERT_TRUE(estimate.has_value());
  EXPECT_EQ(estimate.value(), 60);
}

TEST(FrameRateEstimatorTest, ResetClearsState) {
  FrameRateEstimator estimator;

  estimator.OnNewOutput(0);
  estimator.OnNewOutput(33'333);
  estimator.OnNewOutput(66'666);
  estimator.OnNewOutput(100'000);

  EXPECT_TRUE(estimator.GetEstimatedFrameRate().has_value());

  estimator.Reset();
  EXPECT_FALSE(estimator.GetEstimatedFrameRate().has_value());

  // Needs 4 new frames to get estimate again
  estimator.OnNewOutput(0);
  estimator.OnNewOutput(16'666);
  estimator.OnNewOutput(33'333);
  EXPECT_FALSE(estimator.GetEstimatedFrameRate().has_value());

  estimator.OnNewOutput(50'000);
  auto estimate = estimator.GetEstimatedFrameRate();
  ASSERT_TRUE(estimate.has_value());
  EXPECT_EQ(estimate.value(), 60);
}

TEST(FrameRateEstimatorTest, IgnoreInvalidTimestamps) {
  FrameRateEstimator estimator;

  estimator.OnNewOutput(0);
  estimator.OnNewOutput(33'333);

  // Invalid timestamp (backward in time)
  estimator.OnNewOutput(20'000);

  estimator.OnNewOutput(66'666);

  // We have sent 4 frames, but one was invalid.
  // If the invalid one is correctly ignored, we only have 3 valid frames,
  // so we should not have an estimate yet.
  EXPECT_FALSE(estimator.GetEstimatedFrameRate().has_value());

  // Send 4th valid frame
  estimator.OnNewOutput(100'000);

  auto estimate = estimator.GetEstimatedFrameRate();
  ASSERT_TRUE(estimate.has_value());
  EXPECT_EQ(estimate.value(), 30);
}

}  // namespace
}  // namespace starboard
