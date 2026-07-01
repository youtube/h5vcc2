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

#include "media/starboard/url_player_renderer.h"

#include <memory>
#include <string>

#include "base/functional/callback_helpers.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "media/base/media_util.h"
#include "media/base/mock_filters.h"
#include "media/base/test_helpers.h"
#include "media/starboard/sbplayer_interface.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

struct SbPlayerPrivate {};
struct SbDrmSystemPrivate {};

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrEq;

namespace media {
namespace {

constexpr char kSourceUrl[] = "https://example.test/video.m3u8";

class MockSbPlayerInterface : public SbPlayerInterface {
 public:
  MOCK_METHOD8(Create,
               SbPlayer(SbWindow,
                        const SbPlayerCreationParam*,
                        SbPlayerDeallocateSampleFunc,
                        SbPlayerDecoderStatusFunc,
                        SbPlayerStatusFunc,
                        SbPlayerErrorFunc,
                        void*,
                        SbDecodeTargetGraphicsContextProvider*));
  MOCK_METHOD1(GetPreferredOutputMode,
               SbPlayerOutputMode(const SbPlayerCreationParam*));
  void Destroy(SbPlayer player) override { delete player; }
  MOCK_METHOD3(Seek, void(SbPlayer, base::TimeDelta, int));
  MOCK_METHOD4(WriteSamples,
               void(SbPlayer, SbMediaType, const SbPlayerSampleInfo*, int));
  MOCK_METHOD2(GetMaximumNumberOfSamplesPerWrite, int(SbPlayer, SbMediaType));
  MOCK_METHOD2(WriteEndOfStream, void(SbPlayer, SbMediaType));
  MOCK_METHOD6(SetBounds, void(SbPlayer, int, int, int, int, int));
  MOCK_METHOD2(SetPlaybackRate, bool(SbPlayer, double));
  MOCK_METHOD2(SetVolume, void(SbPlayer, double));
  MOCK_METHOD2(GetInfo, void(SbPlayer, SbPlayerInfo*));
  MOCK_METHOD1(GetCurrentFrame, SbDecodeTarget(SbPlayer));

  MOCK_METHOD6(CreateUrlPlayer,
               SbPlayer(const char*,
                        SbWindow,
                        SbPlayerStatusFunc,
                        SbPlayerEncryptedMediaInitDataEncounteredCB,
                        SbPlayerErrorFunc,
                        void*));
  MOCK_METHOD2(SetUrlPlayerDrmSystem, void(SbPlayer, SbDrmSystem));
  MOCK_METHOD1(GetUrlPlayerOutputModeSupported, bool(SbPlayerOutputMode));
  MOCK_METHOD2(GetUrlPlayerExtraInfo, void(SbPlayer, SbUrlPlayerExtraInfo*));
  MOCK_METHOD3(GetAudioConfiguration,
               bool(SbPlayer, int, SbMediaAudioConfiguration*));
};

class FakeCdmContext : public CdmContext {
 public:
  explicit FakeCdmContext(SbDrmSystem drm_system) : drm_system_(drm_system) {}
  SbDrmSystem GetSbDrmSystem() override { return drm_system_; }

 private:
  SbDrmSystem drm_system_;
};

class UrlPlayerRendererTest : public testing::Test {
 protected:
  UrlPlayerRendererTest() {
    ON_CALL(mock_sbplayer_interface_, GetUrlPlayerOutputModeSupported(_))
        .WillByDefault(Invoke([this](SbPlayerOutputMode output_mode) {
          if (output_mode == kSbPlayerOutputModePunchOut) {
            return punch_out_supported_;
          }
          return decode_to_texture_supported_;
        }));
    ON_CALL(mock_sbplayer_interface_, SetPlaybackRate(_, _))
        .WillByDefault(Return(true));

    renderer_->SetSbPlayerInterfaceForTesting(&mock_sbplayer_interface_);
    renderer_->SetUrlPlayerRendererCallbacks(
        base::DoNothing(), base::DoNothing(), base::NullCallback());
  }

