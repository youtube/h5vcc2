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

#include "media/mojo/clients/starboard/url_player_renderer_client_factory.h"

#include "base/check.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "media/base/starboard/starboard_renderer_config.h"
#include "media/mojo/clients/mojo_media_log_service.h"
#include "media/mojo/clients/mojo_renderer.h"
#include "media/mojo/clients/mojo_renderer_factory.h"
#include "media/mojo/clients/starboard/url_player_renderer_client.h"
#include "media/mojo/mojom/renderer_extensions.mojom.h"
#include "media/renderers/video_overlay_factory.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"

namespace media {

namespace {

StarboardRendererConfig::ExperimentalFeatures GetUrlPlayerFeatures(
    const StarboardRendererConfig::ExperimentalFeatures& features) {
  auto url_player_features = features;
  url_player_features.bypass_mojo_for_media = false;
  return url_player_features;
}
}  // namespace

UrlPlayerRendererClientFactory::UrlPlayerRendererClientFactory(
    MediaLog* media_log,
    std::unique_ptr<MojoRendererFactory> mojo_renderer_factory,
    const media::RendererFactoryTraits* traits)
    : media_log_(media_log),
      mojo_renderer_factory_(std::move(mojo_renderer_factory)),
      experimental_features_(
          GetUrlPlayerFeatures(traits->experimental_features)),
      viewport_size_(traits->viewport_size),
      get_sb_window_handle_callback_(traits->get_sb_window_handle_callback) {}

UrlPlayerRendererClientFactory::~UrlPlayerRendererClientFactory() = default;

std::unique_ptr<Renderer> UrlPlayerRendererClientFactory::CreateRenderer(
    const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
    const scoped_refptr<base::TaskRunner>& /*worker_task_runner*/,
    AudioRendererSink* /*audio_renderer_sink*/,
    VideoRendererSink* video_renderer_sink,
    RequestOverlayInfoCB /*request_overlay_info_cb*/,
    const gfx::ColorSpace& /*target_color_space*/) {
  DCHECK(video_renderer_sink);
  DCHECK(media_log_);
  DCHECK(mojo_renderer_factory_);

  // Clone media log via Mojo pipe.
  mojo::PendingReceiver<mojom::MediaLog> media_log_pending_receiver;
  auto media_log_pending_remote =
      media_log_pending_receiver.InitWithNewPipeAndPassRemote();
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<MojoMediaLogService>(media_log_->Clone()),
      std::move(media_log_pending_receiver));

  // Create renderer extension pipe pair.
  mojo::PendingRemote<media::mojom::StarboardRendererExtension>
      renderer_extension_remote;
  auto renderer_extension_receiver =
      renderer_extension_remote.InitWithNewPipeAndPassReceiver();

  // Create client extension pipe pair.
  mojo::PendingRemote<mojom::StarboardRendererClientExtension>
      client_extension_remote;
  auto client_extension_receiver =
      client_extension_remote.InitWithNewPipeAndPassReceiver();

  // Create overlay factory for video hole punch-out.
  auto overlay_factory = std::make_unique<VideoOverlayFactory>();

  // Build config. URL player ignores audio write durations and max video
  // capabilities, but StarboardRendererConfig requires all fields.
  StarboardRendererConfig config(
      overlay_factory->overlay_plane_id(), base::TimeDelta(), base::TimeDelta(),
      std::string(), experimental_features_, viewport_size_);

  // Create MojoRenderer via the URL player factory path.
  std::unique_ptr<media::MojoRenderer> mojo_renderer =
      mojo_renderer_factory_->CreateUrlPlayerRenderer(
          std::move(media_log_pending_remote), config,
          std::move(renderer_extension_receiver),
          std::move(client_extension_remote), media_task_runner,
          video_renderer_sink);

  return std::make_unique<media::UrlPlayerRendererClient>(
      media_task_runner, media_log_->Clone(), std::move(mojo_renderer),
      std::move(overlay_factory), video_renderer_sink,
      std::move(renderer_extension_remote),
      std::move(client_extension_receiver), get_sb_window_handle_callback_);
}

}  // namespace media
