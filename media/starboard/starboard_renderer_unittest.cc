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

#include "media/starboard/starboard_renderer.h"

#include <memory>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "media/base/decoder_buffer.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_util.h"
#include "media/base/mock_filters.h"
#include "media/base/test_helpers.h"
#include "media/starboard/mock_sbplayer_interface.h"
#include "media/starboard/sbplayer_interface.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::base::test::RunOnceCallback;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace media {

namespace {

class StarboardRendererTest : public testing::Test {
 protected:
  StarboardRendererTest() {
    renderer_ = std::make_unique<StarboardRenderer>(
        task_environment_.GetMainThreadTaskRunner(),
        std::make_unique<NullMediaLog>(),
        /*overlay_plane_id=*/base::UnguessableToken::Create(),
        /*max_video_capabilities=*/"",
        StarboardRendererConfig::ExperimentalFeatures{},
        /*viewport_size=*/gfx::Size()
#if BUILDFLAG(IS_ANDROID)
            ,
        /*android_overlay_factory_cb=*/AndroidOverlayMojoFactoryCB()
#endif  // BUILDFLAG(IS_ANDROID)
    );
    renderer_->SetStarboardRendererCallbacks(
        /*paint_video_hole_frame_cb=*/base::DoNothing(),
        /*update_starboard_rendering_mode_cb=*/base::DoNothing(),
        /*get_sb_window_handle_cb=*/base::NullCallback()
#if BUILDFLAG(IS_ANDROID)
            ,
        /*request_overlay_info_cb=*/base::DoNothing()
#endif  // BUILDFLAG(IS_ANDROID)
    );
    StarboardRenderer::GetDecodeTargetGraphicsContextProviderFunc
        get_decode_target_graphics_context_provider_func = base::BindRepeating(
            &StarboardRendererTest::GetSbDecodeTargetGraphicsContextProvider,
            base::Unretained(this));
    renderer_->set_decode_target_graphics_context_provider(
        get_decode_target_graphics_context_provider_func);

    EXPECT_CALL(media_resource_, GetAllStreams())
        .WillRepeatedly(Invoke(this, &StarboardRendererTest::GetAllStreams));
  }

  ~StarboardRendererTest() override = default;

  void AddStream(DemuxerStream::Type type, bool encrypted) {
    streams_.push_back(CreateMockDemuxerStream(type, encrypted));
  }

  std::vector<DemuxerStream*> GetAllStreams() {
    std::vector<DemuxerStream*> streams;
    for (auto& stream : streams_) {
      streams.push_back(stream.get());
    }
    return streams;
  }

  SbPlayer InitializeWithAudioAndVideo(bool encrypted = false) {
    AddStream(DemuxerStream::AUDIO, encrypted);
    AddStream(DemuxerStream::VIDEO, encrypted);

    SbPlayer player = reinterpret_cast<SbPlayer>(new MockSbPlayer());
    EXPECT_CALL(mock_sbplayer_interface_, Create(_, _, _, _, _, _, _, _))
        .WillOnce(DoAll(SaveArg<3>(&decoder_status_cb_),
                        SaveArg<4>(&player_status_cb_),
                        SaveArg<5>(&player_error_cb_), SaveArg<6>(&context_),
                        Return(player)));

    if (encrypted) {
      EXPECT_CALL(set_cdm_cb_, Run(true));
      renderer_->SetCdm(&cdm_context_, set_cdm_cb_.Get());
    }

    EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
    renderer_->Initialize(&media_resource_, &renderer_client_,
                          renderer_init_cb_.Get());
    return player;
  }

  SbDecodeTargetGraphicsContextProvider*
  GetSbDecodeTargetGraphicsContextProvider() {
    return &decode_target_graphics_context_provider_;
  }

