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

#include "media/mojo/services/starboard/url_player_renderer_wrapper.h"

#include <memory>
#include <string>

#include "base/functional/callback_helpers.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "cobalt/media/service/video_geometry_setter_service.h"
#include "media/base/media_util.h"
#include "media/base/mock_filters.h"
#include "media/base/test_helpers.h"
#include "media/starboard/sbplayer_interface.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

struct SbPlayerPrivate {};

namespace media {
namespace {

using ::base::test::RunOnceCallback;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrEq;
using ::testing::StrictMock;

constexpr char kSourceUrl[] = "https://example.test/video.m3u8";

class MockUrlPlayerRenderer : public UrlPlayerRenderer {
 public:
  explicit MockUrlPlayerRenderer(
      const scoped_refptr<base::SequencedTaskRunner>& task_runner)
      : UrlPlayerRenderer(task_runner,
                          std::make_unique<NullMediaLog>(),
                          base::UnguessableToken::Create(),
                          gfx::Size()) {}

  void Initialize(MediaResource* media_resource,
                  RendererClient* client,
                  PipelineStatusCallback init_cb) override {
    OnInitialize(media_resource, client, init_cb);
  }
  MOCK_METHOD(void,
              OnInitialize,
              (MediaResource*, RendererClient*, PipelineStatusCallback&));
  MOCK_METHOD(void, SetCdm, (CdmContext*, CdmAttachedCB), (override));
  MOCK_METHOD(void,
              SetLatencyHint,
              (std::optional<base::TimeDelta>),
              (override));
  MOCK_METHOD(void, Flush, (base::OnceClosure), (override));
  MOCK_METHOD(void, StartPlayingFrom, (base::TimeDelta), (override));
  MOCK_METHOD(void, SetPlaybackRate, (double), (override));
  MOCK_METHOD(void, SetVolume, (float), (override));
  MOCK_METHOD(base::TimeDelta, GetMediaTime, (), (override));
};

class MockStarboardRendererClientExtension
    : public mojom::StarboardRendererClientExtension {
 public:
  MOCK_METHOD(void, PaintVideoHoleFrame, (const gfx::Size&), (override));
  MOCK_METHOD(void,
              UpdateStarboardRenderingMode,
              (StarboardRenderingMode),
              (override));
  MOCK_METHOD(void, GetSbWindowHandle, (), (override));
  MOCK_METHOD(void, OnDurationChange, (base::TimeDelta), (override));
  MOCK_METHOD(void,
              OnBufferedTimeRangesChange,
              (base::TimeDelta, base::TimeDelta),
              (override));
};

class MockSbPlayerInterface : public SbPlayerInterface {
 public:
  MOCK_METHOD(SbPlayer,
              Create,
              (SbWindow,
               const SbPlayerCreationParam*,
               SbPlayerDeallocateSampleFunc,
               SbPlayerDecoderStatusFunc,
               SbPlayerStatusFunc,
               SbPlayerErrorFunc,
               void*,
               SbDecodeTargetGraphicsContextProvider*),
              (override));
  MOCK_METHOD(SbPlayerOutputMode,
              GetPreferredOutputMode,
              (const SbPlayerCreationParam*),
              (override));
  void Destroy(SbPlayer player) override { delete player; }
  MOCK_METHOD(void, Seek, (SbPlayer, base::TimeDelta, int), (override));
  MOCK_METHOD(void,
              WriteSamples,
              (SbPlayer, SbMediaType, const SbPlayerSampleInfo*, int),
              (override));
  MOCK_METHOD(int,
              GetMaximumNumberOfSamplesPerWrite,
              (SbPlayer, SbMediaType),
              (override));
  MOCK_METHOD(void, WriteEndOfStream, (SbPlayer, SbMediaType), (override));
  MOCK_METHOD(void, SetBounds, (SbPlayer, int, int, int, int, int), (override));
  MOCK_METHOD(bool, SetPlaybackRate, (SbPlayer, double), (override));
  MOCK_METHOD(void, SetVolume, (SbPlayer, double), (override));
  MOCK_METHOD(void, GetInfo, (SbPlayer, SbPlayerInfo*), (override));
  MOCK_METHOD(SbDecodeTarget, GetCurrentFrame, (SbPlayer), (override));
  MOCK_METHOD(SbPlayer,
              CreateUrlPlayer,
              (const char*,
               SbWindow,
               SbPlayerStatusFunc,
               SbPlayerEncryptedMediaInitDataEncounteredCB,
               SbPlayerErrorFunc,
               void*),
              (override));
  MOCK_METHOD(void, SetUrlPlayerDrmSystem, (SbPlayer, SbDrmSystem), (override));
  MOCK_METHOD(bool,
              GetUrlPlayerOutputModeSupported,
              (SbPlayerOutputMode),
              (override));
  MOCK_METHOD(void,
              GetUrlPlayerExtraInfo,
              (SbPlayer, SbUrlPlayerExtraInfo*),
              (override));
  MOCK_METHOD(bool,
              GetAudioConfiguration,
              (SbPlayer, int, SbMediaAudioConfiguration*),
              (override));
};

class UrlPlayerRendererWrapperTest : public testing::Test {
 protected:
  UrlPlayerRendererWrapperTest()
      : mock_renderer_(std::make_unique<StrictMock<MockUrlPlayerRenderer>>(
            task_environment_.GetMainThreadTaskRunner())),
        client_extension_receiver_(&client_extension_) {
    mojo::PendingRemote<mojom::StarboardRendererClientExtension>
        client_extension_remote;
    client_extension_receiver_.Bind(
        client_extension_remote.InitWithNewPipeAndPassReceiver());

    auto media_log_remote = media_log_receiver_.InitWithNewPipeAndPassRemote();
    auto renderer_extension_receiver =
        renderer_extension_.BindNewPipeAndPassReceiver();
    UrlPlayerRendererTraits traits(
        task_environment_.GetMainThreadTaskRunner(),
        std::move(media_log_remote), &video_geometry_setter_service_,
        base::UnguessableToken::Create(), gfx::Size(),
        std::move(renderer_extension_receiver),
        std::move(client_extension_remote));
    renderer_wrapper_ =
        std::make_unique<UrlPlayerRendererWrapper>(std::move(traits));
    renderer_wrapper_->SetRendererForTesting(mock_renderer_.get());
  }

