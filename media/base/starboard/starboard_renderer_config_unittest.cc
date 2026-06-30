// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

#include "media/base/starboard/starboard_renderer_config.h"

#include <string>

#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

TEST(StarboardRendererConfigTest, SunnyDay) {
  constexpr base::TimeDelta audio_write_duration_local =
      base::Microseconds(500000);
  constexpr base::TimeDelta audio_write_duration_remote =
      base::Microseconds(100000);
  const std::string max_video_capabilities =
      "width=1920; height=1080; framerate=15;";
  ExperimentalFeatures::Map map;
  map["Media.ForceDecodeToTexture"] = 1;
  map["Media.BypassMojoForMedia"] = 0;
  StarboardRendererConfig::ExperimentalFeatures experimental_features(map);

  StarboardRendererConfig config(
      base::UnguessableToken::Create(), audio_write_duration_local,
      audio_write_duration_remote, max_video_capabilities,
      experimental_features, gfx::Size(1920, 1080));
  EXPECT_EQ(config.audio_write_duration_local, audio_write_duration_local);
  EXPECT_EQ(config.audio_write_duration_remote, audio_write_duration_remote);
  EXPECT_EQ(config.max_video_capabilities, max_video_capabilities);
  EXPECT_TRUE(config.experimental_features.GetBool(kMediaForceDecodeToTexture));
  EXPECT_FALSE(config.experimental_features.GetBool(kMediaBypassMojoForMedia));
  EXPECT_EQ(config.experimental_features.Get(kMediaForceDecodeToTexture),
            std::optional<bool>(true));
  EXPECT_EQ(config.experimental_features.Get(kMediaBypassMojoForMedia),
            std::optional<bool>(false));
  EXPECT_EQ(config.viewport_size, gfx::Size(1920, 1080));
}

}  // namespace media
