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

#include "media/mojo/clients/starboard/url_player_renderer_client.h"

#include <memory>
#include <optional>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "media/base/media_resource.h"
#include "media/base/media_util.h"
#include "media/base/mock_filters.h"
#include "media/base/mock_video_renderer_sink.h"
#include "media/base/test_helpers.h"
#include "media/base/video_frame.h"
#include "media/mojo/clients/mojo_renderer.h"
#include "media/mojo/mojom/renderer.mojom.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "media/renderers/video_overlay_factory.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace media {
namespace {

using ::testing::_;
using ::testing::StrictMock;

constexpr char kSourceUrl[] = "https://example.test/video.m3u8";

class UrlMediaResource final : public MediaResource {
 public:
  explicit UrlMediaResource(GURL url) : url_(std::move(url)) {}

  std::vector<DemuxerStream*> GetAllStreams() override { return {}; }

  GURL GetMediaUrl() const override { return url_; }

 private:
  GURL url_;
};

class FakeMojomRenderer final : public mojom::Renderer {
 public:
  void Initialize(
      mojo::PendingAssociatedRemote<mojom::RendererClient> client,
      std::optional<std::vector<mojo::PendingRemote<mojom::DemuxerStream>>>
          streams,
      InitializeCallback cb) override {
    ++initialize_count_;
    initialize_cb_ = std::move(cb);
  }

  void InitializeWithUrl(
      mojo::PendingAssociatedRemote<mojom::RendererClient> client,
      const GURL& source_url,
      InitializeWithUrlCallback cb) override {
    ++initialize_with_url_count_;
    source_url_ = source_url;
    initialize_with_url_cb_ = std::move(cb);
  }

  void Flush(FlushCallback cb) override { std::move(cb).Run(); }
  void StartPlayingFrom(base::TimeDelta time) override {}
  void SetPlaybackRate(double playback_rate) override {}
  void SetVolume(float volume) override {}
  void SetCdm(const std::optional<base::UnguessableToken>& cdm_id,
              SetCdmCallback cb) override {
    std::move(cb).Run(true);
  }
  void SetLatencyHint(std::optional<base::TimeDelta> latency_hint) override {}

  void CompleteInitialize(bool success) {
    if (initialize_cb_) {
      std::move(initialize_cb_).Run(success);
      return;
    }

    ASSERT_TRUE(initialize_with_url_cb_);
    std::move(initialize_with_url_cb_).Run(success);
  }

  int initialize_count() const { return initialize_count_; }
  int initialize_with_url_count() const { return initialize_with_url_count_; }
  int initialize_with_bypass_bridge_count() const {
    return initialize_with_bypass_bridge_count_;
  }
  const GURL& source_url() const { return source_url_; }

 private:
  int initialize_count_ = 0;
  int initialize_with_url_count_ = 0;
  int initialize_with_bypass_bridge_count_ = 0;
  GURL source_url_;
  InitializeCallback initialize_cb_;
  InitializeWithUrlCallback initialize_with_url_cb_;

  void InitializeWithBypassBridge(
      mojo::PendingAssociatedRemote<mojom::RendererClient> client,
      uint32_t bypass_bridge_id,
      InitializeWithBypassBridgeCallback cb) override {
    ++initialize_with_bypass_bridge_count_;
    std::move(cb).Run(false);
  }
};

class FakeRendererExtension final : public mojom::StarboardRendererExtension {
 public:
  void GetCurrentVideoFrame(GetCurrentVideoFrameCallback cb) override {
    std::move(cb).Run(nullptr);
  }
  void OnSbWindowHandleReady(uint64_t sb_window_handle) override {}
  void OnGpuChannelTokenReady(
      mojom::CommandBufferIdPtr command_buffer_id) override {}
};

class UrlPlayerRendererClientTest : public testing::Test {
 protected:
  UrlPlayerRendererClientTest() {
    mojo::PendingRemote<mojom::Renderer> renderer_remote;
    renderer_receiver_.Bind(renderer_remote.InitWithNewPipeAndPassReceiver());

    mojo::PendingRemote<mojom::StarboardRendererExtension> renderer_extension;
    renderer_extension_receiver_.Bind(
        renderer_extension.InitWithNewPipeAndPassReceiver());

    client_ = std::make_unique<UrlPlayerRendererClient>(
        task_environment_.GetMainThreadTaskRunner(),
        std::make_unique<NullMediaLog>(),
        std::make_unique<MojoRenderer>(
            task_environment_.GetMainThreadTaskRunner(),
            /*video_overlay_factory=*/nullptr,
            /*video_renderer_sink=*/nullptr, std::move(renderer_remote)),
        std::make_unique<VideoOverlayFactory>(), &video_renderer_sink_,
        std::move(renderer_extension),
        client_extension_remote_.BindNewPipeAndPassReceiver(),
        base::NullCallback());
  }

  void InitializeClient(PipelineStatusCallback init_cb) {
    client_->Initialize(&media_resource_, &renderer_client_,
                        std::move(init_cb));
    task_environment_.RunUntilIdle();
  }

