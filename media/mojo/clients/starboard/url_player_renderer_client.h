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

#ifndef MEDIA_MOJO_CLIENTS_STARBOARD_URL_PLAYER_RENDERER_CLIENT_H_
#define MEDIA_MOJO_CLIENTS_STARBOARD_URL_PLAYER_RENDERER_CLIENT_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
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
class MediaLog;
class MediaResource;
class MojoRenderer;
class VideoOverlayFactory;

// UrlPlayerRendererClient lives in the renderer process (media thread) and
// talks to UrlPlayerRendererWrapper in the GPU process via Mojo.
// Simplified from StarboardRendererClient: no DTT, no RenderCallback,
// no GPU factories, no Android overlay.
class MEDIA_EXPORT UrlPlayerRendererClient
    : public MojoRendererWrapper,
      public mojom::StarboardRendererClientExtension {
 public:
  using RendererExtension = mojom::StarboardRendererExtension;
  using ClientExtension = mojom::StarboardRendererClientExtension;

  UrlPlayerRendererClient(
      const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
      std::unique_ptr<MediaLog> media_log,
      std::unique_ptr<MojoRenderer> mojo_renderer,
      std::unique_ptr<VideoOverlayFactory> video_overlay_factory,
      VideoRendererSink* video_renderer_sink,
      mojo::PendingRemote<RendererExtension> pending_renderer_extension,
      mojo::PendingReceiver<ClientExtension> client_extension_receiver,
      GetSbWindowHandleCallback get_sb_window_handle_callback);

  UrlPlayerRendererClient(const UrlPlayerRendererClient&) = delete;
  UrlPlayerRendererClient& operator=(const UrlPlayerRendererClient&) = delete;

  ~UrlPlayerRendererClient() override;

  // MojoRendererWrapper overrides.
  void Initialize(MediaResource* media_resource,
                  RendererClient* client,
                  PipelineStatusCallback init_cb) override;
  RendererType GetRendererType() override;

  // mojom::StarboardRendererClientExtension implementation.
  void PaintVideoHoleFrame(const gfx::Size& size) override;
  void UpdateStarboardRenderingMode(StarboardRenderingMode mode) override;
  void GetSbWindowHandle() override;
  void OnDurationChange(base::TimeDelta duration) override;
  void OnBufferedTimeRangesChange(base::TimeDelta start,
                                  base::TimeDelta length) override;

 private:
  void OnConnectionError();
  void InitAndBindMojoRenderer(base::OnceClosure complete_cb);
  void InitializeMojoRenderer(MediaResource* media_resource,
                              RendererClient* client,
                              PipelineStatusCallback init_cb);
  bool AreMojoPipesConnected() const {
    return client_extension_receiver_.is_bound() &&
           renderer_extension_.is_bound();
  }
  void OnMojoRendererInitialized(PipelineStatus status);
  void SetMojoRendererInitialized(PipelineStatus status);
  bool IsMojoRendererInitialized() const;
  PipelineStatus pipeline_status() const;

  scoped_refptr<base::SequencedTaskRunner> media_task_runner_;
  std::unique_ptr<MediaLog> media_log_;
  std::unique_ptr<VideoOverlayFactory> video_overlay_factory_;
  raw_ptr<VideoRendererSink> video_renderer_sink_ = nullptr;
  mojo::PendingRemote<RendererExtension> pending_renderer_extension_;
  mojo::PendingReceiver<ClientExtension> pending_client_extension_receiver_;
  mojo::Receiver<ClientExtension> client_extension_receiver_;
  const GetSbWindowHandleCallback get_sb_window_handle_callback_;

  mojo::Remote<RendererExtension> renderer_extension_;

  raw_ptr<RendererClient> client_ = nullptr;
  raw_ptr<MediaResource> media_resource_ = nullptr;
  PipelineStatusCallback init_cb_;

  StarboardRenderingMode rendering_mode_ = StarboardRenderingMode::kInvalid;

  mutable base::Lock lock_;
  bool is_mojo_renderer_initialized_ GUARDED_BY(lock_) = false;
  PipelineStatus pipeline_status_ GUARDED_BY(lock_) =
      PipelineStatus(PIPELINE_ERROR_INVALID_STATE);

  base::WeakPtrFactory<UrlPlayerRendererClient> weak_factory_{this};
};

}  // namespace media

#endif  // MEDIA_MOJO_CLIENTS_STARBOARD_URL_PLAYER_RENDERER_CLIENT_H_