  base::test::TaskEnvironment task_environment_;
  base::MockOnceCallback<void(bool)> set_cdm_cb_;
  base::MockOnceCallback<void(PipelineStatus)> renderer_init_cb_;
  NiceMock<MockCdmContext> cdm_context_;
  NiceMock<MockMediaResource> media_resource_;
  NiceMock<MockRendererClient> renderer_client_;
  std::vector<std::unique_ptr<StrictMock<MockDemuxerStream>>> streams_;
  StrictMock<MockSbPlayerInterface> mock_sbplayer_interface_;
  ScopedSbPlayerInterfaceForTesting scoped_sbplayer_interface_{
      &mock_sbplayer_interface_};
  SbPlayerDecoderStatusFunc decoder_status_cb_ = nullptr;
  SbPlayerStatusFunc player_status_cb_ = nullptr;
  SbPlayerErrorFunc player_error_cb_ = nullptr;
  void* context_ = nullptr;
  SbDecodeTargetGraphicsContextProvider
      decode_target_graphics_context_provider_;
  std::unique_ptr<StarboardRenderer> renderer_;
};

TEST_F(StarboardRendererTest, InitializeWithClearContent) {
  SbPlayer player = InitializeWithAudioAndVideo();
  ASSERT_TRUE(player_status_cb_);
  player_status_cb_(player, context_, kSbPlayerStateInitialized,
                    /*ticket=*/SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, InitializeWaitsForCdm) {
  AddStream(DemuxerStream::AUDIO, /*encrypted=*/true);
  AddStream(DemuxerStream::VIDEO, /*encrypted=*/true);

  EXPECT_CALL(renderer_client_, OnWaiting(WaitingReason::kNoCdm));

  renderer_->Initialize(&media_resource_, &renderer_client_,
                        renderer_init_cb_.Get());

  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, SetCdmThenInitialize) {
  SbPlayer player = InitializeWithAudioAndVideo(/*encrypted=*/true);
  ASSERT_TRUE(player_status_cb_);
  player_status_cb_(player, context_, kSbPlayerStateInitialized,
                    /*ticket=*/SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, InitializeThenSetCdm) {
  AddStream(DemuxerStream::AUDIO, /*encrypted=*/true);
  AddStream(DemuxerStream::VIDEO, /*encrypted=*/true);

  SbPlayer player = reinterpret_cast<SbPlayer>(new MockSbPlayer());
  EXPECT_CALL(mock_sbplayer_interface_, Create(_, _, _, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&decoder_status_cb_),
                      SaveArg<4>(&player_status_cb_),
                      SaveArg<5>(&player_error_cb_), SaveArg<6>(&context_),
                      Return(player)));
  EXPECT_CALL(renderer_client_, OnWaiting(WaitingReason::kNoCdm));
  renderer_->Initialize(&media_resource_, &renderer_client_,
                        renderer_init_cb_.Get());
  task_environment_.RunUntilIdle();

  EXPECT_CALL(set_cdm_cb_, Run(true));
  renderer_->SetCdm(&cdm_context_, set_cdm_cb_.Get());
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(player_status_cb_);
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  player_status_cb_(player, context_, kSbPlayerStateInitialized,
                    /*ticket=*/SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, InitializeFailsWithNoStreams) {
  EXPECT_CALL(renderer_init_cb_,
              Run(HasStatusCode(DEMUXER_ERROR_NO_SUPPORTED_STREAMS)));

  renderer_->Initialize(&media_resource_, &renderer_client_,
                        renderer_init_cb_.Get());

  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, InitializeWithInvalidSbPlayer) {
  AddStream(DemuxerStream::AUDIO, /*encrypted=*/false);
  AddStream(DemuxerStream::VIDEO, /*encrypted=*/false);

  EXPECT_CALL(mock_sbplayer_interface_, Create(_, _, _, _, _, _, _, _))
      .WillOnce(Return(kSbPlayerInvalid));
  EXPECT_CALL(renderer_init_cb_,
              Run(HasStatusCode(DECODER_ERROR_NOT_SUPPORTED)));

  renderer_->Initialize(&media_resource_, &renderer_client_,
                        renderer_init_cb_.Get());

  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, OnPlayerStatusCallbacksPresenting) {
  SbPlayer player = InitializeWithAudioAndVideo();
  ASSERT_TRUE(player_status_cb_);
  player_status_cb_(player, context_, kSbPlayerStateInitialized,
                    /*ticket=*/SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();
  EXPECT_CALL(renderer_client_,
              OnBufferingStateChange(BUFFERING_HAVE_ENOUGH,
                                     BUFFERING_CHANGE_REASON_UNKNOWN));
  player_status_cb_(player, context_, kSbPlayerStatePresenting,
                    /*ticket=*/SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, OnPlayerStatusCallbacksEnded) {
  SbPlayer player = InitializeWithAudioAndVideo();
  ASSERT_TRUE(player_status_cb_);
  player_status_cb_(player, context_, kSbPlayerStateInitialized,
                    /*ticket=*/SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();

  EXPECT_CALL(renderer_client_, OnEnded());
  player_status_cb_(player, context_, kSbPlayerStateEndOfStream,
                    /*ticket=*/SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, OnPlayerErrorCallback) {
  SbPlayer player = InitializeWithAudioAndVideo();
  ASSERT_TRUE(player_status_cb_);
  player_status_cb_(player, context_, kSbPlayerStateInitialized,
                    /*ticket=*/SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(player_error_cb_);

  EXPECT_CALL(renderer_client_, OnError(HasStatusCode(PIPELINE_ERROR_DECODE)));
  player_error_cb_(player, context_, kSbPlayerErrorDecode, "decoding failed");
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, OnErrorDuringInitialization) {
  AddStream(DemuxerStream::AUDIO, /*encrypted=*/false);
  AddStream(DemuxerStream::VIDEO, /*encrypted=*/false);

  SbPlayer player = reinterpret_cast<SbPlayer>(new MockSbPlayer());
  EXPECT_CALL(mock_sbplayer_interface_, Create(_, _, _, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&decoder_status_cb_),
                      SaveArg<4>(&player_status_cb_),
                      SaveArg<5>(&player_error_cb_), SaveArg<6>(&context_),
                      Return(player)));

  // Expect renderer_init_cb_ to be called with an error.
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_ERROR_DECODE)));
  // renderer_client_.OnError should NOT be called because init_cb_ is pending.
  EXPECT_CALL(renderer_client_, OnError(_)).Times(0);

  renderer_->Initialize(&media_resource_, &renderer_client_,
                        renderer_init_cb_.Get());
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(player_error_cb_);
  // Trigger an error before initialization is complete.
  player_error_cb_(player, context_, kSbPlayerErrorDecode, "decoding failed");
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, OnDemuxerErrorDuringInitialization) {
  AddStream(DemuxerStream::AUDIO, /*encrypted=*/false);
  AddStream(DemuxerStream::VIDEO, /*encrypted=*/false);

  SbPlayer player = reinterpret_cast<SbPlayer>(new MockSbPlayer());
  EXPECT_CALL(mock_sbplayer_interface_, Create(_, _, _, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&decoder_status_cb_),
                      SaveArg<4>(&player_status_cb_),
                      SaveArg<5>(&player_error_cb_), SaveArg<6>(&context_),
                      Return(player)));

  // Expect renderer_init_cb_ to be called with an error.
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_ERROR_READ)));
  // renderer_client_.OnError should NOT be called because init_cb_ is pending.
  EXPECT_CALL(renderer_client_, OnError(_)).Times(0);

