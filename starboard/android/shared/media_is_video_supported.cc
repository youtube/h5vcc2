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
#include "starboard/shared/starboard/media/media_support_internal.h"
// clang-format on

#include <android/api-level.h>

#include "starboard/android/shared/max_media_codec_output_buffers_lookup_table.h"
#include "starboard/android/shared/media_capabilities_cache.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/common/size.h"
#include "starboard/configuration.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/media_util.h"

namespace starboard {

bool MediaIsVideoSupported(SbMediaVideoCodec video_codec,
                           const MimeType* mime_type,
                           int profile,
                           int level,
                           int bit_depth,
                           SbMediaPrimaryId primary_id,
                           SbMediaTransferId transfer_id,
                           SbMediaMatrixId matrix_id,
                           int frame_width,
                           int frame_height,
                           int64_t bitrate,
                           int fps,
                           bool decode_to_texture_required) {
  const bool must_support_hdr =
      !IsSDRVideo(bit_depth, primary_id, transfer_id, matrix_id);
  if (must_support_hdr &&
      !MediaCapabilitiesCache::GetInstance()
           ->IsHDRTransferCharacteristicsSupported(transfer_id)) {
    return false;
  }
  // While not necessarily true, for now we assume that all Android devices
  // can play decode-to-texture video just as well as normal video.

  bool must_support_tunnel_mode = false;
  if (mime_type) {
    // Allows for enabling tunneled playback. Disabled by default.
    // https://source.android.com/devices/tv/multimedia-tunneling
    if (!mime_type->ValidateBoolParameter(MimeType::Param::kTunnelMode)) {
      return false;
    }
    must_support_tunnel_mode =
        mime_type->GetParamBoolValue(MimeType::Param::kTunnelMode, false);

    // Allow the web app to control how software decoders should be used.
    if (!mime_type->ValidateStringParameter(
            "softwaredecoder",
            "allowed|disallowed|preferred|unpreferred|required")) {
      return false;
    }

    // Disable MediaCapabilitiesCache if "disablecache" option presented.
    if (!mime_type->ValidateBoolParameter("disablecache")) {
      return false;
    }
    if (mime_type->GetParamBoolValue("disablecache", false)) {
      MediaCapabilitiesCache::GetInstance()->SetCacheEnabled(false);
    }

    if (!mime_type->ValidateBoolParameter("disabledynamicprerollframecount")) {
      return false;
    }
    if (mime_type->GetParamBoolValue("disabledynamicprerollframecount",
                                     false)) {
      MaxMediaCodecOutputBuffersLookupTable::GetInstance()->SetEnabled(false);
    }
  }

  if (must_support_tunnel_mode && decode_to_texture_required) {
    SB_LOG(WARNING) << "Tunnel mode is rejected because output mode decode to "
                       "texture is required but not supported.";
    return false;
  }

  if (must_support_tunnel_mode && android_get_device_api_level() < 34) {
    SB_LOG(WARNING)
        << "Tunnel mode is rejected because Android version is less "
           "than 14.";
    return false;
  }

  const char* mime = SupportedVideoCodecToMimeType(video_codec);
  if (!mime) {
    return false;
  }

  // We assume that if a device supports a format for clear playback, it will
  // also support it for encrypted playback. However, some devices require
  // tunneled playback to be encrypted, so we must align the tunnel mode
  // requirement with the secure playback requirement.
  const bool require_secure_playback = must_support_tunnel_mode;

  return MediaCapabilitiesCache::GetInstance()->HasVideoDecoderFor(
      mime, require_secure_playback, must_support_hdr, must_support_tunnel_mode,
      Size(frame_width, frame_height), bitrate, fps);
}

}  // namespace starboard
