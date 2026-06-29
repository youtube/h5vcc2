// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

// clang-format off
#include "starboard/player.h"
// clang-format on

#include <string>
#include <utility>

#include "starboard/android/shared/exoplayer/exoplayer_player_worker_handler.h"
#include "starboard/android/shared/video_max_video_input_size.h"
#include "starboard/android/shared/video_surface_view.h"
#include "starboard/android/shared/video_window.h"
#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/string.h"
#include "starboard/configuration.h"
#include "starboard/decode_target.h"
#include "starboard/shared/starboard/experimental_features.h"
#include "starboard/shared/starboard/features.h"
#include "starboard/shared/starboard/media/media_tracing.h"
#include "starboard/shared/starboard/player/filter/filter_based_player_worker_handler.h"
#include "starboard/shared/starboard/player/player_internal.h"
#include "starboard/shared/starboard/player/player_worker.h"

using starboard::ExoPlayerPlayerWorkerHandler;
using starboard::FilterBasedPlayerWorkerHandler;
using starboard::PlayerWorker;

SbPlayer SbPlayerCreate(SbWindow /*window*/,
                        const SbPlayerCreationParam* creation_param,
                        SbPlayerDeallocateSampleFunc sample_deallocate_func,
                        SbPlayerDecoderStatusFunc decoder_status_func,
                        SbPlayerStatusFunc player_status_func,
                        SbPlayerErrorFunc player_error_func,
                        void* context,
                        SbDecodeTargetGraphicsContextProvider* provider) {
  // Lazy initialization of media specific event tracing.  See comment in
  // EnsureMediaTracingIsInitialized() for limitations.
  EnsureMediaTracingIsInitialized();

  if (!player_error_func) {
    SB_LOG(ERROR) << "|player_error_func| cannot be null.";
    return kSbPlayerInvalid;
  }

  if (!creation_param) {
    SB_LOG(ERROR) << "CreationParam cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "CreationParam cannot be null");
    return kSbPlayerInvalid;
  }

  const SbMediaAudioStreamInfo& audio_stream_info =
      creation_param->audio_stream_info;
  const SbMediaVideoStreamInfo& video_stream_info =
      creation_param->video_stream_info;

  bool has_audio = audio_stream_info.codec != kSbMediaAudioCodecNone;
  bool has_video = video_stream_info.codec != kSbMediaVideoCodecNone;

  const char* audio_mime = has_audio ? audio_stream_info.mime : "";
  const char* video_mime = has_video ? video_stream_info.mime : "";
  const char* max_video_capabilities =
      has_video ? video_stream_info.max_video_capabilities : "";

  if (!audio_mime) {
    SB_LOG(ERROR) << "creation_param->audio_stream_info.mime cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "creation_param->audio_stream_info.mime cannot be null");
    return kSbPlayerInvalid;
  }
  if (!video_mime) {
    SB_LOG(ERROR) << "creation_param->video_stream_info.mime cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "creation_param->video_stream_info.mime cannot be null");
    return kSbPlayerInvalid;
  }
  if (!max_video_capabilities) {
    SB_LOG(ERROR) << "creation_param->video_stream_info.max_video_capabilities"
                  << " cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "creation_param->video_stream_info.max_video_"
                      "capabilities cannot be null");
    return kSbPlayerInvalid;
  }

  SB_LOG(INFO) << "SbPlayerCreate() called with audio mime \"" << audio_mime
               << "\", video mime \"" << video_mime
               << "\", and max video capabilities \"" << max_video_capabilities
               << "\".";

  if (!sample_deallocate_func) {
    SB_LOG(ERROR) << "|sample_deallocate_func| cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "|sample_deallocate_func| cannot be null");
    return kSbPlayerInvalid;
  }

  if (!decoder_status_func) {
    SB_LOG(ERROR) << "|decoder_status_func| cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "|decoder_status_func| cannot be null");
    return kSbPlayerInvalid;
  }

  if (!player_status_func) {
    SB_LOG(ERROR) << "|player_status_func| cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "|player_status_func| cannot be null");
    return kSbPlayerInvalid;
  }

  auto audio_codec = audio_stream_info.codec;
  auto video_codec = video_stream_info.codec;

  if (audio_codec != kSbMediaAudioCodecNone &&
      audio_codec != kSbMediaAudioCodecAac &&
      audio_codec != kSbMediaAudioCodecAc3 &&
      audio_codec != kSbMediaAudioCodecEac3 &&
      audio_codec != kSbMediaAudioCodecOpus) {
    SB_LOG(ERROR) << "Unsupported audio codec: "
                  << starboard::GetMediaAudioCodecName(audio_codec) << ".";
    player_error_func(
        kSbPlayerInvalid, context, kSbPlayerErrorDecode,
        starboard::FormatString("Unsupported audio codec: %s",
                                starboard::GetMediaAudioCodecName(audio_codec))
            .c_str());
    return kSbPlayerInvalid;
  }

  if (video_codec != kSbMediaVideoCodecNone &&
      video_codec != kSbMediaVideoCodecH264 &&
      video_codec != kSbMediaVideoCodecH265 &&
      video_codec != kSbMediaVideoCodecVp9 &&
      video_codec != kSbMediaVideoCodecAv1) {
    SB_LOG(ERROR) << "Unsupported video codec: "
                  << starboard::GetMediaVideoCodecName(video_codec) << ".";
    player_error_func(
        kSbPlayerInvalid, context, kSbPlayerErrorDecode,
        starboard::FormatString("Unsupported video codec: %s",
                                starboard::GetMediaVideoCodecName(video_codec))
            .c_str());
    return kSbPlayerInvalid;
  }

  if (audio_codec == kSbMediaAudioCodecNone &&
      video_codec == kSbMediaVideoCodecNone) {
    SB_LOG(ERROR) << "SbPlayerCreate() requires at least one audio track or"
                  << " one video track.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "SbPlayerCreate() requires at least one audio track or  "
                      "one video track");
    return kSbPlayerInvalid;
  }

  std::string error_message;
  if (has_audio &&
      audio_stream_info.number_of_channels > SbAudioSinkGetMaxChannels()) {
    error_message = starboard::FormatString(
        "Number of audio channels (%d) exceeds the maximum number of audio "
        "channels supported by this platform (%d)",
        audio_stream_info.number_of_channels, SbAudioSinkGetMaxChannels());
    SB_LOG(ERROR) << error_message << ".";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      error_message.c_str());
    return kSbPlayerInvalid;
  }

  auto output_mode = creation_param->output_mode;
  if (SbPlayerGetPreferredOutputMode(creation_param) != output_mode) {
    error_message = starboard::FormatString(
        "Unsupported player output mode: %d", output_mode);
    SB_LOG(ERROR) << error_message << ".";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      error_message.c_str());
    return kSbPlayerInvalid;
  }

  if (creation_param->output_mode != kSbPlayerOutputModeDecodeToTexture &&
      // TODO: This is temporary for supporting background media playback.
      //       Need to be removed with media refactor.
      //
      // Now this code is also used to avoid creating multiple punch-out player
      // as it happens to work as is.  Note that even without the check here,
      // SbPlayer will properly handle this by reporting error in VideoDecoder,
      // when it fails to acquire the surface.
      video_codec != kSbMediaVideoCodecNone) {
    // Check the availability of the video window. As we only support one main
    // player, and sub players are in decode to texture mode on Android, a
    // single video window should be enough.
    if (!starboard::VideoSurfaceHolder::IsVideoSurfaceAvailable() &&
        !starboard::GetSurfaceViewForCurrentThread()) {
      SB_LOG(ERROR) << "Video surface is not available now.";
      player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                        "Video surface is not available now");
      return kSbPlayerInvalid;
    }
  }

  bool use_exoplayer = starboard::features::FeatureList::IsEnabled(
      starboard::features::kEnableExoPlayer);
  if (use_exoplayer &&
      (creation_param->output_mode == kSbPlayerOutputModeDecodeToTexture ||
       creation_param->drm_system != kSbDrmSystemInvalid)) {
    SB_LOG(WARNING)
        << "ExoPlayer does not support decode-to-texture mode or DRM playback, "
           "defaulting to FilterBasedPlayerWorkerHandler.";
    use_exoplayer = false;
  }

  std::unique_ptr<PlayerWorker::Handler> handler;
  if (use_exoplayer) {
    handler = std::make_unique<ExoPlayerPlayerWorkerHandler>(creation_param);
  } else {
    handler = std::make_unique<FilterBasedPlayerWorkerHandler>(creation_param,
                                                               provider);
  }

  handler->SetMaxVideoInputSize(
      starboard::GetMaxVideoInputSizeForCurrentThread());
  handler->SetExperimentalFeatures(
      starboard::GetExperimentalFeaturesForCurrentThread());
  handler->SetVideoSurfaceView(starboard::GetSurfaceViewForCurrentThread());

  auto player = std::make_unique<starboard::SbPlayerPrivateImpl>(
      audio_codec, video_codec, sample_deallocate_func, decoder_status_func,
      player_status_func, player_error_func, context, std::move(handler));
  if (creation_param->output_mode != kSbPlayerOutputModeDecodeToTexture) {
    // TODO: accomplish this through more direct means.
    // Set the bounds to initialize the VideoSurfaceView. The initial values
    // don't matter.
    SbPlayerSetBounds(player.get(), 0, 0, 0, 0, 0);
  }
  return player.release();
}
