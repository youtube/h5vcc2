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

#include "starboard/shared/starboard/media/parsed_mime_info.h"

#include <cmath>
#include <ostream>
#include <string>

#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/string.h"
#include "starboard/shared/starboard/media/codec_util.h"

namespace starboard {

namespace {

const int64_t kDefaultAudioChannels = 2;

// Turns |eotf| into value of SbMediaTransferId.  If |eotf| isn't recognized the
// function returns kSbMediaTransferIdUnknown.
// This function supports all eotfs required by YouTube TV HTML5 Technical
// Requirements.
SbMediaTransferId GetTransferIdFromString(const std::string& transfer_id) {
  if (transfer_id == "bt709") {
    return kSbMediaTransferIdBt709;
  } else if (transfer_id == "smpte2084") {
    return kSbMediaTransferIdSmpteSt2084;
  } else if (transfer_id == "arib-std-b67") {
    return kSbMediaTransferIdAribStdB67;
  }
  return kSbMediaTransferIdUnknown;
}

bool ParseAudioInfo(const MimeType& mime_type,
                    const std::string& codec,
                    ParsedMimeInfo::AudioCodecInfo* audio_info);

bool ParseVideoInfo(const MimeType& mime_type,
                    const std::string& codec,
                    ParsedMimeInfo::VideoCodecInfo* video_info);

}  // namespace

// static
std::optional<ParsedMimeInfo> ParsedMimeInfo::Create(
    const std::string& mime_string) {
  auto mime_type = MimeType::Create(mime_string);
  if (!mime_type) {
    return std::nullopt;
  }

  // Read "disablecache".
  if (!mime_type->ValidateBoolParameter("disablecache")) {
    return std::nullopt;
  }
  bool disable_cache = mime_type->GetParamBoolValue("disablecache", false);

  // We only support audio or video type.
  if (mime_type->type() != "audio" && mime_type->type() != "video") {
    return std::nullopt;
  }

  auto codecs = mime_type->GetCodecs();
  // We only support up to one audio codec and one video codec.
  if (codecs.size() > 2) {
    return std::nullopt;
  }

  AudioCodecInfo audio_info;
  VideoCodecInfo video_info;

  for (const auto& codec : codecs) {
    if (audio_info.codec == kSbMediaAudioCodecNone &&
        ParseAudioInfo(*mime_type, codec, &audio_info)) {
      continue;
    }
    if (video_info.codec == kSbMediaVideoCodecNone &&
        ParseVideoInfo(*mime_type, codec, &video_info)) {
      continue;
    }
    // It either has an invalid codec or has two codecs of same type.
    return std::nullopt;
  }

  return ParsedMimeInfo(std::move(*mime_type), disable_cache, audio_info,
                        video_info);
}

ParsedMimeInfo ParsedMimeInfo::WithBitrate(int bitrate) const {
  AudioCodecInfo audio_info = audio_info_;
  VideoCodecInfo video_info = video_info_;

  audio_info.bitrate = bitrate;
  video_info.bitrate = bitrate;

  return ParsedMimeInfo(mime_type_, disable_cache_, audio_info, video_info);
}

ParsedMimeInfo::ParsedMimeInfo(MimeType mime_type,
                               bool disable_cache,
                               AudioCodecInfo audio_info,
                               VideoCodecInfo video_info)
    : mime_type_(std::move(mime_type)),
      disable_cache_(disable_cache),
      audio_info_(audio_info),
      video_info_(video_info) {}

namespace {

bool ParseAudioInfo(const MimeType& mime_type,
                    const std::string& codec,
                    ParsedMimeInfo::AudioCodecInfo* audio_info) {
  SB_CHECK(audio_info);
  SB_CHECK_EQ(audio_info->codec, kSbMediaAudioCodecNone);

  SbMediaAudioCodec audio_codec =
      GetAudioCodecFromString(codec.c_str(), mime_type.subtype().c_str());
  if (audio_codec == kSbMediaAudioCodecNone) {
    return false;
  }
  if (!mime_type.ValidateIntParameter(MimeType::Param::kChannels) ||
      !mime_type.ValidateIntParameter(MimeType::Param::kBitrate)) {
    return false;
  }
  audio_info->codec = audio_codec;
  audio_info->channels = mime_type.GetParamIntValue(MimeType::Param::kChannels,
                                                    kDefaultAudioChannels);
  audio_info->bitrate =
      mime_type.GetParamIntValue(MimeType::Param::kBitrate, 0);

  return audio_info->channels >= 0 && audio_info->bitrate >= 0;
}

bool ParseVideoInfo(const MimeType& mime_type,
                    const std::string& codec,
                    ParsedMimeInfo::VideoCodecInfo* video_info) {
  SB_DCHECK(video_info);
  SB_DCHECK(video_info->codec == kSbMediaVideoCodecNone);

  if (!ParseVideoCodec(codec.c_str(), &video_info->codec, &video_info->profile,
                       &video_info->level, &video_info->bit_depth,
                       &video_info->primary_id, &video_info->transfer_id,
                       &video_info->matrix_id)) {
    return false;
  }

  if (video_info->codec == kSbMediaVideoCodecNone) {
    return false;
  }

  std::string eotf = mime_type.GetParamStringValue(MimeType::Param::kEotf, "");
  if (!eotf.empty()) {
    SbMediaTransferId transfer_id_from_eotf = GetTransferIdFromString(eotf);
    if (transfer_id_from_eotf == kSbMediaTransferIdUnknown) {
      // Web apps probe unknown or dummy EOTF values (e.g., "catavision")
      // via isTypeSupported() to verify MIME parameter validation.
      // This is a normal code path.
      return false;
    }
    SB_LOG_IF(WARNING,
              video_info->transfer_id != kSbMediaTransferIdUnspecified &&
                  video_info->transfer_id != transfer_id_from_eotf)
        << "transfer_id " << video_info->transfer_id
        << " set by the codec string \"" << video_info->codec
        << "\" will be overwritten by the eotf attribute " << eotf;
    video_info->transfer_id = transfer_id_from_eotf;
  }

  if (!mime_type.ValidateIntParameter(MimeType::Param::kWidth) ||
      !mime_type.ValidateIntParameter(MimeType::Param::kHeight) ||
      !mime_type.ValidateFloatParameter(MimeType::Param::kFramerate) ||
      !mime_type.ValidateIntParameter(MimeType::Param::kBitrate) ||
      !mime_type.ValidateBoolParameter(MimeType::Param::kDecodeToTexture)) {
    return false;
  }

  video_info->frame_width =
      mime_type.GetParamIntValue(MimeType::Param::kWidth, 0);
  video_info->frame_height =
      mime_type.GetParamIntValue(MimeType::Param::kHeight, 0);
  // TODO: Support float framerate. Our starboard implementation only supports
  // integer framerate, but framerate could be float and we should support it.
  float framerate =
      mime_type.GetParamFloatValue(MimeType::Param::kFramerate, 0.0f);
  video_info->fps = std::round(framerate);
  video_info->bitrate =
      mime_type.GetParamIntValue(MimeType::Param::kBitrate, 0);
  video_info->decode_to_texture_required =
      mime_type.GetParamBoolValue(MimeType::Param::kDecodeToTexture, false);

  return video_info->frame_width >= 0 && video_info->frame_height >= 0 &&
         video_info->fps >= 0 && video_info->bitrate >= 0;
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const ParsedMimeInfo& mime_info) {
  os << "ParsedMimeInfo={mime_type=" << mime_info.mime_type();
  if (mime_info.has_audio_info()) {
    const auto& audio = mime_info.audio_info();
    os << ", audio_info={codec=" << GetMediaAudioCodecName(audio.codec)
       << ", channels=" << audio.channels << "}";
  }
  if (mime_info.has_video_info()) {
    const auto& video = mime_info.video_info();
    os << ", video_info={codec=" << GetMediaVideoCodecName(video.codec)
       << ", profile=" << video.profile << ", level=" << video.level
       << ", bit_depth=" << video.bit_depth
       << ", primary_id=" << GetMediaPrimaryIdName(video.primary_id)
       << ", transfer_id=" << GetMediaTransferIdName(video.transfer_id)
       << ", matrix_id=" << GetMediaMatrixIdName(video.matrix_id)
       << ", width=" << video.frame_width << ", height=" << video.frame_height
       << ", fps=" << video.fps << ", decode_to_texture_required="
       << ToString(video.decode_to_texture_required) << "}";
  }
  os << "}";
  return os;
}

}  // namespace starboard
