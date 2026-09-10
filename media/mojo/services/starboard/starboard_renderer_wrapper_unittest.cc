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

#include "media/mojo/services/starboard/starboard_renderer_wrapper.h"

#include <memory>
#include <optional>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "cobalt/media/service/video_geometry_setter_service.h"
#include "media/base/media_util.h"
#include "media/base/mock_filters.h"
#include "media/base/test_helpers.h"
#include "media/gpu/starboard/starboard_gpu_factory_impl.h"
#include "media/mojo/common/starboard/mojo_renderer_bypass_bridge.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "starboard/decode_target.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::base::test::RunOnceCallback;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

namespace media {

namespace {

class MockStarboardRenderer : public StarboardRenderer {
 public:
  MockStarboardRenderer(
      const scoped_refptr<base::SequencedTaskRunner>& task_runner,
      std::unique_ptr<MediaLog> media_log,
      const base::UnguessableToken& overlay_plane_id,
      TimeDelta audio_write_duration_local,
      TimeDelta audio_write_duration_remote,
      const std::string& max_video_capabilities,
      const StarboardRendererConfig::ExperimentalFeatures&
          experimental_features,
      const gfx::Size& viewport_size
#if BUILDFLAG(IS_ANDROID)
      ,
      const AndroidOverlayMojoFactoryCB android_overlay_factory_cb
#endif  // BUILDFLAG(IS_ANDROID)
      )
      : StarboardRenderer(task_runner,
                          std::move(media_log),
                          overlay_plane_id,
                          audio_write_duration_local,
                          audio_write_duration_remote,
                          max_video_capabilities,
                          experimental_features,
                          viewport_size
#if BUILDFLAG(IS_ANDROID)
                          ,
                          android_overlay_factory_cb
#endif  // BUILDFLAG(IS_ANDROID)
        ) {
  }

  MockStarboardRenderer(const MockStarboardRenderer&) = delete;
  MockStarboardRenderer& operator=(const MockStarboardRenderer&) = delete;

  ~MockStarboardRenderer() = default;

  // Renderer implementation.
  void Initialize(MediaResource* media_resource,
                  RendererClient* client,
                  PipelineStatusCallback init_cb) override {
    OnInitialize(media_resource, client, init_cb);
  }
  MOCK_METHOD3(OnInitialize,
               void(MediaResource* media_resource,
                    RendererClient* client,
                    PipelineStatusCallback& init_cb));
  MOCK_METHOD1(SetLatencyHint, void(std::optional<TimeDelta>));
  MOCK_METHOD1(SetPreservesPitch, void(bool));
  MOCK_METHOD1(SetWasPlayedWithUserActivation, void(bool));
  void Flush(base::OnceClosure flush_cb) override { OnFlush(flush_cb); }
  MOCK_METHOD1(OnFlush, void(base::OnceClosure& flush_cb));
  MOCK_METHOD1(StartPlayingFrom, void(base::TimeDelta timestamp));
  MOCK_METHOD1(SetPlaybackRate, void(double playback_rate));
  MOCK_METHOD1(SetVolume, void(float volume));
  MOCK_METHOD0(GetMediaTime, base::TimeDelta());
  MOCK_METHOD0(HasAudio, bool());
  MOCK_METHOD0(HasVideo, bool());
  void SetCdm(CdmContext* cdm_context, CdmAttachedCB cdm_attached_cb) override {
    OnSetCdm(cdm_context, cdm_attached_cb);
  }
  MOCK_METHOD2(OnSetCdm,
               void(CdmContext* cdm_context, CdmAttachedCB& cdm_attached_cb));
  MOCK_METHOD2(OnSelectedVideoTrackChanged,
               void(std::vector<DemuxerStream*>, base::OnceClosure));
  MOCK_METHOD2(OnSelectedAudioTracksChanged,
               void(std::vector<DemuxerStream*>, base::OnceClosure));
  RendererType GetRendererType() override { return RendererType::kTest; }

  base::WeakPtr<MockStarboardRenderer> AsWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  base::WeakPtrFactory<MockStarboardRenderer> weak_ptr_factory_{this};
};

class MockStarboardGpuFactory : public StarboardGpuFactory {
 public:
  MockStarboardGpuFactory() = default;