  renderer_->Initialize(&media_resource_, &renderer_client_,
                        renderer_init_cb_.Get());
  task_environment_.RunUntilIdle();

  // Now that the player is created (but not initialized), simulate the player
  // asking for data.
  DemuxerStream::ReadCB read_cb;
  EXPECT_CALL(*streams_[0], OnRead(_))
      .WillOnce(Invoke(
          [&read_cb](DemuxerStream::ReadCB& cb) { read_cb = std::move(cb); }));
  EXPECT_CALL(*streams_[1], OnRead(_)).Times(0);

  // Trigger OnNeedData to start a read.
  decoder_status_cb_(player, context_, kSbMediaTypeAudio,
                     kSbPlayerDecoderStateNeedsData, SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();

  ASSERT_FALSE(read_cb.is_null());
  // Simulate a demuxer error.
  std::move(read_cb).Run(DemuxerStream::kError, {});
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest, RejectCdmSwitching) {
  EXPECT_CALL(set_cdm_cb_, Run(true));
  renderer_->SetCdm(&cdm_context_, set_cdm_cb_.Get());
  task_environment_.RunUntilIdle();

  base::MockOnceCallback<void(bool)> second_set_cdm_cb;
  EXPECT_CALL(second_set_cdm_cb, Run(false));
  renderer_->SetCdm(&cdm_context_, second_set_cdm_cb.Get());
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererTest,
       CustomMimeTypesPassedToSbPlayerCreateAndWriteSamples) {
  const std::string kCustomAudioMime =
      "audio/mp4; codecs=\"mp4a.40.2\"; channels=8; bitrate=768000; "
      "audiopassthrough=true; enableresetaudiodecoder=true;";
  const std::string kCustomVideoMime =
      "video/mp4; codecs=\"avc1.64002a\"; width=3840; height=2160; "
      "tunnelmode=true; hdr=hdr10plus; framerate=60;";

  auto audio_stream =
      std::make_unique<StrictMock<MockDemuxerStream>>(DemuxerStream::AUDIO);
  AudioDecoderConfig audio_config = TestAudioConfig::Normal();
  audio_config.set_mime_type(kCustomAudioMime);
  audio_stream->set_audio_decoder_config(audio_config);
  streams_.push_back(std::move(audio_stream));

  auto video_stream =
      std::make_unique<StrictMock<MockDemuxerStream>>(DemuxerStream::VIDEO);
  VideoDecoderConfig video_config = TestVideoConfig::Normal();
  video_config.set_mime_type(kCustomVideoMime);
  video_stream->set_video_decoder_config(video_config);
  streams_.push_back(std::move(video_stream));

  SbPlayer player = reinterpret_cast<SbPlayer>(new MockSbPlayer());
  std::string created_audio_mime;
  std::string created_video_mime;

  EXPECT_CALL(mock_sbplayer_interface_, Create(_, _, _, _, _, _, _, _))
      .WillOnce(Invoke(
          [&](SbWindow /*window*/, const SbPlayerCreationParam* creation_param,
              SbPlayerDeallocateSampleFunc /*sample_deallocate_func*/,
              SbPlayerDecoderStatusFunc decoder_status_func,
              SbPlayerStatusFunc player_status_func,
              SbPlayerErrorFunc player_error_func, void* context,
              SbDecodeTargetGraphicsContextProvider* /*context_provider*/) {
            decoder_status_cb_ = decoder_status_func;
            player_status_cb_ = player_status_func;
            player_error_cb_ = player_error_func;
            context_ = context;
            if (creation_param) {
              if (creation_param->audio_stream_info.mime) {
                created_audio_mime = creation_param->audio_stream_info.mime;
              }
              if (creation_param->video_stream_info.mime) {
                created_video_mime = creation_param->video_stream_info.mime;
              }
            }
            return player;
          }));

  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  renderer_->Initialize(&media_resource_, &renderer_client_,
                        renderer_init_cb_.Get());
  task_environment_.RunUntilIdle();

  // Verify that SbPlayerCreate received the custom MIME strings.
  EXPECT_EQ(created_audio_mime, kCustomAudioMime);
  EXPECT_EQ(created_video_mime, kCustomVideoMime);

  // Verify WriteSamples for Audio with custom MIME parameters.
  DemuxerStream::ReadCB audio_read_cb;
  EXPECT_CALL(*streams_[0], OnRead(_))
      .WillOnce(Invoke(
          [&](DemuxerStream::ReadCB& cb) { audio_read_cb = std::move(cb); }));

  std::string written_audio_mime;
  EXPECT_CALL(mock_sbplayer_interface_,
              WriteSamples(player, kSbMediaTypeAudio, _, _))
      .WillOnce(Invoke([&](SbPlayer /*player*/, SbMediaType /*type*/,
                           const SbPlayerSampleInfo* sample_infos,
                           int number_of_sample_infos) {
        EXPECT_NE(sample_infos, nullptr);
        EXPECT_GT(number_of_sample_infos, 0);
        if (sample_infos && number_of_sample_infos > 0 &&
            sample_infos[0].audio_sample_info.stream_info.mime) {
          written_audio_mime =
              sample_infos[0].audio_sample_info.stream_info.mime;
        }
      }));

  decoder_status_cb_(player, context_, kSbMediaTypeAudio,
                     kSbPlayerDecoderStateNeedsData, SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();

  ASSERT_FALSE(audio_read_cb.is_null());
  const uint8_t kAudioData[] = {0x01, 0x02, 0x03, 0x04};
  scoped_refptr<DecoderBuffer> audio_buffer =
      DecoderBuffer::CopyFrom(kAudioData);
  std::move(audio_read_cb).Run(DemuxerStream::kOk, {audio_buffer});
  task_environment_.RunUntilIdle();

  EXPECT_EQ(written_audio_mime, kCustomAudioMime);

  // Verify WriteSamples for Video with custom MIME parameters.
  DemuxerStream::ReadCB video_read_cb;
  EXPECT_CALL(*streams_[1], OnRead(_))
      .WillOnce(Invoke(
          [&](DemuxerStream::ReadCB& cb) { video_read_cb = std::move(cb); }));

  std::string written_video_mime;
  EXPECT_CALL(mock_sbplayer_interface_,
              WriteSamples(player, kSbMediaTypeVideo, _, _))
      .WillOnce(Invoke([&](SbPlayer /*player*/, SbMediaType /*type*/,
                           const SbPlayerSampleInfo* sample_infos,
                           int number_of_sample_infos) {
        EXPECT_NE(sample_infos, nullptr);
        EXPECT_GT(number_of_sample_infos, 0);
        if (sample_infos && number_of_sample_infos > 0 &&
            sample_infos[0].video_sample_info.stream_info.mime) {
          written_video_mime =
              sample_infos[0].video_sample_info.stream_info.mime;
        }
      }));

  decoder_status_cb_(player, context_, kSbMediaTypeVideo,
                     kSbPlayerDecoderStateNeedsData, SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();

  ASSERT_FALSE(video_read_cb.is_null());
  const uint8_t kVideoData[] = {0x00, 0x00, 0x00, 0x01};
  scoped_refptr<DecoderBuffer> video_buffer =
      DecoderBuffer::CopyFrom(kVideoData);
  std::move(video_read_cb).Run(DemuxerStream::kOk, {video_buffer});
  task_environment_.RunUntilIdle();

  EXPECT_EQ(written_video_mime, kCustomVideoMime);

  ASSERT_TRUE(player_status_cb_);
  player_status_cb_(player, context_, kSbPlayerStateInitialized,
                    SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();
}

}  // namespace

}  // namespace media
