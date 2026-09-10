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

#ifndef MEDIA_MOJO_CLIENTS_STARBOARD_STARBOARD_RENDERER_CLIENT_FACTORY_H_
#define MEDIA_MOJO_CLIENTS_STARBOARD_STARBOARD_RENDERER_CLIENT_FACTORY_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "media/base/renderer_factory.h"
#include "media/base/starboard/renderer_factory_traits.h"
#include "media/base/starboard/starboard_renderer_config.h"
#include "media/starboard/starboard_callbacks.h"

namespace media {
class GpuVideoAcceleratorFactories;
class MediaLog;
class MojoRendererFactory;

// Creates StarboardRendererClient and binds
// media::mojom::StarboardRendererExtension and
// mojom::StarboardRendererClientExtension to StarboardRenderer.
class MEDIA_EXPORT StarboardRendererClientFactory final
    : public RendererFactory {
 public:
  using GetGpuFactoriesCB =
      base::RepeatingCallback<GpuVideoAcceleratorFactories*()>;

  StarboardRendererClientFactory(
      MediaLog* media_log,
      std::unique_ptr<MojoRendererFactory> mojo_renderer_factory,
      const GetGpuFactoriesCB& get_gpu_factories_cb,
      const media::RendererFactoryTraits* traits);

  StarboardRendererClientFactory(const StarboardRendererClientFactory&) =
      delete;
  StarboardRendererClientFactory& operator=(
      const StarboardRendererClientFactory&) = delete;

  ~StarboardRendererClientFactory() final;

  // RendererFactory implementation.
  std::unique_ptr<Renderer> CreateRenderer(
      const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
      const scoped_refptr<base::TaskRunner>& worker_task_runner,
      AudioRendererSink* audio_renderer_sink,
      VideoRendererSink* video_renderer_sink,
      RequestOverlayInfoCB request_overlay_info_cb,
      const gfx::ColorSpace& target_color_space) final;

 private:
  raw_ptr<MediaLog> media_log_;
  std::unique_ptr<MojoRendererFactory> mojo_renderer_factory_;
  // Creates gpu factories for supporting decode-to-texture mode.
  // It could be null.
  GetGpuFactoriesCB get_gpu_factories_cb_;
  const std::string max_video_capabilities_;
  const StarboardRendererConfig::ExperimentalFeatures experimental_features_;
  const gfx::Size viewport_size_;
  const GetSbWindowHandleCallback get_sb_window_handle_callback_;
};

}  // namespace media

#endif  // MEDIA_MOJO_CLIENTS_STARBOARD_STARBOARD_RENDERER_CLIENT_FACTORY_H_