  ~UrlPlayerRendererWrapperTest() override {
    renderer_wrapper_.reset();
    mock_renderer_.reset();
  }

  base::test::TaskEnvironment task_environment_;
  cobalt::media::VideoGeometrySetterService video_geometry_setter_service_;
  std::unique_ptr<StrictMock<MockUrlPlayerRenderer>> mock_renderer_;
  StrictMock<MockStarboardRendererClientExtension> client_extension_;
  mojo::Receiver<mojom::StarboardRendererClientExtension>
      client_extension_receiver_;
  mojo::Remote<mojom::StarboardRendererExtension> renderer_extension_;
  mojo::PendingReceiver<mojom::MediaLog> media_log_receiver_;
  std::unique_ptr<UrlPlayerRendererWrapper> renderer_wrapper_;
  NiceMock<MockMediaResource> media_resource_;
  NiceMock<MockRendererClient> renderer_client_;
};

TEST_F(UrlPlayerRendererWrapperTest, InitializesAfterGeometrySubscription) {
  base::MockOnceCallback<void(PipelineStatus)> init_cb;
  EXPECT_CALL(*mock_renderer_,
              OnInitialize(&media_resource_, &renderer_client_, _))
      .WillOnce(RunOnceCallback<2>(PIPELINE_OK));
  EXPECT_CALL(init_cb, Run(HasStatusCode(PIPELINE_OK)));

  renderer_wrapper_->Initialize(&media_resource_, &renderer_client_,
                                init_cb.Get());
  task_environment_.RunUntilIdle();
}

TEST_F(UrlPlayerRendererWrapperTest, DelegatesRendererMethods) {
  const base::TimeDelta start_time = base::Seconds(5);
  const base::TimeDelta media_time = base::Seconds(42);
  base::MockOnceClosure flush_cb;
  base::MockOnceCallback<void(bool)> cdm_cb;

  EXPECT_CALL(*mock_renderer_,
              SetLatencyHint(std::optional(base::Milliseconds(250))));
  renderer_wrapper_->SetLatencyHint(base::Milliseconds(250));
  EXPECT_CALL(*mock_renderer_, StartPlayingFrom(start_time));
  renderer_wrapper_->StartPlayingFrom(start_time);
  EXPECT_CALL(*mock_renderer_, SetPlaybackRate(1.25));
  renderer_wrapper_->SetPlaybackRate(1.25);
  EXPECT_CALL(*mock_renderer_, SetVolume(0.5f));
  renderer_wrapper_->SetVolume(0.5f);
  EXPECT_CALL(*mock_renderer_, GetMediaTime()).WillOnce(Return(media_time));
  EXPECT_EQ(renderer_wrapper_->GetMediaTime(), media_time);
  EXPECT_CALL(*mock_renderer_, Flush(_))
      .WillOnce(Invoke([](base::OnceClosure cb) { std::move(cb).Run(); }));
  EXPECT_CALL(flush_cb, Run());
  renderer_wrapper_->Flush(flush_cb.Get());
  EXPECT_CALL(*mock_renderer_, SetCdm(nullptr, _))
      .WillOnce(Invoke([](CdmContext*, Renderer::CdmAttachedCB cb) {
        std::move(cb).Run(true);
      }));
  EXPECT_CALL(cdm_cb, Run(true));
  renderer_wrapper_->SetCdm(nullptr, cdm_cb.Get());
}

TEST_F(UrlPlayerRendererWrapperTest, ReportsUrlPlayerTypeAndNoGpuFrame) {
  EXPECT_EQ(renderer_wrapper_->GetRendererType(), RendererType::kUrlPlayer);

  auto command_buffer_id = mojom::CommandBufferId::New();
  command_buffer_id->channel_token = base::UnguessableToken::Create();
  command_buffer_id->route_id = 7;
  renderer_wrapper_->OnGpuChannelTokenReady(std::move(command_buffer_id));

  base::MockCallback<UrlPlayerRendererWrapper::GetCurrentVideoFrameCallback>
      frame_cb;
  EXPECT_CALL(frame_cb, Run(testing::IsNull()));
  renderer_wrapper_->GetCurrentVideoFrame(frame_cb.Get());
}

TEST_F(UrlPlayerRendererWrapperTest, InitializeWithUrlPassesNullMediaResource) {
  base::MockOnceCallback<void(PipelineStatus)> init_cb;
  // InitializeWithUrl should call Initialize with nullptr media_resource.
  EXPECT_CALL(*mock_renderer_, OnInitialize(nullptr, &renderer_client_, _))
      .WillOnce(RunOnceCallback<2>(PIPELINE_OK));
  EXPECT_CALL(init_cb, Run(HasStatusCode(PIPELINE_OK)));

  renderer_wrapper_->InitializeWithUrl(&renderer_client_, GURL(kSourceUrl),
                                       init_cb.Get());
  task_environment_.RunUntilIdle();
}

class UrlPlayerRendererWrapperIntegrationTest : public testing::Test {
 protected:
  UrlPlayerRendererWrapperIntegrationTest()
      : overlay_plane_id_(base::UnguessableToken::Create()),
        client_extension_receiver_(&client_extension_) {
    mojo::PendingRemote<mojom::StarboardRendererClientExtension>
        client_extension_remote;
    client_extension_receiver_.Bind(
        client_extension_remote.InitWithNewPipeAndPassReceiver());

    auto media_log_remote = media_log_receiver_.InitWithNewPipeAndPassRemote();
    auto renderer_extension_receiver =
        renderer_extension_.BindNewPipeAndPassReceiver();
    UrlPlayerRendererTraits traits(
        task_environment_.GetMainThreadTaskRunner(),
        std::move(media_log_remote), &video_geometry_setter_service_,
        overlay_plane_id_, gfx::Size(), std::move(renderer_extension_receiver),
        std::move(client_extension_remote));
    renderer_wrapper_ =
        std::make_unique<UrlPlayerRendererWrapper>(std::move(traits));
    renderer_wrapper_->GetRenderer()->SetSbPlayerInterfaceForTesting(
        &mock_sbplayer_interface_);

    ON_CALL(mock_sbplayer_interface_, GetUrlPlayerOutputModeSupported(_))
        .WillByDefault(Invoke([](SbPlayerOutputMode mode) {
          return mode == kSbPlayerOutputModePunchOut;
        }));
    ON_CALL(mock_sbplayer_interface_, SetPlaybackRate(_, _))
        .WillByDefault(Return(true));
  }

