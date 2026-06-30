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

#include "media/mojo/clients/starboard/starboard_renderer_client.h"

#include "base/functional/callback_helpers.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "media/base/fake_demuxer_stream.h"
#include "media/base/media_switches.h"
#include "media/base/mock_media_log.h"
#include "media/base/test_helpers.h"
#include "media/base/video_renderer_sink.h"
#include "media/media_buildflags.h"
#include "media/mojo/mojom/renderer.mojom.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "media/renderers/video_overlay_factory.h"
#include "media/video/mock_gpu_video_accelerator_factories.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace media {

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::StrictMock;

namespace {

class FakeMojomRenderer : public mojom::Renderer {
 public:
  FakeMojomRenderer() = default;
  ~FakeMojomRenderer() override = default;

  void Initialize(
      mojo::PendingAssociatedRemote<mojom::RendererClient>,
      std::optional<std::vector<mojo::PendingRemote<mojom::DemuxerStream>>>,
      InitializeCallback cb) override {
    std::move(cb).Run(true);
  }
  void InitializeWithBypassBridge(
      mojo::PendingAssociatedRemote<mojom::RendererClient>,
      uint32_t bypass_bridge_id,
      InitializeWithBypassBridgeCallback cb) override {
    std::move(cb).Run(true);
  }
#if BUILDFLAG(USE_STARBOARD_URL_PLAYER)
  void InitializeWithUrl(mojo::PendingAssociatedRemote<mojom::RendererClient>,
                         const GURL& source_url,
                         InitializeWithUrlCallback cb) override {
    std::move(cb).Run(true);
  }
#endif  // BUILDFLAG(USE_STARBOARD_URL_PLAYER)
  MOCK_METHOD1(Flush, void(FlushCallback));
  void StartPlayingFrom(base::TimeDelta time) override {}
  MOCK_METHOD1(SetPlaybackRate, void(double));
  void SetVolume(float volume) override {}
  MOCK_METHOD2(SetCdm,
               void(const absl::optional<base::UnguessableToken>&,
                    SetCdmCallback));
  void SetLatencyHint(std::optional<::base::TimeDelta> latency_hint) override {}
};

class FakeStarboardRendererExtension
    : public mojom::StarboardRendererExtension {
 public:
  FakeStarboardRendererExtension() = default;
  ~FakeStarboardRendererExtension() override = default;

  MOCK_METHOD1(GetCurrentVideoFrame, void(GetCurrentVideoFrameCallback cb));
  void OnSbWindowHandleReady(uint64_t sb_window_handle) override {}
#if BUILDFLAG(IS_ANDROID)
  MOCK_METHOD1(OnOverlayInfoChanged, void(const OverlayInfo& overlay_info));
#endif  // BUILDFLAG(IS_ANDROID)
  void OnGpuChannelTokenReady(
      mojom::CommandBufferIdPtr command_buffer_id) override {}
};

class MockVideoRendererSink : public VideoRendererSink {
 public:
  MockVideoRendererSink() = default;
  ~MockVideoRendererSink() override = default;

  MOCK_METHOD1(Start, void(RenderCallback*));
  MOCK_METHOD0(Stop, void());
  MOCK_METHOD2(PaintSingleFrame, void(scoped_refptr<VideoFrame>, bool));
};

class MockRendererClientStarboard : public RendererClient {
 public:
  MockRendererClientStarboard() = default;
  ~MockRendererClientStarboard() = default;

  MOCK_METHOD1(OnError, void(PipelineStatus));
  MOCK_METHOD1(OnFallback, void(PipelineStatus));
  void OnEnded() override {}
  MOCK_METHOD1(OnStatisticsUpdate, void(const PipelineStatistics&));
  MOCK_METHOD2(OnBufferingStateChange,
               void(BufferingState, BufferingStateChangeReason));
  MOCK_METHOD1(OnWaiting, void(WaitingReason));
  MOCK_METHOD1(OnAudioConfigChange, void(const AudioDecoderConfig&));
  MOCK_METHOD1(OnVideoConfigChange, void(const VideoDecoderConfig&));
  MOCK_METHOD1(OnVideoNaturalSizeChange, void(const gfx::Size&));
  MOCK_METHOD1(OnVideoOpacityChange, void(bool));
  MOCK_METHOD1(OnVideoFrameRateChange, void(absl::optional<int>));
  MOCK_METHOD0(IsVideoStreamAvailable, bool());
};

class StarboardRendererClientTest : public ::testing::Test {
 protected:
  StarboardRendererClientTest() = default;
  ~StarboardRendererClientTest() override = default;

  void SetUp() override {
    mock_gpu_factories_ =
        std::make_unique<NiceMock<MockGpuVideoAcceleratorFactories>>(nullptr);
    ON_CALL(*mock_gpu_factories_, GetChannelToken(_))
        .WillByDefault(
            Invoke([](base::OnceCallback<void(const base::UnguessableToken&)>
                          callback) {
              std::move(callback).Run(base::UnguessableToken());
            }));

    media_resource_ = std::make_unique<FakeMediaResource>(3, 9, false);
  }

