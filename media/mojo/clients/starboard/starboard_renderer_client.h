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

#ifndef MEDIA_MOJO_CLIENTS_STARBOARD_STARBOARD_RENDERER_CLIENT_H_
#define MEDIA_MOJO_CLIENTS_STARBOARD_STARBOARD_RENDERER_CLIENT_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "media/base/pipeline_status.h"
#include "media/base/renderer_client.h"
#include "media/base/starboard/starboard_rendering_mode.h"
#include "media/base/video_renderer_sink.h"
#include "media/mojo/clients/mojo_renderer_wrapper.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "media/starboard/starboard_callbacks.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace media {
class GpuVideoAcceleratorFactories;
class RendererClient;
class MediaLog;
class MediaResource;
class MojoRenderer;
class VideoFrame;
class VideoOverlayFactory;

// StarboardRendererClient lives in Chrome_InProcRendererThread
// (Media thread) talks to the StarboardRenderer living in the
// Chrome_InProcGpuThread, using `mojo_renderer_` and `renderer_extension_`.
class MEDIA_EXPORT StarboardRendererClient
    : public MojoRendererWrapper,
      public RendererClient,
      public mojom::StarboardRendererClientExtension,
      public VideoRendererSink::RenderCallback {
 public:
  using RendererExtension = mojom::StarboardRendererExtension;
  using ClientExtension = media::mojom::StarboardRendererClientExtension;

  StarboardRendererClient(
      const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
      std::unique_ptr<MediaLog> media_log,
      std::unique_ptr<MojoRenderer> mojo_renderer,
      std::unique_ptr<VideoOverlayFactory> video_overlay_factory,
      VideoRendererSink* video_renderer_sink,
      mojo::PendingRemote<RendererExtension> pending_renderer_extension,
      mojo::PendingReceiver<ClientExtension> client_extension_receiver,
      GetSbWindowHandleCallback get_sb_window_handle_callback,
      GpuVideoAcceleratorFactories* gpu_factories
#if BUILDFLAG(IS_ANDROID)
      ,
      RequestOverlayInfoCB request_overlay_info_cb
#endif  // BUILDFLAG(IS_ANDROID)
  );

  StarboardRendererClient(const StarboardRendererClient&) = delete;
  StarboardRendererClient& operator=(const StarboardRendererClient&) = delete;

  ~StarboardRendererClient() override;

  // MojoRendererWrapper overrides.
  void Initialize(MediaResource* media_resource,
                  RendererClient* client,
                  PipelineStatusCallback init_cb) override;
  void StartPlayingFrom(base::TimeDelta time) override;
  RendererType GetRendererType() override;

  // RendererClient implementation.
  void OnError(PipelineStatus status) override;
  void OnFallback(PipelineStatus fallback) override;
  void OnEnded() override;
  void OnStatisticsUpdate(const PipelineStatistics& stats) override;
  void OnBufferingStateChange(BufferingState state,
                              BufferingStateChangeReason) override;
  void OnWaiting(WaitingReason reason) override;
  void OnAudioConfigChange(const AudioDecoderConfig& config) override;
  void OnVideoConfigChange(const VideoDecoderConfig& config) override;
  void OnVideoNaturalSizeChange(const gfx::Size& size) override;
  void OnVideoOpacityChange(bool opaque) override;
  void OnVideoFrameRateChange(std::optional<int> fps) override;

  // VideoRendererSink::RenderCallback implementation.
  scoped_refptr<VideoFrame> Render(
      base::TimeTicks deadline_min,
      base::TimeTicks deadline_max,
      VideoRendererSink::RenderCallback::RenderingMode rendering_mode) override;
  void OnFrameDropped() override;
  base::TimeDelta GetPreferredRenderInterval() override;

  // mojom::StarboardRendererClientExtension implementation
  void PaintVideoHoleFrame(const gfx::Size& size) override;
  void UpdateStarboardRenderingMode(const StarboardRenderingMode mode) override;
  void GetSbWindowHandle() override;
#if BUILDFLAG(USE_STARBOARD_URL_PLAYER)
  void OnDurationChange(base::TimeDelta duration) override;
  void OnBufferedTimeRangesChange(base::TimeDelta start,
                                  base::TimeDelta length) override;
#endif  // BUILDFLAG(USE_STARBOARD_URL_PLAYER)
#if BUILDFLAG(IS_ANDROID)
  void RequestOverlayInfo(bool restart_for_transitions) override;
#endif  // BUILDFLAG(IS_ANDROID)

 private:
  void OnConnectionError();
  void InitAndBindMojoRenderer(base::OnceClosure complete_cb);
  void OnGpuChannelTokenReady(mojom::CommandBufferIdPtr command_buffer_id,
                              base::OnceClosure complete_cb,
                              const base::UnguessableToken& channel_token);
  void InitializeMojoRenderer(MediaResource* media_resource,
                              RendererClient* client,
                              PipelineStatusCallback init_cb);
  void InitAndConstructMojoRenderer(mojom::CommandBufferIdPtr command_buffer_id,
                                    base::OnceClosure complete_cb);
  bool AreMojoPipesConnected() const {
    return client_extension_receiver_.is_bound() &&
           renderer_extension_.is_bound();
  }
  void OnMojoRendererInitialized(PipelineStatus status);
  void SetMojoRendererInitialized(PipelineStatus status);
  bool IsMojoRendererInitialized() const;
  PipelineStatus pipeline_status() const;
  void SetPlayingState(bool is_playing);
  void UpdateCurrentFrame();
  void OnGetCurrentVideoFrameDone(const scoped_refptr<VideoFrame>& frame);
#if BUILDFLAG(IS_ANDROID)
  void OnOverlayInfoChanged(const OverlayInfo& overlay_info);
#endif  // BUILDFLAG(IS_ANDROID)
  void StartVideoRendererSink();
  void StopVideoRendererSink();

  scoped_refptr<base::SequencedTaskRunner> media_task_runner_;
  std::unique_ptr<MediaLog> media_log_;
  std::unique_ptr<VideoOverlayFactory> video_overlay_factory_;
  raw_ptr<VideoRendererSink> video_renderer_sink_ = nullptr;
  mojo::PendingRemote<RendererExtension> pending_renderer_extension_;
  mojo::PendingReceiver<ClientExtension> pending_client_extension_receiver_;
  mojo::Receiver<ClientExtension> client_extension_receiver_;
  const GetSbWindowHandleCallback get_sb_window_handle_callback_;
  raw_ptr<GpuVideoAcceleratorFactories> gpu_factories_ = nullptr;
#if BUILDFLAG(IS_ANDROID)
  RequestOverlayInfoCB request_overlay_info_cb_;
#endif  // BUILDFLAG(IS_ANDROID)

  mojo::Remote<RendererExtension> renderer_extension_;

  raw_ptr<RendererClient> client_ = nullptr;
  PipelineStatusCallback init_cb_;

  // Rendering mode the Starboard Renderer will use.
  StarboardRenderingMode rendering_mode_ = StarboardRenderingMode::kInvalid;

  bool is_playing_ = false;
  bool video_renderer_sink_started_ = false;
#if BUILDFLAG(IS_ANDROID)
  bool overlay_info_requested_ = false;
#endif  // BUILDFLAG(IS_ANDROID)
  scoped_refptr<VideoFrame> next_video_frame_;

  mutable base::Lock lock_;
  bool is_mojo_renderer_initialized_ GUARDED_BY(lock_) = false;
  PipelineStatus pipeline_status_ GUARDED_BY(lock_) =
      PipelineStatus(PIPELINE_ERROR_INVALID_STATE);

  // NOTE: Do not add member variables after weak_factory_
  // It should be the first one destroyed among all members.
  // See base/memory/weak_ptr.h.
  base::WeakPtrFactory<StarboardRendererClient> weak_factory_{this};
};

}  // namespace media

#endif  // MEDIA_MOJO_CLIENTS_STARBOARD_STARBOARD_RENDERER_CLIENT_H_
