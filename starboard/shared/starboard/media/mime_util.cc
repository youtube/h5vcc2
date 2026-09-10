// Copyright 2022 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/media/mime_util.h"

#include <cstring>
#include <string>
#include <vector>

#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/log.h"
#include "starboard/shared/starboard/media/key_system_supportability_cache.h"
#include "starboard/shared/starboard/media/media_support_internal.h"
#include "starboard/shared/starboard/media/mime_supportability_cache.h"
#include "starboard/shared/starboard/media/mime_type.h"
#include "starboard/shared/starboard/media/parsed_mime_info.h"

namespace starboard {

namespace {

// Use SbMediaGetAudioConfiguration() to check if the platform can support
// |channels|.
bool IsAudioOutputSupported(SbMediaAudioCodingType coding_type, int channels) {
  // SbPlayerBridge::GetAudioConfigurations() reads up to 32 configurations. The
  // limit here is to avoid infinite loop and also match
  // SbPlayerBridge::GetAudioConfigurations().
  const int kMaxAudioConfigurations = 32;
  int output_index = 0;
  SbMediaAudioConfiguration configuration;
  while (output_index < kMaxAudioConfigurations &&
         SbMediaGetAudioConfiguration(output_index++, &configuration)) {
    if (configuration.coding_type == coding_type &&
        configuration.number_of_channels >= channels) {
      return true;
    }
  }
  return false;
}

bool IsSupportedKeySystem(SbMediaAudioCodec codec, const char* key_system) {
  SB_DCHECK(key_system);
  // KeySystemSupportabilityCache() should always return supported for empty
  // |key_system|, so here it should always be non empty.
  SB_DCHECK_GT(strlen(key_system), 0U);

  return MediaIsKeySystemSupported(kSbMediaVideoCodecNone, codec, key_system);
}

bool IsSupportedKeySystem(SbMediaVideoCodec codec, const char* key_system) {
  SB_DCHECK(key_system);
  // KeySystemSupportabilityCache() should always return supported for empty
  // |key_system|, so here it should always be non empty.
  SB_DCHECK_GT(strlen(key_system), 0U);

  return MediaIsKeySystemSupported(codec, kSbMediaAudioCodecNone, key_system);
}

bool IsSupportedAudioCodec(const ParsedMimeInfo& mime_info) {
  SB_DCHECK(mime_info.has_audio_info());

  const MimeType& mime_type = mime_info.mime_type();
  const ParsedMimeInfo::AudioCodecInfo& audio_info = mime_info.audio_info();

  switch (audio_info.codec) {
    case kSbMediaAudioCodecNone:
      SB_NOTREACHED();
      return false;
    case kSbMediaAudioCodecAac:
    case kSbMediaAudioCodecAc3:
    case kSbMediaAudioCodecEac3:
      if (mime_type.subtype() != "mp4") {
        return false;
      }
      break;
    case kSbMediaAudioCodecOpus:
    case kSbMediaAudioCodecVorbis:
      if (mime_type.subtype() != "webm") {
        return false;
      }
      break;
    case kSbMediaAudioCodecMp3:
      if (mime_type.subtype() != "mpeg" && mime_type.subtype() != "mp3") {
        return false;
      }
      break;
    case kSbMediaAudioCodecFlac:
      if (mime_type.subtype() != "ogg" && mime_type.subtype() != "flac") {
        return false;
      }
      break;
    case kSbMediaAudioCodecPcm:
      if (mime_type.subtype() != "wav") {
        return false;
      }
      break;
    case kSbMediaAudioCodecIamf:
      if (mime_type.subtype() != "mp4") {
        return false;
      }
      break;
  }

  if (!IsAudioOutputSupported(kSbMediaAudioCodingTypePcm,
                              audio_info.channels)) {
    return false;
  }

  return MediaIsAudioSupported(audio_info.codec, &mime_type,
                               audio_info.bitrate);
}

bool IsSupportedVideoCodec(const ParsedMimeInfo& mime_info) {
  SB_DCHECK(mime_info.has_video_info());

  const MimeType& mime_type = mime_info.mime_type();
  const ParsedMimeInfo::VideoCodecInfo& video_info = mime_info.video_info();

  switch (video_info.codec) {
    case kSbMediaVideoCodecNone:
      SB_NOTREACHED();
      return false;
    case kSbMediaVideoCodecH264:
    case kSbMediaVideoCodecH265:
      if (mime_type.subtype() != "mp4") {
        return false;
      }
      break;
    case kSbMediaVideoCodecMpeg2:
    case kSbMediaVideoCodecTheora:
      return false;  // No associated container in YT.
    case kSbMediaVideoCodecVc1:
    case kSbMediaVideoCodecAv1:
      if (mime_type.subtype() != "mp4") {
        return false;
      }
      break;
    case kSbMediaVideoCodecVp8:
      if (mime_type.subtype() != "webm") {
        return false;
      }
      break;
    case kSbMediaVideoCodecVp9:
      if (mime_type.subtype() != "mp4" && mime_type.subtype() != "webm") {
        return false;
      }
      break;
    case kSbMediaVideoCodecAv2:
      return false;
  }

  std::string cryptoblockformat =
      mime_type.GetParamStringValue(MimeType::Param::kCryptoblockformat, "");
  if (!cryptoblockformat.empty()) {
    if ((mime_type.subtype() != "mp4" && mime_type.subtype() != "webm") ||
        cryptoblockformat != "subsample") {
      return false;
    }
  }

  return MediaIsVideoSupported(
      video_info.codec, &mime_type, video_info.profile, video_info.level,
      video_info.bit_depth, video_info.primary_id, video_info.transfer_id,
      video_info.matrix_id, video_info.frame_width, video_info.frame_height,
      video_info.bitrate, video_info.fps,
      video_info.decode_to_texture_required);
}

}  // namespace

SbMediaSupportType CanPlayMimeAndKeySystem(const char* mime,
                                           const char* key_system) {
  SB_DCHECK(mime);
  SB_DCHECK(key_system);

  // Get cached ParsedMimeInfo with its supportability. If it is not found in
  // the cache, MimeSupportabilityCache would parse the mime string and return
  // the ParsedMimeInfo with kSupportabilityUnknown.
  auto result =
      MimeSupportabilityCache::GetInstance()->GetMimeSupportability(mime);
  Supportability mime_supportability = result.supportability;
  const std::optional<ParsedMimeInfo>& mime_info = result.mime_info;

  if (mime_info && mime_info->disable_cache()) {
    // Disable all caches if required.
    mime_supportability = kSupportabilityUnknown;
    MimeSupportabilityCache::GetInstance()->SetCacheEnabled(false);
    KeySystemSupportabilityCache::GetInstance()->SetCacheEnabled(false);
  }

  // Reject mime if cached result is not supported.
  if (mime_supportability == kSupportabilityNotSupported) {
    return kSbMediaSupportTypeNotSupported;
  }

  // MimeSupportabilityCache::GetMimeSupportability() returns
  // kSupportabilityNotSupported if ParsedMimeInfo is not valid, so |mime_info|
  // must be valid here.
  SB_DCHECK(mime_info);

  const MimeType& mime_type = mime_info->mime_type();
  const std::vector<std::string>& codecs = mime_type.GetCodecs();

  // Quick check for mp4 format.
  if (codecs.size() == 0) {
    // This happens when the H5 player is either querying for progressive
    // playback support, or probing for generic mp4 support without specific
    // codecs.
    if (mime_type.subtype() == "mp4") {
      return kSbMediaSupportTypeMaybe;
    } else {
      return kSbMediaSupportTypeNotSupported;
    }
  }

  // Reject mime if it doesn't have any valid codec info.
  if (!mime_info->has_audio_info() && !mime_info->has_video_info()) {
    return kSbMediaSupportTypeNotSupported;
  }

  // Get cached key system supportability. Note that we check if audio or video
  // codec supports key system separately.
  if (mime_info->has_audio_info()) {
    Supportability key_system_supportability =
        KeySystemSupportabilityCache::GetInstance()->GetKeySystemSupportability(
            mime_info->audio_info().codec, key_system);
    if (key_system_supportability == kSupportabilityUnknown) {
      key_system_supportability =
          IsSupportedKeySystem(mime_info->audio_info().codec, key_system)
              ? kSupportabilitySupported
              : kSupportabilityNotSupported;
      KeySystemSupportabilityCache::GetInstance()->CacheKeySystemSupportability(
          mime_info->audio_info().codec, key_system, key_system_supportability);
    }
    // Reject mime if audio codec doesn't support the key system.
    if (key_system_supportability == kSupportabilityNotSupported) {
      return kSbMediaSupportTypeNotSupported;
    }
  }
  if (mime_info->has_video_info()) {
    Supportability key_system_supportability =
        KeySystemSupportabilityCache::GetInstance()->GetKeySystemSupportability(
            mime_info->video_info().codec, key_system);
    if (key_system_supportability == kSupportabilityUnknown) {
      key_system_supportability =
          IsSupportedKeySystem(mime_info->video_info().codec, key_system)
              ? kSupportabilitySupported
              : kSupportabilityNotSupported;
      KeySystemSupportabilityCache::GetInstance()->CacheKeySystemSupportability(
          mime_info->video_info().codec, key_system, key_system_supportability);
    }
    // Reject mime if video codec doesn't the key system.
    if (key_system_supportability == kSupportabilityNotSupported) {
      return kSbMediaSupportTypeNotSupported;
    }
  }

  // At this point, |key_system| is supported. Return supported here if
  // mime is also supported. Otherwise, |mime_supportability| must be unknown.
  if (mime_supportability == kSupportabilitySupported) {
    return kSbMediaSupportTypeProbably;
  }
  SB_DCHECK_EQ(mime_supportability, kSupportabilityUnknown);

  // Call platform functions to check if it's supported.
  if (mime_info->has_audio_info() && !IsSupportedAudioCodec(*mime_info)) {
    mime_supportability = kSupportabilityNotSupported;
  } else if (mime_info->has_video_info() &&
             !IsSupportedVideoCodec(*mime_info)) {
    mime_supportability = kSupportabilityNotSupported;
  } else {
    mime_supportability = kSupportabilitySupported;
  }

  // Cache mime supportability.
  MimeSupportabilityCache::GetInstance()->CacheMimeSupportability(
      mime, mime_supportability);

  return mime_supportability == kSupportabilitySupported
             ? kSbMediaSupportTypeProbably
             : kSbMediaSupportTypeNotSupported;
}

}  // namespace starboard
