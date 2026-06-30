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

#include "base/functional/bind.h"
#include "base/logging.h"
#include "media/base/media_log.h"
#include "media/base/media_resource.h"
#include "media/base/video_frame.h"
#include "media/mojo/clients/mojo_renderer.h"
#include "media/renderers/video_overlay_factory.h"
#include "url/gurl.h"

namespace media {

UrlPlayerRendererClient::UrlPlayerRendererClient(
    const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
    std::unique_ptr<MediaLog> media_log,
    std::unique_ptr<MojoRenderer> mojo_renderer,
    std::unique_ptr<VideoOverlayFactory> video_overlay_factory,
    VideoRendererSink* video_renderer_sink,
    mojo::PendingRemote<RendererExtension> pending_renderer_extension,
    mojo::PendingReceiver<ClientExtension> client_extension_receiver,
    GetSbWindowHandleCallback get_sb_window_handle_callback)
    : MojoRendererWrapper(std::move(mojo_renderer)),
      media_task_runner_(media_task_runner),
      media_log_(std::move(media_log)),
      video_overlay_factory_(std::move(video_overlay_factory)),
      video_renderer_sink_(video_renderer_sink),
      pending_renderer_extension_(std::move(pending_renderer_extension)),
      pending_client_extension_receiver_(std::move(client_extension_receiver)),
      client_extension_receiver_(this),
      get_sb_window_handle_callback_(get_sb_window_handle_callback) {
  DCHECK(media_task_runner_);
  DCHECK(video_renderer_sink_);
  DCHECK(video_overlay_factory_);
  LOG(INFO) << "UrlPlayerRendererClient constructed.";
}

UrlPlayerRendererClient::~UrlPlayerRendererClient() = default;

void UrlPlayerRendererClient::Initialize(MediaResource* media_resource,
                                         RendererClient* client,
                                         PipelineStatusCallback init_cb) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!init_cb_);

  client_ = client;
  media_resource_ = media_resource;
  init_cb_ = std::move(init_cb);

  DCHECK(!AreMojoPipesConnected());
  InitAndBindMojoRenderer(base::BindOnce(
      &UrlPlayerRendererClient::InitializeMojoRenderer,
      weak_factory_.GetWeakPtr(), media_resource, client,
      base::BindOnce(&UrlPlayerRendererClient::OnMojoRendererInitialized,
                     weak_factory_.GetWeakPtr())));
}

RendererType UrlPlayerRendererClient::GetRendererType() {
  return RendererType::kUrlPlayer;
}

void UrlPlayerRendererClient::PaintVideoHoleFrame(const gfx::Size& size) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  video_renderer_sink_->PaintSingleFrame(
      video_overlay_factory_->CreateFrame(size));
}

// Two-event initialization protocol:
//
// UrlPlayerRendererClient initialization requires two independent events
// before firing init_cb_:
//   Event A: Mojo init response (OnMojoRendererInitialized) - response to
//            InitializeWithUrl sent over mojom::Renderer pipe.
//   Event B: Rendering mode update (UpdateStarboardRenderingMode) - sent by
//            UrlPlayerRendererWrapper over
//            mojom::StarboardRendererClientExtension pipe after
//            UrlPlayerRenderer creates the SbPlayerBridge.
//
// These arrive on different Mojo pipes with no ordering guarantee.
// Whichever arrives second sees the first's state and fires init_cb_.
//
// URL player only supports punch-out mode. The rendering mode is determined
// at the Starboard layer by SbUrlPlayerOutputModeSupported()
// (starboard/tvos/shared/media/url_player_output_mode_supported.mm), which
// returns true only for kSbPlayerOutputModePunchOut. This is called via
// SbPlayerBridge::ComputeSbUrlPlayerOutputMode() (media/starboard/
// sbplayer_bridge.cc) before player creation. After creation,
// UrlPlayerRenderer::CreatePlayerBridge() (media/starboard/
// url_player_renderer.cc) validates the mode and always sends kPunchOut
// through the callback chain:
//   UrlPlayerRenderer -> UrlPlayerRendererWrapper::
//   OnUpdateStarboardRenderingMode -> Mojo -> here.
//
// Because the rendering mode is always kPunchOut for URL player,
// DTT and invalid modes are not expected. LOG(WARNING) is used to flag
// unexpected values without crashing, since the platform guarantees
// punch-out.
void UrlPlayerRendererClient::UpdateStarboardRenderingMode(
    const StarboardRenderingMode mode) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  rendering_mode_ = mode;

  if (rendering_mode_ != StarboardRenderingMode::kPunchOut) {
    LOG(WARNING) << "URL player received unexpected rendering mode: "
                 << static_cast<int>(rendering_mode_)
                 << ". Only punch-out is supported.";
    return;
  }

  // Two-event protocol: if Mojo init already completed, run init_cb_ now.
  if (IsMojoRendererInitialized() && !init_cb_.is_null()) {
    std::move(init_cb_).Run(pipeline_status());
  }
}

