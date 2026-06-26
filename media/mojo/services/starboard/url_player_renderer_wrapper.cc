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

#include <utility>

#include "base/logging.h"
#include "media/mojo/services/mojo_media_log.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace media {

UrlPlayerRendererTraits::UrlPlayerRendererTraits(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    mojo::PendingRemote<mojom::MediaLog> media_log_remote,
    cobalt::media::VideoGeometrySetterService* video_geometry_setter_service,
    const base::UnguessableToken& overlay_plane_id,
    const gfx::Size& viewport_size,
    mojo::PendingReceiver<mojom::StarboardRendererExtension>
        renderer_extension_receiver,
    mojo::PendingRemote<mojom::StarboardRendererClientExtension>
        client_extension_remote)
    : task_runner(std::move(task_runner)),
      media_log_remote(std::move(media_log_remote)),
      video_geometry_setter_service(video_geometry_setter_service),
      overlay_plane_id(overlay_plane_id),
      viewport_size(viewport_size),
      renderer_extension_receiver(std::move(renderer_extension_receiver)),
      client_extension_remote(std::move(client_extension_remote)) {}

UrlPlayerRendererTraits::~UrlPlayerRendererTraits() = default;

UrlPlayerRendererWrapper::UrlPlayerRendererWrapper(
    UrlPlayerRendererTraits traits)
    : renderer_extension_receiver_(
          this,
          std::move(traits.renderer_extension_receiver)),
      client_extension_remote_(std::move(traits.client_extension_remote),
                               traits.task_runner),
      video_geometry_setter_service_(traits.video_geometry_setter_service),
      overlay_plane_id_(traits.overlay_plane_id),
      renderer_(
          traits.task_runner,
          std::make_unique<MojoMediaLog>(std::move(traits.media_log_remote),
                                         traits.task_runner),
          traits.overlay_plane_id,
          traits.viewport_size) {
  DETACH_FROM_THREAD(thread_checker_);
}

UrlPlayerRendererWrapper::~UrlPlayerRendererWrapper() = default;

void UrlPlayerRendererWrapper::Initialize(MediaResource* media_resource,
                                          RendererClient* client,
                                          PipelineStatusCallback init_cb) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(init_cb);

  // Subscribe to video geometry changes.
  DCHECK(video_geometry_setter_service_);
  video_geometry_setter_service_->GetVideoGeometryChangeSubscriber(
      video_geometry_change_subscriber_remote_.BindNewPipeAndPassReceiver());
  DCHECK(video_geometry_change_subscriber_remote_);
  video_geometry_change_subscriber_remote_->SubscribeToVideoGeometryChange(
      overlay_plane_id_,
      video_geometry_change_client_receiver_.BindNewPipeAndPassRemote(),
      base::BindOnce(
          &UrlPlayerRendererWrapper::OnSubscribeToVideoGeometryChange,
          weak_factory_.GetWeakPtr(), media_resource, client,
          std::move(init_cb)));

  // Install renderer callbacks for paint, rendering mode, and window handle.
  GetRenderer()->SetUrlPlayerRendererCallbacks(
      base::BindRepeating(&UrlPlayerRendererWrapper::OnPaintVideoHoleFrame,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          &UrlPlayerRendererWrapper::OnUpdateStarboardRenderingMode,
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&UrlPlayerRendererWrapper::OnGetSbWindowHandle,
                          weak_factory_.GetWeakPtr()));
}

void UrlPlayerRendererWrapper::InitializeWithUrl(
    RendererClient* client,
    const GURL& source_url,
    PipelineStatusCallback init_cb) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(source_url.is_valid());

  GetRenderer()->SetSourceUrl(source_url.spec());
  Initialize(/*media_resource=*/nullptr, client, std::move(init_cb));
}

void UrlPlayerRendererWrapper::Flush(base::OnceClosure flush_cb) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->Flush(std::move(flush_cb));
}

void UrlPlayerRendererWrapper::StartPlayingFrom(base::TimeDelta time) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->StartPlayingFrom(time);
}

void UrlPlayerRendererWrapper::SetPlaybackRate(double playback_rate) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->SetPlaybackRate(playback_rate);
}

void UrlPlayerRendererWrapper::SetVolume(float volume) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->SetVolume(volume);
}

void UrlPlayerRendererWrapper::SetCdm(CdmContext* cdm_context,
                                      CdmAttachedCB cdm_attached_cb) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->SetCdm(cdm_context, std::move(cdm_attached_cb));
}

void UrlPlayerRendererWrapper::SetLatencyHint(
    std::optional<base::TimeDelta> latency_hint) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->SetLatencyHint(latency_hint);
}

base::TimeDelta UrlPlayerRendererWrapper::GetMediaTime() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return GetRenderer()->GetMediaTime();
}

RendererType UrlPlayerRendererWrapper::GetRendererType() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return RendererType::kUrlPlayer;
}

void UrlPlayerRendererWrapper::OnGpuChannelTokenReady(
    mojom::CommandBufferIdPtr command_buffer_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // URL player uses punch-out only; GPU channel is not needed.
}

void UrlPlayerRendererWrapper::GetCurrentVideoFrame(
    GetCurrentVideoFrameCallback callback) {
  // URL player uses punch-out only; decode-to-texture is unsupported.
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(nullptr);
}

void UrlPlayerRendererWrapper::OnSbWindowHandleReady(
    uint64_t sb_window_handle) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->OnSbWindowHandleReady(sb_window_handle);
}

UrlPlayerRenderer* UrlPlayerRendererWrapper::GetRenderer() {
  if (test_renderer_) {
    return test_renderer_;
  }
  return &renderer_;
}

void UrlPlayerRendererWrapper::OnVideoGeometryChange(
    const gfx::RectF& rect_f,
    gfx::OverlayTransform /* transform */) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  gfx::Rect new_bounds = gfx::ToEnclosedRect(rect_f);
  GetRenderer()->OnVideoGeometryChange(new_bounds);
}

void UrlPlayerRendererWrapper::OnPaintVideoHoleFrame(const gfx::Size& size) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->PaintVideoHoleFrame(size);
}

void UrlPlayerRendererWrapper::OnUpdateStarboardRenderingMode(
    StarboardRenderingMode mode) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->UpdateStarboardRenderingMode(mode);
}

void UrlPlayerRendererWrapper::OnGetSbWindowHandle() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->GetSbWindowHandle();
}

void UrlPlayerRendererWrapper::OnSubscribeToVideoGeometryChange(
    MediaResource* media_resource,
    RendererClient* client,
    PipelineStatusCallback init_cb) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(init_cb);
  GetRenderer()->Initialize(media_resource, client, std::move(init_cb));
}

}  // namespace media