  MockStarboardGpuFactory(const MockStarboardGpuFactory&) = delete;
  MockStarboardGpuFactory& operator=(const MockStarboardGpuFactory&) = delete;

  ~MockStarboardGpuFactory() = default;

  void Initialize(base::UnguessableToken channel_token,
                  int32_t route_id,
                  base::OnceClosure callback) override {
    std::move(callback).Run();
  }

  void RunWithGlesContext(base::OnceClosure callback,
                          base::WaitableEvent* done_event) override {
    if (done_event) {
      done_event->Signal();
    }
  }

  void CreateImageOnGpu(const gfx::Size& coded_size,
                        const gfx::ColorSpace& color_space,
                        viz::SharedImageFormat format,
                        scoped_refptr<gpu::ClientSharedImage>& shared_image,
                        const std::vector<uint32_t>& texture_service_ids,
                        const std::vector<uint32_t>& texture_targets,
                        uint64_t decode_target,
#if BUILDFLAG(IS_ANDROID)
                        scoped_refptr<gpu::RefCountedLock> drdc_lock,
#endif  // BUILDFLAG(IS_ANDROID)
                        base::WaitableEvent* done_event) override {
    done_event->Signal();
  }

 private:
  void OnWillDestroyStub(bool have_context) override {}
};

class StarboardRendererWrapperTest : public testing::Test {
 protected:
  using RendererExtension = mojom::StarboardRendererExtension;
  using ClientExtension = mojom::StarboardRendererClientExtension;

  StarboardRendererWrapperTest()
      : mock_renderer_(std::make_unique<StrictMock<MockStarboardRenderer>>(
            task_environment_.GetMainThreadTaskRunner(),
            std::make_unique<NullMediaLog>(),
            base::UnguessableToken::Create(),
            base::Seconds(1),
            base::Seconds(1),
            std::string(),
            StarboardRendererConfig::ExperimentalFeatures{},
            gfx::Size()
#if BUILDFLAG(IS_ANDROID)
                ,
            AndroidOverlayMojoFactoryCB()
#endif  // BUILDFLAG(IS_ANDROID)
                )),
        mock_gpu_factory_(dedicated_task_runner) {
    // Setup MockStarboardGpuFactory as StarboardGpuFactory so
    // it can overwrite |gpu_factory_| in StarboardRendererWrapper
    // via SetGpuFactoryForTesting().
    gpu_factory_ = std::move(mock_gpu_factory_);
    // Setup traits required by MockStarboardRendererWrapper.
    auto media_log_remote = media_log_.InitWithNewPipeAndPassRemote();
    auto renderer_extension_receiver =
        renderer_extension_.BindNewPipeAndPassReceiver();
    auto client_extension_remote =
        client_extension_.InitWithNewPipeAndPassRemote();
    StarboardRendererTraits traits(
        task_environment_.GetMainThreadTaskRunner(),
        task_environment_.GetMainThreadTaskRunner(),
        std::move(media_log_remote), &video_geometry_setter_service_,
        base::UnguessableToken::Create(), base::Seconds(1), base::Seconds(1),
        std::string(), StarboardRendererConfig::ExperimentalFeatures{},
        gfx::Size(), std::move(renderer_extension_receiver),
        std::move(client_extension_remote), base::NullCallback());
    renderer_wrapper_ =
        std::make_unique<StarboardRendererWrapper>(std::move(traits)
#if BUILDFLAG(IS_ANDROID)
                                                       ,
                                                   /*ref_counted_lock=*/nullptr
#endif  // BUILDFLAG(IS_ANDROID)
        );
    renderer_wrapper_->SetRendererForTesting(mock_renderer_.get());
    renderer_wrapper_->SetGpuFactoryForTesting(&gpu_factory_);

    EXPECT_CALL(media_resource_, GetAllStreams())
        .WillRepeatedly(
            Invoke(this, &StarboardRendererWrapperTest::GetAllStreams));
  }

  ~StarboardRendererWrapperTest() override {
    renderer_wrapper_.reset();
    mock_renderer_.reset();
  }

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