  base::test::TaskEnvironment task_environment_;
  FakeMojomRenderer fake_renderer_;
  mojo::Receiver<mojom::Renderer> renderer_receiver_{&fake_renderer_};
  FakeRendererExtension fake_renderer_extension_;
  mojo::Receiver<mojom::StarboardRendererExtension>
      renderer_extension_receiver_{&fake_renderer_extension_};
  mojo::Remote<mojom::StarboardRendererClientExtension>
      client_extension_remote_;
  StrictMock<MockVideoRendererSink> video_renderer_sink_;
  StrictMock<MockRendererClient> renderer_client_;
  UrlMediaResource media_resource_{GURL(kSourceUrl)};
  std::unique_ptr<UrlPlayerRendererClient> client_;
};

TEST_F(UrlPlayerRendererClientTest, CompletesAfterMojoInitAndPunchOutMode) {
  base::MockOnceCallback<void(PipelineStatus)> init_cb;
  EXPECT_CALL(init_cb, Run(_)).Times(0);

  InitializeClient(init_cb.Get());
  EXPECT_EQ(fake_renderer_.initialize_count(), 0);
  EXPECT_EQ(fake_renderer_.initialize_with_url_count(), 1);
  EXPECT_EQ(fake_renderer_.source_url(), GURL(kSourceUrl));

  fake_renderer_.CompleteInitialize(true);
  task_environment_.RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&init_cb);

  EXPECT_CALL(init_cb, Run(HasStatusCode(PIPELINE_OK)));
  client_->UpdateStarboardRenderingMode(StarboardRenderingMode::kPunchOut);
  task_environment_.RunUntilIdle();
}

TEST_F(UrlPlayerRendererClientTest, CompletesWhenPunchOutModeArrivesFirst) {
  base::MockOnceCallback<void(PipelineStatus)> init_cb;
  EXPECT_CALL(init_cb, Run(_)).Times(0);

  InitializeClient(init_cb.Get());
  client_->UpdateStarboardRenderingMode(StarboardRenderingMode::kPunchOut);
  task_environment_.RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&init_cb);

  EXPECT_CALL(init_cb, Run(HasStatusCode(PIPELINE_OK)));
  fake_renderer_.CompleteInitialize(true);
  task_environment_.RunUntilIdle();
}

TEST_F(UrlPlayerRendererClientTest, InvalidUrlFailsAfterRenderingModeArrives) {
  UrlMediaResource invalid_media_resource{GURL()};
  base::MockOnceCallback<void(PipelineStatus)> init_cb;

  // Invalid URL causes mojo init to fail internally, but the two-event
  // protocol waits for a rendering mode update before firing init_cb_.
  EXPECT_CALL(init_cb, Run(_)).Times(0);
  client_->Initialize(&invalid_media_resource, &renderer_client_,
                      init_cb.Get());
  task_environment_.RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&init_cb);

  EXPECT_EQ(fake_renderer_.initialize_count(), 0);
  EXPECT_EQ(fake_renderer_.initialize_with_bypass_bridge_count(), 0);

  // Rendering mode arrival completes init with the stored error.
  EXPECT_CALL(init_cb,
              Run(HasStatusCode(PIPELINE_ERROR_INITIALIZATION_FAILED)));
  client_->UpdateStarboardRenderingMode(StarboardRenderingMode::kPunchOut);
  task_environment_.RunUntilIdle();
}

TEST_F(UrlPlayerRendererClientTest, DisconnectDuringInitFailsInit) {
  base::MockOnceCallback<void(PipelineStatus)> init_cb;
  EXPECT_CALL(init_cb, Run(HasStatusCode(PIPELINE_ERROR_DISCONNECTED)));

  InitializeClient(init_cb.Get());
  renderer_extension_receiver_.reset();
  task_environment_.RunUntilIdle();
}

// URL player only supports punch-out mode. The rendering mode is determined
// at the Starboard layer by SbUrlPlayerOutputModeSupported() which returns
// true only for kSbPlayerOutputModePunchOut. DTT and invalid modes cannot
// arrive in production, so UpdateStarboardRenderingMode() ignores them.
// See the protocol comment in url_player_renderer_client.cc.
TEST_F(UrlPlayerRendererClientTest, IgnoresDecodeToTextureDuringInit) {
  base::MockOnceCallback<void(PipelineStatus)> init_cb;
  EXPECT_CALL(init_cb, Run(_)).Times(0);

  InitializeClient(init_cb.Get());
  // DTT is ignored by URL player. init_cb_ should not fire.
  client_->UpdateStarboardRenderingMode(
      StarboardRenderingMode::kDecodeToTexture);
  task_environment_.RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&init_cb);

  // Mojo init completing afterwards should also not fire init_cb_
  // because rendering_mode_ is not kPunchOut.
  EXPECT_CALL(init_cb, Run(_)).Times(0);
  fake_renderer_.CompleteInitialize(true);
  task_environment_.RunUntilIdle();
}

TEST_F(UrlPlayerRendererClientTest, IgnoresInvalidRenderingModeDuringInit) {
  base::MockOnceCallback<void(PipelineStatus)> init_cb;
  EXPECT_CALL(init_cb, Run(_)).Times(0);

  InitializeClient(init_cb.Get());
  // Invalid mode is ignored. init_cb_ should not fire.
  client_->UpdateStarboardRenderingMode(StarboardRenderingMode::kInvalid);
  task_environment_.RunUntilIdle();
}

}  // namespace
}  // namespace media