void UrlPlayerRendererClient::GetSbWindowHandle() {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  uint64_t sb_window_handle = 0;
  if (get_sb_window_handle_callback_) {
    sb_window_handle = get_sb_window_handle_callback_.Run();
  }
  renderer_extension_->OnSbWindowHandleReady(sb_window_handle);
}

void UrlPlayerRendererClient::OnDurationChange(base::TimeDelta duration) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  if (media_resource_) {
    media_resource_->ForwardDurationChangeToDemuxerHost(duration);
  }
}

void UrlPlayerRendererClient::OnBufferedTimeRangesChange(
    base::TimeDelta start,
    base::TimeDelta length) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  if (media_resource_) {
    media_resource_->ForwardBufferedTimeRangesToDemuxerHost(start, length);
  }
}

void UrlPlayerRendererClient::OnConnectionError() {
  LOG(ERROR) << "Mojo connection error.";
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  MEDIA_LOG(ERROR, media_log_) << "UrlPlayerRendererClient disconnected";
  if (!init_cb_.is_null()) {
    std::move(init_cb_).Run(PIPELINE_ERROR_DISCONNECTED);
    return;
  }
  client_->OnError(PIPELINE_ERROR_DISCONNECTED);
}

void UrlPlayerRendererClient::InitAndBindMojoRenderer(
    base::OnceClosure complete_cb) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!AreMojoPipesConnected());

  renderer_extension_.Bind(std::move(pending_renderer_extension_),
                           media_task_runner_);
  client_extension_receiver_.Bind(std::move(pending_client_extension_receiver_),
                                  media_task_runner_);

  renderer_extension_.set_disconnect_handler(base::BindOnce(
      &UrlPlayerRendererClient::OnConnectionError, base::Unretained(this)));

  // URL player does not need GPU channel. Skip OnGpuChannelTokenReady.
  DCHECK(complete_cb);
  std::move(complete_cb).Run();
}

void UrlPlayerRendererClient::InitializeMojoRenderer(
    MediaResource* media_resource,
    RendererClient* client,
    PipelineStatusCallback init_cb) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(AreMojoPipesConnected());

  GURL url = media_resource->GetMediaUrl();
  if (!url.is_valid()) {
    LOG(ERROR) << "Invalid media URL.";
    media_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(init_cb),
                       PipelineStatus(PIPELINE_ERROR_INITIALIZATION_FAILED,
                                      "Invalid media URL for URL player")));
    return;
  }

  InitializeWithUrl(client, url, std::move(init_cb));
}

void UrlPlayerRendererClient::OnMojoRendererInitialized(PipelineStatus status) {
  DCHECK(media_task_runner_->RunsTasksInCurrentSequence());

  SetMojoRendererInitialized(status);

  // Two-event protocol: if punch-out rendering mode already arrived, complete
  // now. Otherwise, UpdateStarboardRenderingMode() will fire init_cb_ when it
  // arrives. See the protocol comment above UpdateStarboardRenderingMode().
  if (rendering_mode_ == StarboardRenderingMode::kPunchOut &&
      !init_cb_.is_null()) {
    std::move(init_cb_).Run(status);
  }
}

void UrlPlayerRendererClient::SetMojoRendererInitialized(
    PipelineStatus status) {
  base::AutoLock auto_lock(lock_);
  is_mojo_renderer_initialized_ = true;
  pipeline_status_ = status;
}

bool UrlPlayerRendererClient::IsMojoRendererInitialized() const {
  base::AutoLock auto_lock(lock_);
  return is_mojo_renderer_initialized_;
}

PipelineStatus UrlPlayerRendererClient::pipeline_status() const {
  base::AutoLock auto_lock(lock_);
  return pipeline_status_;
}

}  // namespace media