  base::test::TaskEnvironment task_environment_;
  scoped_refptr<base::SequencedTaskRunner> dedicated_task_runner =
      base::ThreadPool::CreateSingleThreadTaskRunner(
          /*traits=*/{},
          base::SingleThreadTaskRunnerThreadMode::DEDICATED);
  cobalt::media::VideoGeometrySetterService video_geometry_setter_service_;
  std::unique_ptr<StrictMock<MockStarboardRenderer>> mock_renderer_;
  base::SequenceBound<StrictMock<MockStarboardGpuFactory>> mock_gpu_factory_;
  base::SequenceBound<StarboardGpuFactory> gpu_factory_;
  std::unique_ptr<StarboardRendererWrapper> renderer_wrapper_;
  NiceMock<MockMediaResource> media_resource_;
  NiceMock<MockRendererClient> renderer_client_;
  base::MockOnceCallback<void(PipelineStatus)> renderer_init_cb_;
  std::vector<std::unique_ptr<StrictMock<MockDemuxerStream>>> streams_;

  mojo::PendingReceiver<mojom::MediaLog> media_log_;
  mojo::Remote<mojom::StarboardRendererExtension> renderer_extension_;
  mojo::PendingReceiver<mojom::StarboardRendererClientExtension>
      client_extension_;
};

TEST_F(StarboardRendererWrapperTest, InitializeWithoutGpuChannelToken) {
  AddStream(DemuxerStream::AUDIO, false);
  AddStream(DemuxerStream::VIDEO, false);

  EXPECT_CALL(*mock_renderer_, OnInitialize(_, _, _))
      .WillOnce(RunOnceCallback<2>(PIPELINE_OK));
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  renderer_wrapper_->Initialize(&media_resource_, &renderer_client_,
                                renderer_init_cb_.Get());

  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest, InitializeWithGpuChannelToken) {
  AddStream(DemuxerStream::AUDIO, false);
  AddStream(DemuxerStream::VIDEO, false);

  auto command_buffer_id = mojom::CommandBufferId::New();
  command_buffer_id->channel_token = base::UnguessableToken::Create();
  command_buffer_id->route_id = 0;
  renderer_wrapper_->OnGpuChannelTokenReady(std::move(command_buffer_id));

  EXPECT_CALL(*mock_renderer_, OnInitialize(_, _, _))
      .WillOnce(RunOnceCallback<2>(PIPELINE_OK));
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  renderer_wrapper_->Initialize(&media_resource_, &renderer_client_,
                                renderer_init_cb_.Get());

  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest, InitializeWithBypassBridge) {
  auto bypass_bridge = base::MakeRefCounted<MojoRendererBypassBridge>(
      task_environment_.GetMainThreadTaskRunner(), base::DoNothing(),
      base::DoNothing());
  AddStream(DemuxerStream::AUDIO, false);
  AddStream(DemuxerStream::VIDEO, false);
  AudioDecoderConfig audio_config = streams_[0]->audio_decoder_config();
  audio_config.set_mime_type("audio/mp4");
  streams_[0]->set_audio_decoder_config(audio_config);

  VideoDecoderConfig video_config = streams_[1]->video_decoder_config();
  video_config.set_mime_type("video/mp4");
  streams_[1]->set_video_decoder_config(video_config);
  bypass_bridge->SetStreams(streams_[0].get(), streams_[1].get());
  uint32_t bridge_id = 12345;
  BypassBridgeRegistry::Register(bridge_id, bypass_bridge);
  base::ScopedClosureRunner unregister_runner(
      base::BindOnce(&BypassBridgeRegistry::Unregister, bridge_id));

  base::MockOnceCallback<void(bool)> bypass_init_cb;
  EXPECT_CALL(bypass_init_cb, Run(true));
  renderer_wrapper_->InitializeWithBypassBridge(bridge_id,
                                                bypass_init_cb.Get());

  EXPECT_CALL(*mock_renderer_, OnInitialize(_, _, _))
      .WillOnce(RunOnceCallback<2>(PIPELINE_OK));
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  renderer_wrapper_->Initialize(&media_resource_, &renderer_client_,
                                renderer_init_cb_.Get());

  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest,
       InitializeWithBypassBridge_NoValidStreams) {
  auto bypass_bridge = base::MakeRefCounted<MojoRendererBypassBridge>(
      task_environment_.GetMainThreadTaskRunner(), base::DoNothing(),
      base::DoNothing());
  uint32_t bridge_id = 12345;
  BypassBridgeRegistry::Register(bridge_id, bypass_bridge);
  base::ScopedClosureRunner unregister_runner(
      base::BindOnce(&BypassBridgeRegistry::Unregister, bridge_id));

  base::MockOnceCallback<void(bool)> bypass_init_cb;
  EXPECT_CALL(bypass_init_cb, Run(false));
  renderer_wrapper_->InitializeWithBypassBridge(bridge_id,
                                                bypass_init_cb.Get());
}

TEST_F(StarboardRendererWrapperTest, Flush) {
  base::MockOnceClosure closure;
  EXPECT_CALL(*mock_renderer_, OnFlush(_)).WillOnce(RunOnceCallback<0>());
  EXPECT_CALL(closure, Run());
  renderer_wrapper_->Flush(closure.Get());
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest, StartPlayingFrom) {
  const base::TimeDelta kStartTime = base::Seconds(5);
  EXPECT_CALL(*mock_renderer_, StartPlayingFrom(kStartTime));
  renderer_wrapper_->StartPlayingFrom(kStartTime);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest, SetPlaybackRate) {
  const double kPlaybackRate = 2.5;
  EXPECT_CALL(*mock_renderer_, SetPlaybackRate(kPlaybackRate));
  renderer_wrapper_->SetPlaybackRate(kPlaybackRate);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest, SetVolume) {
  const float kVolume = 0.75f;
  EXPECT_CALL(*mock_renderer_, SetVolume(kVolume));
  renderer_wrapper_->SetVolume(kVolume);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest, GetMediaTime) {
  const base::TimeDelta kExpectedTime = base::Seconds(42);
  EXPECT_CALL(*mock_renderer_, GetMediaTime()).WillOnce(Return(kExpectedTime));
  EXPECT_EQ(renderer_wrapper_->GetMediaTime(), kExpectedTime);
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest, SetCdm) {
  base::MockCallback<Renderer::CdmAttachedCB> cdm_attached_cb;
  EXPECT_CALL(*mock_renderer_, OnSetCdm(/*cdm_context=*/nullptr, _))
      .WillOnce(RunOnceCallback<1>(true));
  EXPECT_CALL(cdm_attached_cb, Run(true));
  renderer_wrapper_->SetCdm(/*cdm_context=*/nullptr, cdm_attached_cb.Get());
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest, GetCurrentVideoFrame) {
  base::MockCallback<StarboardRendererWrapper::GetCurrentVideoFrameCallback>
      callback;
  EXPECT_CALL(callback, Run(testing::IsNull()));
  renderer_wrapper_->GetCurrentVideoFrame(callback.Get());
  task_environment_.RunUntilIdle();
}

TEST_F(StarboardRendererWrapperTest, ProxyDemuxerStreamDelegation) {
  auto bypass_bridge = base::MakeRefCounted<MojoRendererBypassBridge>(
      task_environment_.GetMainThreadTaskRunner(), base::DoNothing(),
      base::DoNothing());
  AddStream(DemuxerStream::AUDIO, false);
  AddStream(DemuxerStream::VIDEO, false);
  AudioDecoderConfig audio_config = streams_[0]->audio_decoder_config();
  audio_config.set_mime_type("audio/mp4");
  streams_[0]->set_audio_decoder_config(audio_config);

  VideoDecoderConfig video_config = streams_[1]->video_decoder_config();
  video_config.set_mime_type("video/mp4");
  streams_[1]->set_video_decoder_config(video_config);
  bypass_bridge->SetStreams(streams_[0].get(), streams_[1].get());

  uint32_t bridge_id = 12345;
  BypassBridgeRegistry::Register(bridge_id, bypass_bridge);
  base::ScopedClosureRunner unregister_runner(
      base::BindOnce(&BypassBridgeRegistry::Unregister, bridge_id));

  base::MockOnceCallback<void(bool)> bypass_init_cb;
  EXPECT_CALL(bypass_init_cb, Run(true));
  renderer_wrapper_->InitializeWithBypassBridge(bridge_id,
                                                bypass_init_cb.Get());

  MediaResource* captured_media_resource = nullptr;
  EXPECT_CALL(*mock_renderer_, OnInitialize(_, _, _))
      .WillOnce(
          Invoke([&captured_media_resource](MediaResource* media_resource,
                                            RendererClient* client,
                                            PipelineStatusCallback& init_cb) {
            captured_media_resource = media_resource;
            std::move(init_cb).Run(PIPELINE_OK);
          }));
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  renderer_wrapper_->Initialize(&media_resource_, &renderer_client_,
                                renderer_init_cb_.Get());
  task_environment_.RunUntilIdle();

  ASSERT_NE(captured_media_resource, nullptr);

  // Verify delegation of EnableBitstreamConverter() through proxy streams.
  EXPECT_CALL(*streams_[0], EnableBitstreamConverter());
  EXPECT_CALL(*streams_[1], EnableBitstreamConverter());

  auto proxy_streams = captured_media_resource->GetAllStreams();
  ASSERT_EQ(proxy_streams.size(), 2u);
  EXPECT_EQ(proxy_streams[0]->audio_decoder_config().mime_type(), "audio/mp4");
  EXPECT_EQ(proxy_streams[1]->video_decoder_config().mime_type(), "video/mp4");
  proxy_streams[0]->EnableBitstreamConverter();
  proxy_streams[1]->EnableBitstreamConverter();
}

TEST_F(StarboardRendererWrapperTest, TimerLifecycle) {
  auto bypass_bridge = base::MakeRefCounted<MojoRendererBypassBridge>(
      task_environment_.GetMainThreadTaskRunner(), base::DoNothing(),
      base::DoNothing());
  AddStream(DemuxerStream::AUDIO, false);
  AddStream(DemuxerStream::VIDEO, false);
  AudioDecoderConfig audio_config = streams_[0]->audio_decoder_config();
  audio_config.set_mime_type("audio/mp4");
  streams_[0]->set_audio_decoder_config(audio_config);

  VideoDecoderConfig video_config = streams_[1]->video_decoder_config();
  video_config.set_mime_type("video/mp4");
  streams_[1]->set_video_decoder_config(video_config);
  bypass_bridge->SetStreams(streams_[0].get(), streams_[1].get());

  uint32_t bridge_id = 12345;
  BypassBridgeRegistry::Register(bridge_id, bypass_bridge);
  base::ScopedClosureRunner unregister_runner(
      base::BindOnce(&BypassBridgeRegistry::Unregister, bridge_id));

  base::MockOnceCallback<void(bool)> bypass_init_cb;
  EXPECT_CALL(bypass_init_cb, Run(true));
  renderer_wrapper_->InitializeWithBypassBridge(bridge_id,
                                                bypass_init_cb.Get());

  EXPECT_CALL(*mock_renderer_, OnInitialize(_, _, _))
      .WillOnce(RunOnceCallback<2>(PIPELINE_OK));
  EXPECT_CALL(renderer_init_cb_, Run(HasStatusCode(PIPELINE_OK)));
  renderer_wrapper_->Initialize(&media_resource_, &renderer_client_,
                                renderer_init_cb_.Get());
  task_environment_.RunUntilIdle();

  // Start playing (sets playback_rate > 0 when SetPlaybackRate(1.0) is called).
  EXPECT_CALL(*mock_renderer_, GetMediaTime())
      .WillRepeatedly(Return(base::Seconds(0)));
  EXPECT_CALL(*mock_renderer_, SetPlaybackRate(1.0));
  renderer_wrapper_->SetPlaybackRate(1.0);
  EXPECT_CALL(*mock_renderer_, StartPlayingFrom(base::Seconds(0)));
  renderer_wrapper_->StartPlayingFrom(base::Seconds(0));

  // Pause playback (playback_rate == 0).
  EXPECT_CALL(*mock_renderer_, SetPlaybackRate(0.0));
  renderer_wrapper_->SetPlaybackRate(0.0);

  // Resume and Flush.
  EXPECT_CALL(*mock_renderer_, SetPlaybackRate(1.0));
  renderer_wrapper_->SetPlaybackRate(1.0);
  base::MockOnceClosure flush_cb;
  EXPECT_CALL(*mock_renderer_, OnFlush(_)).WillOnce(RunOnceCallback<0>());
  EXPECT_CALL(flush_cb, Run());
  renderer_wrapper_->Flush(flush_cb.Get());
  task_environment_.RunUntilIdle();
}

}  // namespace

}  // namespace media