  base::test::TaskEnvironment task_environment_;
  cobalt::media::VideoGeometrySetterService video_geometry_setter_service_;
  const base::UnguessableToken overlay_plane_id_;
  NiceMock<MockSbPlayerInterface> mock_sbplayer_interface_;
  StrictMock<MockStarboardRendererClientExtension> client_extension_;
  mojo::Receiver<mojom::StarboardRendererClientExtension>
      client_extension_receiver_;
  mojo::Remote<mojom::StarboardRendererExtension> renderer_extension_;
  mojo::PendingReceiver<mojom::MediaLog> media_log_receiver_;
  std::unique_ptr<UrlPlayerRendererWrapper> renderer_wrapper_;
  NiceMock<MockMediaResource> media_resource_;
  NiceMock<MockRendererClient> renderer_client_;
};

TEST_F(UrlPlayerRendererWrapperIntegrationTest,
       ForwardsUrlWindowGeometryAndClientCallbacks) {
  constexpr uint64_t kWindowHandle = 0x1234;
  SbWindow window = reinterpret_cast<SbWindow>(kWindowHandle);
  SbPlayer player = new SbPlayerPrivate();
  SbPlayerStatusFunc player_status_cb = nullptr;
  void* player_context = nullptr;

  base::MockOnceCallback<void(PipelineStatus)> init_cb;
  EXPECT_CALL(client_extension_, GetSbWindowHandle());
  renderer_wrapper_->InitializeWithUrl(&renderer_client_, GURL(kSourceUrl),
                                       init_cb.Get());
  task_environment_.RunUntilIdle();

  renderer_wrapper_->OnVideoGeometryChange(
      gfx::RectF(10.0f, 20.0f, 640.0f, 360.0f), gfx::OVERLAY_TRANSFORM_NONE);
  EXPECT_CALL(mock_sbplayer_interface_,
              CreateUrlPlayer(StrEq(kSourceUrl), window, _, _, _, _))
      .WillOnce(DoAll(SaveArg<2>(&player_status_cb),
                      SaveArg<5>(&player_context), Return(player)));
  EXPECT_CALL(mock_sbplayer_interface_, SetBounds(player, _, 10, 20, 640, 360));
  EXPECT_CALL(client_extension_,
              UpdateStarboardRenderingMode(StarboardRenderingMode::kPunchOut));
  renderer_extension_->OnSbWindowHandleReady(kWindowHandle);
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(player_status_cb);
  EXPECT_CALL(init_cb, Run(HasStatusCode(PIPELINE_OK)));
  player_status_cb(player, player_context, kSbPlayerStateInitialized,
                   SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();

  EXPECT_CALL(mock_sbplayer_interface_, GetInfo(player, _))
      .WillRepeatedly(Invoke([](SbPlayer, SbPlayerInfo* info) {
        *info = {};
        info->frame_width = 1920;
        info->frame_height = 1080;
      }));
  EXPECT_CALL(renderer_client_,
              OnVideoNaturalSizeChange(gfx::Size(1920, 1080)));
  EXPECT_CALL(renderer_client_,
              OnBufferingStateChange(BUFFERING_HAVE_ENOUGH,
                                     BUFFERING_CHANGE_REASON_UNKNOWN));
  EXPECT_CALL(client_extension_, PaintVideoHoleFrame(gfx::Size(1920, 1080)));
  // Zero duration from GetInfo maps to kInfiniteDuration (live HLS stream).
  EXPECT_CALL(client_extension_, OnDurationChange(kInfiniteDuration));
  player_status_cb(player, player_context, kSbPlayerStatePresenting,
                   SB_PLAYER_INITIAL_TICKET);
  task_environment_.RunUntilIdle();
}

}  // namespace
}  // namespace media
