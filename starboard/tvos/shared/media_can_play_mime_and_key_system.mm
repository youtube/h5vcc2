// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#import <AVFoundation/AVFoundation.h>

#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/string.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/codec_util.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/media/mime_type.h"
#include "starboard/shared/starboard/media/mime_util.h"
#include "starboard/tvos/shared/media/drm_system_platform.h"
#import "starboard/tvos/shared/media/playback_capabilities.h"

namespace {

const char kUrlPlayerMimeType[] = "application/x-mpegURL";
const size_t kUrlPlayerMimeTypeLength = strlen(kUrlPlayerMimeType);
const int64_t kDefaultAudioChannels = 2;

bool IsAudioCodecSupportedByUrlPlayer(const starboard::MimeType& mime_type,
                                      const std::string& codec) {
  SbMediaAudioCodec audio_codec = starboard::GetAudioCodecFromString(
      codec.c_str(), mime_type.subtype().c_str());
  if (audio_codec != kSbMediaAudioCodecAac &&
      audio_codec != kSbMediaAudioCodecAc3 &&
      audio_codec != kSbMediaAudioCodecEac3) {
    return false;
  }

  int channels = mime_type.GetParamIntValue(MimeType::Param::kChannels,
                                            kDefaultAudioChannels);
  if (channels <= kDefaultAudioChannels) {
    return true;
  }
  return channels <=
         [AVAudioSession sharedInstance].maximumOutputNumberOfChannels;
}

bool IsVideoCodecSupportedByUrlPlayer(const std::string& codec) {
  SbMediaVideoCodec video_codec;
  int profile = -1;
  int level = -1;
  int bit_depth = 8;
  SbMediaPrimaryId primary_id = kSbMediaPrimaryIdUnspecified;
  SbMediaTransferId transfer_id = kSbMediaTransferIdUnspecified;
  SbMediaMatrixId matrix_id = kSbMediaMatrixIdUnspecified;
  if (!starboard::ParseVideoCodec(codec.c_str(), &video_codec, &profile, &level,
                                  &bit_depth, &primary_id, &transfer_id,
                                  &matrix_id)) {
    return false;
  }
  if (video_codec == kSbMediaVideoCodecH264) {
    return true;
  }
  if (video_codec == kSbMediaVideoCodecVp9) {
    return starboard::PlaybackCapabilities::IsHwVp9Supported();
  }
  return false;
}

}  // namespace

SbMediaSupportType SbMediaCanPlayMimeAndKeySystem(const char* mime,
                                                  const char* key_system) {
  if (mime == NULL) {
    SB_DLOG(WARNING) << "mime cannot be NULL";
    return kSbMediaSupportTypeNotSupported;
  }

  if (key_system == NULL) {
    SB_DLOG(WARNING) << "key_system cannot be NULL";
    return kSbMediaSupportTypeNotSupported;
  }

  if (strchr(key_system, ';')) {
    // TODO: Remove this check and enable key system with attributes support.
    return kSbMediaSupportTypeNotSupported;
  }

  // "application/x-mpegURL" is for hls content and will use UrlPlayer. The
  // supported types are different from SbPlayer.
  if (strncasecmp(kUrlPlayerMimeType, mime, kUrlPlayerMimeTypeLength) == 0) {
    auto mime_type = starboard::MimeType::Create(mime);
    if (!mime_type) {
      return kSbMediaSupportTypeNotSupported;
    }
    auto codecs = mime_type->GetCodecs();
    for (const auto& codec : codecs) {
      if (!IsAudioCodecSupportedByUrlPlayer(*mime_type, codec) &&
          !IsVideoCodecSupportedByUrlPlayer(codec)) {
        return kSbMediaSupportTypeNotSupported;
      }
    }
    // UrlPlayer only supports what DrmSystemPlatform supports.
    if (strlen(key_system) > 0 &&
        key_system != starboard::DrmSystemPlatform::GetKeySystemName()) {
      return kSbMediaSupportTypeNotSupported;
    }

    return kSbMediaSupportTypeProbably;
  }

  return starboard::CanPlayMimeAndKeySystem(mime, key_system);
}
