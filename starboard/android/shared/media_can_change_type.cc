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

// clang-format off
#include "starboard/media.h"
// clang-format on

#include "starboard/common/log.h"
#include "starboard/shared/starboard/media/parsed_mime_info.h"

bool SbMediaCanChangeType(const char* current_mime, const char* new_mime) {
  if (current_mime == NULL) {
    SB_DLOG(WARNING) << "current_mime cannot be NULL.";
    return false;
  }
  if (new_mime == NULL) {
    SB_DLOG(WARNING) << "new_mime cannot be NULL.";
    return false;
  }

  auto current_mime_info = starboard::ParsedMimeInfo::Create(current_mime);
  if (!current_mime_info) {
    SB_DLOG(WARNING) << "Failed to parse current_mime: " << current_mime;
    return false;
  }

  auto new_mime_info = starboard::ParsedMimeInfo::Create(new_mime);
  if (!new_mime_info) {
    SB_DLOG(WARNING) << "Failed to parse new_mime: " << new_mime;
    return false;
  }

  // Reject stream media type mismatches (e.g. Video -> Audio or vice versa).
  if (current_mime_info->has_video_info() != new_mime_info->has_video_info() ||
      current_mime_info->has_audio_info() != new_mime_info->has_audio_info()) {
    SB_DLOG(WARNING) << "MIME stream type mismatch between " << current_mime
                     << " and " << new_mime;
    return false;
  }

  // Reject cross-family video codec switches (e.g. VP9 -> AV1).
  if (current_mime_info->has_video_info() && new_mime_info->has_video_info() &&
      current_mime_info->video_info().codec !=
          new_mime_info->video_info().codec) {
    SB_DLOG(WARNING) << "Cannot change video codec family from "
                     << current_mime_info->video_info().codec << " to "
                     << new_mime_info->video_info().codec;
    return false;
  }

  // Reject cross-family audio codec switches (e.g. AAC -> Opus).
  if (current_mime_info->has_audio_info() && new_mime_info->has_audio_info() &&
      current_mime_info->audio_info().codec !=
          new_mime_info->audio_info().codec) {
    SB_DLOG(WARNING) << "Cannot change audio codec family from "
                     << current_mime_info->audio_info().codec << " to "
                     << new_mime_info->audio_info().codec;
    return false;
  }

  return true;
}
