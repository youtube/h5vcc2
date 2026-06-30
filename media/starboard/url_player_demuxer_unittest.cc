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

#include "media/starboard/url_player_demuxer.h"

#include "base/test/task_environment.h"
#include "media/base/demuxer_stream.h"
#include "media/base/mock_demuxer_host.h"
#include "media/base/pipeline_status.h"
#include "media/base/test_helpers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace media {
namespace {

// --- IsHlsUrl tests ---

TEST(IsHlsUrlTest, DetectsM3u8Extension) {
  EXPECT_TRUE(IsHlsUrl(GURL("https://example.com/video.m3u8")));
  EXPECT_TRUE(IsHlsUrl(GURL("https://cdn.test/path/to/stream.m3u8")));
  EXPECT_TRUE(IsHlsUrl(GURL("https://example.com/video.m3u8?token=abc&t=123")));
}

TEST(IsHlsUrlTest, DetectsHlsVariantInPath) {
  EXPECT_TRUE(IsHlsUrl(GURL("https://example.com/hls_variant/stream")));
  EXPECT_TRUE(
      IsHlsUrl(GURL("https://cdn.test/path/hls_variant_720p/index.mp4")));
}

TEST(IsHlsUrlTest, RejectsOrdinaryUrls) {
  EXPECT_FALSE(IsHlsUrl(GURL("https://example.com/video.mp4")));
  EXPECT_FALSE(IsHlsUrl(GURL("https://example.com/video.webm")));
  EXPECT_FALSE(IsHlsUrl(GURL("https://example.com/stream")));
  EXPECT_FALSE(IsHlsUrl(GURL("https://example.com/")));
}

TEST(IsHlsUrlTest, RejectsEmptyAndInvalidUrls) {
  EXPECT_FALSE(IsHlsUrl(GURL()));
  EXPECT_FALSE(IsHlsUrl(GURL("")));
  EXPECT_FALSE(IsHlsUrl(GURL("not-a-url")));
}

// --- UrlPlayerDemuxer tests ---

class UrlPlayerDemuxerTest : public testing::Test {
 protected:
  UrlPlayerDemuxerTest()
      : demuxer_(task_environment_.GetMainThreadTaskRunner(),
                 GURL("https://example.com/video.m3u8")) {}

  base::test::TaskEnvironment task_environment_;
  UrlPlayerDemuxer demuxer_;
  testing::NiceMock<MockDemuxerHost> host_;
};

TEST_F(UrlPlayerDemuxerTest, GetMediaUrlReturnsOriginalUrl) {
  EXPECT_EQ(demuxer_.GetMediaUrl(), GURL("https://example.com/video.m3u8"));
}

TEST_F(UrlPlayerDemuxerTest, GetAllStreamsReturnsAudioAndVideo) {
  auto streams = demuxer_.GetAllStreams();
  ASSERT_EQ(streams.size(), 2u);

  bool has_audio = false;
  bool has_video = false;
  for (auto* stream : streams) {
    if (stream->type() == DemuxerStream::AUDIO) {
      has_audio = true;
    }
    if (stream->type() == DemuxerStream::VIDEO) {
      has_video = true;
    }
  }
  EXPECT_TRUE(has_audio);
  EXPECT_TRUE(has_video);
}

TEST_F(UrlPlayerDemuxerTest, ReportsDemuxerType) {
  EXPECT_EQ(demuxer_.GetDemuxerType(), DemuxerType::kUrlPlayerDemuxer);
}

TEST_F(UrlPlayerDemuxerTest, ReportsDisplayName) {
  EXPECT_EQ(demuxer_.GetDisplayName(), "UrlPlayerDemuxer");
}

TEST_F(UrlPlayerDemuxerTest, InitializeSucceeds) {
  PipelineStatus result = PIPELINE_ERROR_ABORT;
  demuxer_.Initialize(&host_, base::BindOnce([](PipelineStatus* out,
                                                PipelineStatus s) { *out = s; },
                                             &result));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(result, PIPELINE_OK);
}

TEST_F(UrlPlayerDemuxerTest, SeekSucceeds) {
  demuxer_.Initialize(&host_, base::DoNothing());
  task_environment_.RunUntilIdle();

  PipelineStatus result = PIPELINE_ERROR_ABORT;
  demuxer_.Seek(
      base::Seconds(10),
      base::BindOnce([](PipelineStatus* out, PipelineStatus s) { *out = s; },
                     &result));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(result, PIPELINE_OK);
}

TEST_F(UrlPlayerDemuxerTest, IsSeekable) {
  EXPECT_TRUE(demuxer_.IsSeekable());
}

TEST_F(UrlPlayerDemuxerTest, PlaceholderStreamConfigs) {
  auto streams = demuxer_.GetAllStreams();
  for (auto* stream : streams) {
    if (stream->type() == DemuxerStream::AUDIO) {
      auto config = stream->audio_decoder_config();
      EXPECT_TRUE(config.IsValidConfig());
      EXPECT_EQ(config.codec(), AudioCodec::kAAC);
    }
    if (stream->type() == DemuxerStream::VIDEO) {
      auto config = stream->video_decoder_config();
      EXPECT_TRUE(config.IsValidConfig());
      EXPECT_EQ(config.codec(), VideoCodec::kH264);
    }
  }
}

TEST_F(UrlPlayerDemuxerTest, ZeroMemoryUsage) {
  EXPECT_EQ(demuxer_.GetMemoryUsage(), 0);
}

}  // namespace
}  // namespace media
