// Copyright 2023 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/media/media_util.h"

#include <vector>

#include "starboard/common/log.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/resolutions.h"
#include "testing/gtest/include/gtest/gtest.h"

extern bool operator==(const SbMediaColorMetadata&,
                       const SbMediaColorMetadata&);

namespace starboard {
namespace {

std::vector<uint8_t> ToVector(const void* data, int size) {
  if (size == 0) {
    return std::vector<uint8_t>();
  }
  SB_CHECK(data);
  const uint8_t* data_as_uint8 = static_cast<const uint8_t*>(data);
  return std::vector<uint8_t>(data_as_uint8, data_as_uint8 + size);
}

TEST(AudioStreamInfoTest, DefaultCtor) {
  AudioStreamInfo audio_stream_info;

  EXPECT_EQ(audio_stream_info.codec, kSbMediaAudioCodecNone);
  // No other members should be accessed if `codec` is `kSbMediaAudioCodecNone`,
  // however we still want to make sure that the following members are empty.
  EXPECT_TRUE(audio_stream_info.mime.empty());
  EXPECT_TRUE(audio_stream_info.audio_specific_config.empty());
}

TEST(AudioStreamInfoTest, CodecNone) {
  SbMediaAudioStreamInfo sb_media_audio_stream_info = {kSbMediaAudioCodecNone};

  AudioStreamInfo audio_stream_info;
  audio_stream_info = sb_media_audio_stream_info;
  EXPECT_EQ(audio_stream_info.codec, kSbMediaAudioCodecNone);

  audio_stream_info.ConvertTo(&sb_media_audio_stream_info);
  EXPECT_EQ(sb_media_audio_stream_info.codec, kSbMediaAudioCodecNone);
}

TEST(AudioStreamInfoTest, SbMediaAudioStreamInfo) {
  SbMediaAudioStreamInfo original = {};

  original.codec = kSbMediaAudioCodecOpus;
  original.mime = "audio/webm";
  original.number_of_channels = 2;
  original.samples_per_second = 24000;
  original.bits_per_sample = 16;
  original.audio_specific_config_size = 6;
  original.audio_specific_config = "config";

  AudioStreamInfo audio_stream_info(original);

  EXPECT_EQ(original.codec, audio_stream_info.codec);
  EXPECT_EQ(original.mime, audio_stream_info.mime);
  EXPECT_EQ(original.number_of_channels, audio_stream_info.number_of_channels);
  EXPECT_EQ(original.samples_per_second, audio_stream_info.samples_per_second);
  EXPECT_EQ(original.bits_per_sample, audio_stream_info.bits_per_sample);
  EXPECT_EQ(ToVector(original.audio_specific_config,
                     original.audio_specific_config_size),
            audio_stream_info.audio_specific_config);
}

TEST(VideoStreamInfoTest, DefaultCtor) {
  VideoStreamInfo video_stream_info;

  EXPECT_EQ(video_stream_info.codec, kSbMediaVideoCodecNone);
  // No other members should be accessed if `codec` is `kSbMediaVideoCodecNone`,
  // however we still want to make sure that the following members are empty.
  EXPECT_TRUE(video_stream_info.mime.empty());
  EXPECT_TRUE(video_stream_info.max_video_capabilities.empty());
}

TEST(VideoStreamInfoTest, CodecNone) {
  SbMediaVideoStreamInfo sb_media_video_stream_info = {kSbMediaVideoCodecNone};

  VideoStreamInfo video_stream_info;
  video_stream_info = sb_media_video_stream_info;
  EXPECT_EQ(video_stream_info.codec, kSbMediaVideoCodecNone);

  video_stream_info.ConvertTo(&sb_media_video_stream_info);
  EXPECT_EQ(sb_media_video_stream_info.codec, kSbMediaVideoCodecNone);
}

TEST(VideoStreamInfoTest, SbMediaVideoStreamInfo) {
  SbMediaVideoStreamInfo original = {};

  original.codec = kSbMediaVideoCodecAv1;
  original.mime = "video/mp4";
  original.max_video_capabilities = "width=3840";
  original.frame_width = 1080;
  original.frame_height = 1920;

  VideoStreamInfo video_stream_info(original);

  EXPECT_EQ(original.codec, video_stream_info.codec);
  EXPECT_EQ(original.mime, video_stream_info.mime);
  EXPECT_EQ(original.max_video_capabilities,
            video_stream_info.max_video_capabilities);
  EXPECT_EQ(original.frame_width, video_stream_info.frame_size.width);
  EXPECT_EQ(original.frame_height, video_stream_info.frame_size.height);
  EXPECT_EQ(original.color_metadata, video_stream_info.color_metadata);
}

TEST(AudioSampleInfoTest, DefaultCtor) {
  AudioSampleInfo audio_sample_info;

  EXPECT_EQ(audio_sample_info.stream_info.codec, kSbMediaAudioCodecNone);
  // No other members should be accessed if `codec` is `kSbMediaAudioCodecNone`,
  // however we still want to make sure that the following members are empty.
  EXPECT_TRUE(audio_sample_info.stream_info.mime.empty());
  EXPECT_TRUE(audio_sample_info.stream_info.audio_specific_config.empty());
}

TEST(AudioSampleInfoTest, SbMediaAudioSampleInfo) {
  SbMediaAudioSampleInfo original = {};
  SbMediaAudioStreamInfo& stream_info = original.stream_info;

  stream_info.codec = kSbMediaAudioCodecOpus;
  stream_info.mime = "audio/webm";
  stream_info.number_of_channels = 2;
  stream_info.samples_per_second = 24000;
  stream_info.bits_per_sample = 16;
  stream_info.audio_specific_config_size = 6;
  stream_info.audio_specific_config = "config";

  AudioSampleInfo audio_sample_info(original);

  EXPECT_EQ(stream_info.codec, audio_sample_info.stream_info.codec);
  EXPECT_EQ(stream_info.mime, audio_sample_info.stream_info.mime);
  EXPECT_EQ(stream_info.number_of_channels,
            audio_sample_info.stream_info.number_of_channels);
  EXPECT_EQ(stream_info.samples_per_second,
            audio_sample_info.stream_info.samples_per_second);
  EXPECT_EQ(stream_info.bits_per_sample,
            audio_sample_info.stream_info.bits_per_sample);
  EXPECT_EQ(ToVector(stream_info.audio_specific_config,
                     stream_info.audio_specific_config_size),
            audio_sample_info.stream_info.audio_specific_config);
}

TEST(VideoSampleInfoTest, DefaultCtor) {
  VideoSampleInfo video_sample_info;

  EXPECT_EQ(video_sample_info.stream_info.codec, kSbMediaVideoCodecNone);
  // No other members should be accessed if `codec` is `kSbMediaVideoCodecNone`,
  // however we still want to make sure that the following members are empty.
  EXPECT_TRUE(video_sample_info.stream_info.mime.empty());
  EXPECT_TRUE(video_sample_info.stream_info.max_video_capabilities.empty());
}

TEST(VideoSampleInfoTest, SbMediaVideoSampleInfo) {
  SbMediaVideoSampleInfo original = {};
  SbMediaVideoStreamInfo& stream_info = original.stream_info;

  original.is_key_frame = true;
  stream_info.codec = kSbMediaVideoCodecAv1;
  stream_info.mime = "video/mp4";
  stream_info.max_video_capabilities = "width=3840";
  stream_info.frame_width = 1080;
  stream_info.frame_height = 1920;

  VideoSampleInfo video_sample_info(original);

  EXPECT_EQ(original.is_key_frame, video_sample_info.is_key_frame);
  EXPECT_EQ(stream_info.codec, video_sample_info.stream_info.codec);
  EXPECT_EQ(stream_info.mime, video_sample_info.stream_info.mime);
  EXPECT_EQ(stream_info.max_video_capabilities,
            video_sample_info.stream_info.max_video_capabilities);
  EXPECT_EQ(stream_info.frame_width,
            video_sample_info.stream_info.frame_size.width);
  EXPECT_EQ(stream_info.frame_height,
            video_sample_info.stream_info.frame_size.height);
  EXPECT_EQ(stream_info.color_metadata,
            video_sample_info.stream_info.color_metadata);
}

TEST(MediaUtilTest, AudioDurationToFrames) {
  EXPECT_EQ(AudioDurationToFrames(0, 48000), 0);
  EXPECT_EQ(AudioDurationToFrames(1'000'000LL / 2, 48000), 48000 / 2);
  EXPECT_EQ(AudioDurationToFrames(1'000'000LL, 48000), 48000);
  EXPECT_EQ(AudioDurationToFrames(1'000'000LL * 2, 48000), 48000 * 2);
}

TEST(MediaUtilTest, AudioFramesToDuration) {
  EXPECT_EQ(AudioFramesToDuration(0, 48000), 0);
  EXPECT_EQ(AudioFramesToDuration(48000 / 2, 48000), 1'000'000LL / 2);
  EXPECT_EQ(AudioFramesToDuration(48000, 48000), 1'000'000LL);
  EXPECT_EQ(AudioFramesToDuration(48000 * 2, 48000), 1'000'000LL * 2);
}

TEST(AudioStreamInfoTest, StreamOperator) {
  AudioStreamInfo audio_stream_info;
  audio_stream_info.codec = kSbMediaAudioCodecOpus;
  audio_stream_info.mime = "audio/webm";
  audio_stream_info.number_of_channels = 2;
  audio_stream_info.samples_per_second = 48000;
  audio_stream_info.bits_per_sample = 16;

  std::stringstream ss;
  ss << audio_stream_info;

  EXPECT_EQ(ss.str(),
            "{codec=opus, mime=audio/webm, channels=2, "
            "samples_per_second=48'000, bits_per_sample=16}");
}

TEST(AudioStreamInfoTest, StreamOperator_Empty) {
  AudioStreamInfo audio_stream_info;
  audio_stream_info.codec = kSbMediaAudioCodecOpus;
  audio_stream_info.number_of_channels = 2;
  audio_stream_info.samples_per_second = 48000;
  audio_stream_info.bits_per_sample = 16;

  std::stringstream ss;
  ss << audio_stream_info;

  EXPECT_EQ(ss.str(),
            "{codec=opus, mime=(empty), channels=2, "
            "samples_per_second=48'000, bits_per_sample=16}");
}

TEST(MediaUtilTest, Resolutions) {
  EXPECT_EQ(Resolution::k240p, Size(426, 240));
  EXPECT_EQ(Resolution::k360p, Size(640, 360));
  EXPECT_EQ(Resolution::k480p, Size(854, 480));
  EXPECT_EQ(Resolution::k720p, Size(1280, 720));
  EXPECT_EQ(Resolution::k1080p, Size(1920, 1080));
  EXPECT_EQ(Resolution::k1440p, Size(2560, 1440));
  EXPECT_EQ(Resolution::k4k, Size(3840, 2160));
  EXPECT_EQ(Resolution::k8k, Size(7680, 4320));
}

TEST(MediaUtilTest, IsSDRVideo) {
  EXPECT_TRUE(IsSDRVideo(8, kSbMediaPrimaryIdBt709, kSbMediaTransferIdBt709,
                         kSbMediaMatrixIdBt709));
  EXPECT_TRUE(IsSDRVideo(8, kSbMediaPrimaryIdUnspecified,
                         kSbMediaTransferIdUnspecified,
                         kSbMediaMatrixIdUnspecified));
  EXPECT_TRUE(IsSDRVideo(8, kSbMediaPrimaryIdSmpte170M,
                         kSbMediaTransferIdSmpte170M,
                         kSbMediaMatrixIdSmpte170M));

  EXPECT_FALSE(IsSDRVideo(10, kSbMediaPrimaryIdBt709, kSbMediaTransferIdBt709,
                          kSbMediaMatrixIdBt709));
  EXPECT_FALSE(IsSDRVideo(12, kSbMediaPrimaryIdBt709, kSbMediaTransferIdBt709,
                          kSbMediaMatrixIdBt709));

  EXPECT_FALSE(IsSDRVideo(8, kSbMediaPrimaryIdBt2020, kSbMediaTransferIdBt709,
                          kSbMediaMatrixIdBt709));
  EXPECT_FALSE(IsSDRVideo(8, kSbMediaPrimaryIdBt709,
                          kSbMediaTransferIdSmpteSt2084,
                          kSbMediaMatrixIdBt709));
  EXPECT_FALSE(IsSDRVideo(8, kSbMediaPrimaryIdBt709, kSbMediaTransferIdBt709,
                          kSbMediaMatrixIdBt2020NonconstantLuminance));
}

TEST(MediaUtilTest, IsSDRVideo_Mime) {
  // Standard 8-bit AVC / H.264 stream without HDR colorimetry.
  EXPECT_TRUE(IsSDRVideo("video/mp4; codecs=\"avc1.4d401e\""));
  // Short-form VP9 string where bit depth defaults to 8-bit and colorimetry
  // defaults to BT.709.
  EXPECT_TRUE(IsSDRVideo("video/webm; codecs=\"vp9\""));
  // Unrecognized or invalid MIME types fall back to assuming SDR video.
  EXPECT_TRUE(IsSDRVideo("invalid_mime"));
  // 8-bit VP9 with explicitly specified BT.709 color primaries (01),
  // transfer function (01), and matrix coefficients (01).
  EXPECT_TRUE(
      IsSDRVideo("video/webm; codecs=\"vp09.00.51.08.01.01.01.01.01\""));

  // 10-bit depth (Profile 2 VP9) is HDR, not SDR.
  EXPECT_FALSE(
      IsSDRVideo("video/webm; codecs=\"vp09.02.51.10.01.09.16.09.01\""));
  // 12-bit depth (Profile 3 VP9) is HDR, not SDR.
  EXPECT_FALSE(
      IsSDRVideo("video/webm; codecs=\"vp09.03.51.12.01.09.16.09.01\""));
  // 8-bit depth VP9 paired with BT.2020 color primaries (09) and SMPTE ST 2084
  // PQ transfer curve (16) is HDR, not SDR.
  EXPECT_FALSE(
      IsSDRVideo("video/webm; codecs=\"vp09.00.51.08.01.09.16.09.01\""));
}

}  // namespace
}  // namespace starboard
