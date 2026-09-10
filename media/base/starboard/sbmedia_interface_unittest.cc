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

#include "media/base/starboard/sbmedia_interface.h"

#include <limits>

#include "base/test/scoped_feature_list.h"
#include "media/base/media_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

TEST(SbMediaInterfaceTest, ExtractMimeIntParam) {
  const char kMime720p[] =
      "video/mp4; codecs=\"avc1.4d401f\"; width=1280; height=720; "
      "framerate=30; bitrate=2496430";
  EXPECT_EQ(ExtractMimeIntParam(kMime720p, "width"), 1280);
  EXPECT_EQ(ExtractMimeIntParam(kMime720p, "height"), 720);
  EXPECT_EQ(ExtractMimeIntParam(kMime720p, "framerate"), 30);
  EXPECT_EQ(ExtractMimeIntParam(kMime720p, "bitrate"), 2496430);
  EXPECT_EQ(ExtractMimeIntParam(kMime720p, "channels"), 0);

  // Quoted integer values and whitespace around '='.
  EXPECT_EQ(ExtractMimeIntParam("video/mp4; width = \"1920\" ; height = 1080",
                                "width"),
            1920);
  EXPECT_EQ(ExtractMimeIntParam("video/mp4; width = \"1920\" ; height = 1080",
                                "height"),
            1080);

  // Should not match substrings like "max_height" when searching for "height".
  EXPECT_EQ(
      ExtractMimeIntParam("video/mp4; max_height=2160; height=720", "height"),
      720);
  EXPECT_EQ(ExtractMimeIntParam("video/mp4; max_height=2160", "height"), 0);

  // Integer overflow clamps to INT_MAX.
  EXPECT_EQ(
      ExtractMimeIntParam("video/mp4; width=999999999999999999999", "width"),
      std::numeric_limits<int>::max());
}

TEST(SbMediaInterfaceTest, Exceeds720p) {
  EXPECT_FALSE(Exceeds720p(nullptr));
  EXPECT_FALSE(Exceeds720p(""));

  // Audio queries (no width/height)
  EXPECT_FALSE(Exceeds720p("audio/mp4; codecs=\"mp4a.40.2\"; channels=2"));
  EXPECT_FALSE(Exceeds720p("audio/webm; codecs=\"opus\"; channels=2"));
  EXPECT_FALSE(Exceeds720p("audio/mp4; codecs=\"ec-3\"; channels=6"));
  EXPECT_FALSE(Exceeds720p("audio/mp4; codecs=\"ac-3\"; channels=6"));
  EXPECT_FALSE(Exceeds720p("audio/mp4; codecs=\"iamf.001.001.Opus\""));

  // <= 720p video queries
  EXPECT_FALSE(Exceeds720p(
      "video/webm; codecs=\"vp09.00.51.08.01.01.01.01.00\"; width=640; "
      "height=360; framerate=30; bitrate=280000"));
  EXPECT_FALSE(
      Exceeds720p("video/mp4; codecs=\"av01.0.04M.08\"; width=854; height=480; "
                  "framerate=30; bitrate=787121; eotf=bt709"));
  EXPECT_FALSE(Exceeds720p(
      "video/webm; codecs=\"vp09.00.51.08.01.01.01.01.00\"; width=1280; "
      "height=720; framerate=30; bitrate=1862827; eotf=bt709"));
  EXPECT_FALSE(
      Exceeds720p("video/mp4; codecs=\"avc1.4d401f\"; width=1280; height=720; "
                  "framerate=30; bitrate=2496430"));

  // > 720p video queries
  EXPECT_TRUE(
      Exceeds720p("video/mp4; codecs=\"avc1.64002a\"; width=1920; height=1080; "
                  "framerate=60; bitrate=6933529"));
  EXPECT_TRUE(Exceeds720p(
      "video/mp4; codecs=\"av01.0.13M.08\"; width=3840; height=2160; "
      "framerate=60; bitrate=17443607; eotf=bt709"));
  EXPECT_TRUE(Exceeds720p(
      "video/webm; codecs=\"vp09.00.51.08.01.01.01.01.00\"; width=3840; "
      "height=2160; framerate=60; bitrate=26523399; eotf=bt709"));

  // Boundary checks (721p height or 1281 width)
  EXPECT_TRUE(
      Exceeds720p("video/mp4; codecs=\"avc1.640028\"; width=1280; height=721; "
                  "framerate=30"));
  EXPECT_TRUE(
      Exceeds720p("video/mp4; codecs=\"avc1.640028\"; width=1281; height=720; "
                  "framerate=30"));
}

TEST(SbMediaInterfaceTest, CapResolutionTo720pFeature) {
  DefaultSbMediaInterface sb_media;

  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(kCobaltCapResolutionTo720p);

  // > 720p formats must be rejected with kSbMediaSupportTypeNotSupported
  EXPECT_EQ(sb_media.CanPlayMimeAndKeySystem(
                "video/mp4; codecs=\"avc1.64002a\"; width=1920; height=1080; "
                "framerate=60; bitrate=6933529",
                ""),
            kSbMediaSupportTypeNotSupported);
  EXPECT_EQ(sb_media.CanPlayMimeAndKeySystem(
                "video/mp4; codecs=\"av01.0.13M.08\"; width=3840; height=2160; "
                "framerate=60; bitrate=17443607; eotf=bt709",
                ""),
            kSbMediaSupportTypeNotSupported);
  EXPECT_EQ(
      sb_media.CanPlayMimeAndKeySystem(
          "video/webm; codecs=\"vp09.00.51.08.01.01.01.01.00\"; width=3840; "
          "height=2160; framerate=60; bitrate=26523399; eotf=bt709",
          ""),
      kSbMediaSupportTypeNotSupported);
}

}  // namespace media
