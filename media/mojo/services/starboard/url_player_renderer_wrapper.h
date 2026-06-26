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

#ifndef MEDIA_MOJO_SERVICES_STARBOARD_URL_PLAYER_RENDERER_WRAPPER_H_
#define MEDIA_MOJO_SERVICES_STARBOARD_URL_PLAYER_RENDERER_WRAPPER_H_

#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "cobalt/media/service/mojom/video_geometry_setter.mojom.h"
#include "cobalt/media/service/video_geometry_setter_service.h"
#include "media/base/renderer.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "media/starboard/url_player_renderer.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "url/gurl.h"

namespace media {

struct UrlPlayerRendererTraits {
  scoped_refptr<base::SequencedTaskRunner> task_runner;
  mojo::PendingRemote<mojom::MediaLog> media_log_remote;
  cobalt::media::VideoGeometrySetterService* video_geometry_setter_service;
  const base::UnguessableToken& overlay_plane_id;
  const gfx::Size& viewport_size;
  mojo::PendingReceiver<mojom::StarboardRendererExtension>
      renderer_extension_receiver;
  mojo::PendingRemote<mojom::StarboardRendererClientExtension>
      client_extension_remote;

  UrlPlayerRendererTraits(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      mojo::PendingRemote<mojom::MediaLog> media_log_remote,
      cobalt::media::VideoGeometrySetterService* video_geometry_setter_service,
      const base::UnguessableToken& overlay_plane_id,
      const gfx::Size& viewport_size,
      mojo::PendingReceiver<mojom::StarboardRendererExtension>
          renderer_extension_receiver,
      mojo::PendingRemote<mojom::StarboardRendererClientExtension>
          client_extension_remote);
  UrlPlayerRendererTraits(UrlPlayerRendererTraits&& that) = default;
  ~UrlPlayerRendererTraits();
};

// Wrapper around UrlPlayerRenderer that bridges to Mojo extension endpoints.
// Mirrors StarboardRendererWrapper but omits GPU factory, decode-to-texture
// (SbDecodeTarget) frame extraction, and Android overlay support because
// URL player delegates rendering to the platform (AVPlayer) via punch-out.
// This is consistent with Cobalt 25, where SbUrlPlayerOutputModeSupported()
// returned true only for kSbPlayerOutputModePunchOut on tvOS.
// OnGpuChannelTokenReady() and GetCurrentVideoFrame() are stubbed as no-ops;
// decode-to-texture support can be added when tvOS requires it.
//
// Lifetime and Ownership: Owned by MojoRendererService, lifetime is
// tied to the Mojo connection.
//
// Threading Model: Thread-affine, must be used on the media task
// runner sequence.
class UrlPlayerRendererWrapper
    : public Renderer,
      public mojom::StarboardRendererExtension,
      public cobalt::media::mojom::VideoGeometryChangeClient {
 public:
  using RendererExtension = mojom::StarboardRendererExtension;
  using ClientExtension = mojom::StarboardRendererClientExtension;

  explicit UrlPlayerRendererWrapper(UrlPlayerRendererTraits traits);

  UrlPlayerRendererWrapper(const UrlPlayerRendererWrapper&) = delete;
  UrlPlayerRendererWrapper& operator=(const UrlPlayerRendererWrapper&) = delete;

  ~UrlPlayerRendererWrapper() override;

  // Renderer implementation.
  void Initialize(MediaResource* media_resource,
                  RendererClient* client,
                  PipelineStatusCallback init_cb) override;
  void InitializeWithUrl(RendererClient* client,
                         const GURL& source_url,
                         PipelineStatusCallback init_cb);
  void SetCdm(CdmContext* cdm_context, CdmAttachedCB cdm_attached_cb) override;
  void SetLatencyHint(std::optional<base::TimeDelta> latency_hint) override;
  void Flush(base::OnceClosure flush_cb) override;
  void StartPlayingFrom(base::TimeDelta time) override;
  void SetPlaybackRate(double playback_rate) override;
  void SetVolume(float volume) override;
  base::TimeDelta GetMediaTime() override;
  RendererType GetRendererType() override;

  // mojom::StarboardRendererExtension implementation.
  void OnGpuChannelTokenReady(
      mojom::CommandBufferIdPtr command_buffer_id) override;
  void GetCurrentVideoFrame(GetCurrentVideoFrameCallback callback) override;
  void OnSbWindowHandleReady(uint64_t sb_window_handle) override;
  // cobalt::media::mojom::VideoGeometryChangeClient implementation.
  void OnVideoGeometryChange(const gfx::RectF& rect_f,
                             gfx::OverlayTransform transform) override;

  UrlPlayerRenderer* GetRenderer();

  void SetRendererForTesting(UrlPlayerRenderer* renderer) {
    test_renderer_ = renderer;
  }

 private:
  void OnPaintVideoHoleFrame(const gfx::Size& size);
  void OnUpdateStarboardRenderingMode(StarboardRenderingMode mode);
  void OnGetSbWindowHandle();
  void OnSubscribeToVideoGeometryChange(MediaResource* media_resource,
                                        RendererClient* client,
                                        PipelineStatusCallback init_cb);

  mojo::Receiver<RendererExtension> renderer_extension_receiver_;
  mojo::Remote<ClientExtension> client_extension_remote_;
  cobalt::media::VideoGeometrySetterService* video_geometry_setter_service_;
  const base::UnguessableToken overlay_plane_id_;
  UrlPlayerRenderer renderer_;

  raw_ptr<UrlPlayerRenderer> test_renderer_ = nullptr;

  mojo::Remote<cobalt::media::mojom::VideoGeometryChangeSubscriber>
      video_geometry_change_subscriber_remote_;
  mojo::Receiver<cobalt::media::mojom::VideoGeometryChangeClient>
      video_geometry_change_client_receiver_{this};

  THREAD_CHECKER(thread_checker_);

  base::WeakPtrFactory<UrlPlayerRendererWrapper> weak_factory_{this};
};

}  // namespace media

#endif  // MEDIA_MOJO_SERVICES_STARBOARD_URL_PLAYER_RENDERER_WRAPPER_H_