  SbPlayer ExpectPlayerCreation(SbWindow expected_window = kSbWindowInvalid) {
    SbPlayer player = new SbPlayerPrivate();
    EXPECT_CALL(mock_sbplayer_interface_,
                CreateUrlPlayer(StrEq(kSourceUrl), expected_window, _, _, _, _))
        .WillOnce(DoAll(SaveArg<2>(&player_status_cb_),
                        SaveArg<4>(&player_error_cb_), SaveArg<5>(&context_),
                        Return(player)));
    return player;
  }

  void Initialize(SbPlayer player) {
    renderer_->SetSourceUrl(kSourceUrl);
    EXPECT_CALL(init_cb_, Run(HasStatusCode(PIPELINE_OK)));
    renderer_->Initialize(&media_resource_, &renderer_client_, init_cb_.Get());
    ASSERT_TRUE(player_status_cb_);
    player_status_cb_(player, context_, kSbPlayerStateInitialized,
                      SB_PLAYER_INITIAL_TICKET);
    task_environment_.RunUntilIdle();
  }

  base::test::TaskEnvironment task_environment_;
  NiceMock<MockSbPlayerInterface> mock_sbplayer_interface_;
  NiceMock<MockMediaResource> media_resource_;
  NiceMock<MockRendererClient> renderer_client_;
  base::MockOnceCallback<void(PipelineStatus)> init_cb_;
  SbPlayerStatusFunc player_status_cb_ = nullptr;
  SbPlayerErrorFunc player_error_cb_ = nullptr;
  void* context_ = nullptr;
  bool punch_out_supported_ = true;
  bool decode_to_texture_supported_ = false;
  const std::unique_ptr<UrlPlayerRenderer> renderer_ =
      std::make_unique<UrlPlayerRenderer>(
          task_environment_.GetMainThreadTaskRunner(),
          std::make_unique<NullMediaLog>(),
          base::UnguessableToken::Create(),
          gfx::Size());
};

TEST_F(UrlPlayerRendererTest, InitializeWithoutUrlFailsAsync) {
  // Initialize without URL set — should fail asynchronously.
  EXPECT_CALL(mock_sbplayer_interface_, CreateUrlPlayer(_, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(init_cb_,
              Run(HasStatusCode(PIPELINE_ERROR_INITIALIZATION_FAILED)));
  renderer_->Initialize(&media_resource_, &renderer_client_, init_cb_.Get());
  task_environment_.RunUntilIdle();
}

TEST_F(UrlPlayerRendererTest, InitializeCompletesOnPlayerInitialized) {
  SbPlayer player = ExpectPlayerCreation();
  Initialize(player);
  EXPECT_EQ(renderer_->GetRendererType(), RendererType::kUrlPlayer);
}

TEST_F(UrlPlayerRendererTest, InitializeWaitsForWindowHandle) {
  base::MockRepeatingCallback<void()> get_window_cb;
  renderer_->SetUrlPlayerRendererCallbacks(base::DoNothing(), base::DoNothing(),
                                           get_window_cb.Get());
  renderer_->SetSourceUrl(kSourceUrl);
  EXPECT_CALL(get_window_cb, Run());
  EXPECT_CALL(mock_sbplayer_interface_, CreateUrlPlayer(_, _, _, _, _, _))
      .Times(0);
  renderer_->Initialize(&media_resource_, &renderer_client_, init_cb_.Get());

  testing::Mock::VerifyAndClearExpectations(&mock_sbplayer_interface_);
  SbWindow window = reinterpret_cast<SbWindow>(0x1234);
  SbPlayer player = ExpectPlayerCreation(window);
  EXPECT_CALL(init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  renderer_->OnSbWindowHandleReady(reinterpret_cast<uint64_t>(window));
  player_status_cb_(player, context_, kSbPlayerStateInitialized,
                    SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();
}

TEST_F(UrlPlayerRendererTest, RejectsDecodeToTextureFallback) {
  punch_out_supported_ = false;
  decode_to_texture_supported_ = true;
  ExpectPlayerCreation();
  renderer_->SetSourceUrl(kSourceUrl);
  EXPECT_CALL(init_cb_, Run(HasStatusCode(DECODER_ERROR_NOT_SUPPORTED)));

  renderer_->Initialize(&media_resource_, &renderer_client_, init_cb_.Get());
}

TEST_F(UrlPlayerRendererTest, DelegatesRateVolumeSeekAndFlush) {
  SbPlayer player = ExpectPlayerCreation();
  Initialize(player);

  // Volume is not gated by seek_pending.
  EXPECT_CALL(mock_sbplayer_interface_, SetVolume(player, 0.5));
  renderer_->SetVolume(0.5f);

  // StartPlayingFrom triggers Seek which clears seek_pending.
  EXPECT_CALL(mock_sbplayer_interface_, Seek(player, base::Seconds(12), _));
  renderer_->StartPlayingFrom(base::Seconds(12));

  // SetPlaybackRate now reaches the interface after seek_pending is cleared.
  EXPECT_CALL(mock_sbplayer_interface_, SetPlaybackRate(player, 1.25));
  renderer_->SetPlaybackRate(1.25);

  // Flush calls PrepareForSeek which pauses via SetPlaybackRate(0).
  base::MockOnceClosure flush_cb;
  EXPECT_CALL(mock_sbplayer_interface_, SetPlaybackRate(player, 0.0));
  EXPECT_CALL(flush_cb, Run());
  renderer_->Flush(flush_cb.Get());
}

TEST_F(UrlPlayerRendererTest, ReportsPlayerErrorDuringInitialization) {
  SbPlayer player = ExpectPlayerCreation();
  renderer_->SetSourceUrl(kSourceUrl);
  EXPECT_CALL(init_cb_, Run(HasStatusCode(PIPELINE_ERROR_DECODE)));
  EXPECT_CALL(renderer_client_, OnError(_)).Times(0);
  renderer_->Initialize(&media_resource_, &renderer_client_, init_cb_.Get());

  ASSERT_TRUE(player_error_cb_);
  player_error_cb_(player, context_, kSbPlayerErrorDecode, "decode failed");
  task_environment_.RunUntilIdle();
}

TEST_F(UrlPlayerRendererTest, ReportsPlayerErrorAfterInitialization) {
  SbPlayer player = ExpectPlayerCreation();
  Initialize(player);
  EXPECT_CALL(renderer_client_, OnError(HasStatusCode(PIPELINE_ERROR_DECODE)));

  player_error_cb_(player, context_, kSbPlayerErrorDecode, "decode failed");
  task_environment_.RunUntilIdle();
}

TEST_F(UrlPlayerRendererTest, AttachesCdmBeforePlayerCreation) {
  SbDrmSystem drm_system = new SbDrmSystemPrivate();
  FakeCdmContext cdm_context(drm_system);
  base::MockOnceCallback<void(bool)> cdm_cb;
  EXPECT_CALL(cdm_cb, Run(true));
  renderer_->SetCdm(&cdm_context, cdm_cb.Get());

  SbPlayer player = ExpectPlayerCreation();
  EXPECT_CALL(mock_sbplayer_interface_,
              SetUrlPlayerDrmSystem(player, drm_system));
  Initialize(player);
  delete drm_system;
}

TEST_F(UrlPlayerRendererTest, AttachesCdmAfterPlayerCreationAndRejectsSwitch) {
  SbPlayer player = ExpectPlayerCreation();
  Initialize(player);

  SbDrmSystem drm_system = new SbDrmSystemPrivate();
  FakeCdmContext cdm_context(drm_system);
  base::MockOnceCallback<void(bool)> cdm_cb;
  {
    InSequence sequence;
    EXPECT_CALL(mock_sbplayer_interface_,
                SetUrlPlayerDrmSystem(player, drm_system));
    EXPECT_CALL(cdm_cb, Run(true));
  }
  renderer_->SetCdm(&cdm_context, cdm_cb.Get());

  base::MockOnceCallback<void(bool)> replacement_cb;
  EXPECT_CALL(replacement_cb, Run(false));
  renderer_->SetCdm(&cdm_context, replacement_cb.Get());
  delete drm_system;
}

}  // namespace
}  // namespace media
