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

#include "starboard/android/shared/media_codec.h"

#include <android/api-level.h>

#include <memory>
#include <optional>
#include <string>

#include "starboard/android/shared/media_capabilities_cache.h"
#include "starboard/android/shared/media_codec_bridge.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/android/shared/ndk_media_codec.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/string.h"
#include "starboard/shared/starboard/features.h"

namespace starboard {
namespace {

bool CanUseNdkMediaCodec(std::optional<int> tunnel_mode_audio_session_id,
                         bool require_secured_decoder,
                         const jni_zero::JavaRef<jobject>& j_media_crypto,
                         const SbMediaColorMetadata* color_metadata) {
  if (!features::FeatureList::IsEnabled(features::kNdkVideo)) {
    return false;
  }

  // We do not use NDK AMediaCodec for DRM, since it requires architectural
  // changes.
  if (require_secured_decoder || j_media_crypto) {
    return false;
  }
  // We do not use NDK AMediaCodec for HDR for now.
  if (color_metadata) {
    return false;
  }
  // NDK AMediaCodec does not support tunnel mode.
  if (tunnel_mode_audio_session_id) {
    return false;
  }
  // NDK AMediaCodec requires API level >= 28.
  if (android_get_device_api_level() < 28) {
    return false;
  }

  return true;
}

}  // namespace

std::unique_ptr<MediaCodec> DefaultMediaCodecFactory::CreateAudioMediaCodec(
    const AudioStreamInfo& audio_stream_info,
    MediaCodec::Handler* handler,
    const jni_zero::JavaRef<jobject>& j_media_crypto) {
  return MediaCodecBridge::CreateAudioMediaCodec(audio_stream_info, handler,
                                                 j_media_crypto);
}

NonNullResult<std::unique_ptr<MediaCodec>>
DefaultMediaCodecFactory::CreateVideoMediaCodec(
    SbMediaVideoCodec video_codec,
    const Size& frame_size_hint,
    int fps,
    const std::optional<Size>& max_frame_size,
    MediaCodec::Handler* handler,
    const jni_zero::JavaRef<jobject>& j_surface,
    const jni_zero::JavaRef<jobject>& j_media_crypto,
    const SbMediaColorMetadata* color_metadata,
    const MediaCodec::VideoPlatformOptions& platform_options) {
  if (max_frame_size) {
    SB_CHECK_GT(max_frame_size->width, 0);
    SB_CHECK_GT(max_frame_size->height, 0);
  }

  const char* mime = SupportedVideoCodecToMimeType(video_codec);
  if (!mime) {
    return Failure(std::string("Unsupported mime for codec: ") +
                   GetMediaVideoCodecName(video_codec));
  }

  const bool must_support_secure = platform_options.require_secured_decoder;
  const bool must_support_hdr = color_metadata;
  const bool must_support_tunnel_mode =
      platform_options.tunnel_mode_audio_session_id.has_value();

  std::string decoder_name =
      MediaCapabilitiesCache::GetInstance()->FindVideoDecoder(
          mime, must_support_secure, must_support_hdr,
          platform_options.require_software_codec, must_support_tunnel_mode);
  if (decoder_name.empty() && color_metadata) {
    decoder_name = MediaCapabilitiesCache::GetInstance()->FindVideoDecoder(
        mime, must_support_secure, /*must_support_hdr=*/false,
        platform_options.require_software_codec, must_support_tunnel_mode);
  }
  if (decoder_name.empty() && platform_options.require_software_codec) {
    decoder_name = MediaCapabilitiesCache::GetInstance()->FindVideoDecoder(
        mime, must_support_secure, /*must_support_hdr=*/false,
        /*require_software_codec=*/false, must_support_tunnel_mode);
  }

  if (decoder_name.empty()) {
    return Failure(
        FormatString("Failed to find decoder: mime=%s, mustSupportSecure=%s",
                     mime, starboard::ToString(!!j_media_crypto).data()));
  }

  if (CanUseNdkMediaCodec(platform_options.tunnel_mode_audio_session_id,
                          platform_options.require_secured_decoder,
                          j_media_crypto, color_metadata)) {
    auto ndk_bridge = NdkMediaCodec::Create(
        video_codec, decoder_name, frame_size_hint, fps, max_frame_size,
        handler, j_surface, platform_options.enable_frame_renderer_listener,
        platform_options.max_input_size);
    if (ndk_bridge) {
      return ndk_bridge;
    }
    SB_LOG(WARNING)
        << "Failed to create NdkMediaCodec. Falling back to Java MediaCodec.";
  }

  auto jni_result = MediaCodecBridge::CreateVideoMediaCodec(
      video_codec, decoder_name, mime, frame_size_hint, fps, max_frame_size,
      handler, j_surface, j_media_crypto, color_metadata, platform_options);
  if (jni_result) {
    return std::move(jni_result.value());
  }
  return Failure(jni_result.error());
}

// static
std::unique_ptr<MediaCodec> MediaCodec::CreateAudioMediaCodec(
    const AudioStreamInfo& audio_stream_info,
    Handler* handler,
    const jni_zero::JavaRef<jobject>& j_media_crypto) {
  DefaultMediaCodecFactory factory;
  return factory.CreateAudioMediaCodec(audio_stream_info, handler,
                                       j_media_crypto);
}

// static
NonNullResult<std::unique_ptr<MediaCodec>> MediaCodec::CreateVideoMediaCodec(
    SbMediaVideoCodec video_codec,
    const Size& frame_size_hint,
    int fps,
    const std::optional<Size>& max_frame_size,
    Handler* handler,
    const jni_zero::JavaRef<jobject>& j_surface,
    const jni_zero::JavaRef<jobject>& j_media_crypto,
    const SbMediaColorMetadata* color_metadata,
    const MediaCodec::VideoPlatformOptions& platform_options) {
  DefaultMediaCodecFactory factory;
  return factory.CreateVideoMediaCodec(
      video_codec, frame_size_hint, fps, max_frame_size, handler, j_surface,
      j_media_crypto, color_metadata, platform_options);
}

// static
bool MediaCodec::IsFrameRenderedCallbackEnabled() {
  return android_get_device_api_level() >= 34;
}

FrameSize::FrameSize() : FrameSize(Size(), /*has_crop_values=*/false) {}

FrameSize::FrameSize(Size display_size, bool has_crop_values)
    : display_size(display_size), has_crop_values(has_crop_values) {
  SB_CHECK_GE(display_size.width, 0);
  SB_CHECK_GE(display_size.height, 0);
}

std::ostream& operator<<(std::ostream& os, const FrameSize& size) {
  return os << "{display_size=" << size.display_size
            << ", has_crop_values=" << starboard::ToString(size.has_crop_values)
            << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const MediaCodec::VideoPlatformOptions& options) {
  return os << "{max_input_size=" << options.max_input_size
            << ", skip_video_frames_over_60_fps="
            << starboard::ToString(options.skip_video_frames_over_60_fps)
            << ", ignore_mediacodec_callbacks_during_flushing="
            << starboard::ToString(
                   options.ignore_mediacodec_callbacks_during_flushing)
            << ", enable_frame_renderer_listener="
            << starboard::ToString(options.enable_frame_renderer_listener)
            << ", enable_low_latency="
            << starboard::ToString(options.enable_low_latency)
            << ", require_secured_decoder="
            << starboard::ToString(options.require_secured_decoder)
            << ", require_software_codec="
            << starboard::ToString(options.require_software_codec)
            << ", force_big_endian_hdr_metadata="
            << starboard::ToString(options.force_big_endian_hdr_metadata)
            << ", tunnel_mode_audio_session_id="
            << starboard::ToString(options.tunnel_mode_audio_session_id) << "}";
}

}  // namespace starboard