  void InitializeStarboardRendererClient(bool with_gpu_factories = true) {
    mojo::PendingRemote<mojom::Renderer> renderer_remote;
    mojo::MakeSelfOwnedReceiver(
        std::make_unique<FakeMojomRenderer>(),
        renderer_remote.InitWithNewPipeAndPassReceiver());

    mojo::PendingRemote<mojom::StarboardRendererExtension>
        starboard_renderer_extensions_remote;
    mojo::MakeSelfOwnedReceiver(
        std::make_unique<FakeStarboardRendererExtension>(),
        starboard_renderer_extensions_remote.InitWithNewPipeAndPassReceiver());

    mojo::PendingRemote<media::mojom::StarboardRendererClientExtension>
        client_extension_remote;
    auto client_extension_receiver =
        client_extension_remote.InitWithNewPipeAndPassReceiver();
    auto mojo_renderer = std::make_unique<MojoRenderer>(
        task_environment_.GetMainThreadTaskRunner(),
        /*video_overlay_factory=*/nullptr,
        /*video_renderer_sink=*/nullptr, std::move(renderer_remote));
    auto overlay_factory = std::make_unique<VideoOverlayFactory>();
    starboard_renderer_client_ = std::make_unique<StarboardRendererClient>(
        task_environment_.GetMainThreadTaskRunner(), media_log_.Clone(),
        std::move(mojo_renderer), std::move(overlay_factory),
        &mock_video_renderer_sink_,
        std::move(starboard_renderer_extensions_remote),
        std::move(client_extension_receiver),
        /*get_sb_window_handle_callback=*/
        base::BindRepeating([]() -> uint64_t { return 0; }),
        with_gpu_factories ? mock_gpu_factories_.get() : nullptr
#if BUILDFLAG(IS_ANDROID)
        ,
        /*request_overlay_info_cb=*/base::DoNothing()
#endif  // BUILDFLAG(IS_ANDROID)
    );
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  MockMediaLog media_log_;
  MockVideoRendererSink mock_video_renderer_sink_;
  std::unique_ptr<NiceMock<MockGpuVideoAcceleratorFactories>>
      mock_gpu_factories_;
  NiceMock<MockRendererClientStarboard> renderer_client_;
  base::MockOnceCallback<void(PipelineStatus)> renderer_init_cb_;
  std::unique_ptr<FakeMediaResource> media_resource_;
  std::unique_ptr<StarboardRendererClient> starboard_renderer_client_;
};

TEST_F(StarboardRendererClientTest, CreateAndDestroy) {
  // This is expected to not crash.
}

TEST_F(StarboardRendererClientTest, InitializeWithGpuFactories) {
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  InitializeStarboardRendererClient();
  starboard_renderer_client_->Initialize(
      media_resource_.get(), &renderer_client_, renderer_init_cb_.Get());
  starboard_renderer_client_->UpdateStarboardRenderingMode(
      StarboardRenderingMode::kPunchOut);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererClientTest, InitializeWithoutGpuFactories) {
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  InitializeStarboardRendererClient(false);
  starboard_renderer_client_->Initialize(
      media_resource_.get(), &renderer_client_, renderer_init_cb_.Get());
  starboard_renderer_client_->UpdateStarboardRenderingMode(
      StarboardRenderingMode::kPunchOut);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererClientTest, PunchOutModeDoesNotStartSink) {
  InitializeStarboardRendererClient();
  starboard_renderer_client_->Initialize(media_resource_.get(),
                                         &renderer_client_, base::DoNothing());
  EXPECT_CALL(mock_video_renderer_sink_, Start(_)).Times(0);
  EXPECT_CALL(mock_video_renderer_sink_, Stop()).Times(0);
  starboard_renderer_client_->UpdateStarboardRenderingMode(
      StarboardRenderingMode::kPunchOut);
  task_environment_.RunUntilIdle();

  starboard_renderer_client_->StartPlayingFrom(base::Seconds(0));
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererClientTest, PunchOutModePaintsVideoHole) {
  InitializeStarboardRendererClient();
  starboard_renderer_client_->Initialize(media_resource_.get(),
                                         &renderer_client_, base::DoNothing());
  starboard_renderer_client_->UpdateStarboardRenderingMode(
      StarboardRenderingMode::kPunchOut);
  task_environment_.RunUntilIdle();

  const gfx::Size kVideoSize(640, 360);
  EXPECT_CALL(mock_video_renderer_sink_, PaintSingleFrame(_, false));
  starboard_renderer_client_->PaintVideoHoleFrame(kVideoSize);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererClientTest, DecodeToTextureModeStartsAndStopsSink) {
  InitializeStarboardRendererClient();
  starboard_renderer_client_->Initialize(media_resource_.get(),
                                         &renderer_client_, base::DoNothing());
  EXPECT_CALL(mock_video_renderer_sink_,
              Start(starboard_renderer_client_.get()));
  starboard_renderer_client_->UpdateStarboardRenderingMode(
      StarboardRenderingMode::kDecodeToTexture);
  starboard_renderer_client_->StartPlayingFrom(base::Seconds(0));
  task_environment_.RunUntilIdle();

  EXPECT_CALL(mock_video_renderer_sink_, Stop());
  starboard_renderer_client_->OnEnded();
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererClientTest,
       DecodeToTextureModeWithoutStartingPlayback) {
  InitializeStarboardRendererClient();
  starboard_renderer_client_->Initialize(media_resource_.get(),
                                         &renderer_client_, base::DoNothing());
  EXPECT_CALL(mock_video_renderer_sink_, Start(_)).Times(0);
  starboard_renderer_client_->UpdateStarboardRenderingMode(
      StarboardRenderingMode::kDecodeToTexture);
  task_environment_.RunUntilIdle();

  EXPECT_CALL(mock_video_renderer_sink_, Stop()).Times(0);
  starboard_renderer_client_->OnEnded();
  task_environment_.RunUntilIdle();
}

}  // namespace

}  // namespace media
