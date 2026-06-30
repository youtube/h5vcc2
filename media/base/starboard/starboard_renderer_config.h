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

#ifndef MEDIA_BASE_STARBOARD_STARBOARD_RENDERER_CONFIG_H_
#define MEDIA_BASE_STARBOARD_STARBOARD_RENDERER_CONFIG_H_

#include <ostream>
#include <string>

#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "media/base/media_export.h"
#include "media/base/starboard/experimental_features.h"
#include "ui/gfx/geometry/size.h"

namespace media {

// Configs for StarboardRenderer.
struct MEDIA_EXPORT StarboardRendererConfig {
  using ExperimentalFeatures = media::ExperimentalFeatures;

  StarboardRendererConfig();
  StarboardRendererConfig(const base::UnguessableToken& overlay_plane_id,
                          base::TimeDelta audio_write_duration_local,
                          base::TimeDelta audio_write_duration_remote,
                          const std::string& max_video_capabilities,
                          const ExperimentalFeatures& experimental_features,
                          const gfx::Size& viewport_size);
  StarboardRendererConfig(const StarboardRendererConfig&);
  StarboardRendererConfig& operator=(const StarboardRendererConfig&);

  base::UnguessableToken overlay_plane_id;
  base::TimeDelta audio_write_duration_local;
  base::TimeDelta audio_write_duration_remote;
  std::string max_video_capabilities;
  ExperimentalFeatures experimental_features;
  gfx::Size viewport_size;
};

}  // namespace media

#endif  // MEDIA_BASE_STARBOARD_STARBOARD_RENDERER_CONFIG_H_
