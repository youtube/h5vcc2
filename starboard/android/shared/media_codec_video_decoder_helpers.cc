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

#include "starboard/android/shared/media_codec_video_decoder_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/shared/starboard/media/mime_type.h"
#include "starboard/shared/starboard/media/resolutions.h"

namespace starboard {

bool IsSoftwareDecoderRequired(const std::string& max_video_capabilities) {
  if (max_video_capabilities.empty()) {
    return false;
  }

  auto mime_type =
      MimeType::Create("video/mp4; codecs=\"vp9\"; " + max_video_capabilities);
  if (!mime_type) {
    SB_LOG(INFO) << "Use hardware decoder as `max_video_capabilities` ("
                 << max_video_capabilities << ") is invalid.";
    return false;
  }

  std::string software_decoder_expectation =
      mime_type->GetParamStringValue("softwaredecoder", "");
  if (software_decoder_expectation == "required" ||
      software_decoder_expectation == "preferred") {
    SB_LOG(INFO) << "Use software decoder as `softwaredecoder` is set to \""
                 << software_decoder_expectation << "\".";
    return true;
  } else if (software_decoder_expectation == "disallowed" ||
             software_decoder_expectation == "unpreferred") {
    SB_LOG(INFO) << "Use hardware decoder as `softwaredecoder` is set to \""
                 << software_decoder_expectation << "\".";
    return false;
  }

  bool is_low_resolution =
      mime_type->GetParamIntValue(MimeType::Param::kWidth,
                                  Resolution::k1080p.width) <= 432 &&
      mime_type->GetParamIntValue(MimeType::Param::kHeight,
                                  Resolution::k1080p.height) <= 240;
  bool is_low_fps =
      mime_type->GetParamIntValue(MimeType::Param::kFramerate, 30) <= 15;

  if (!is_low_resolution || !is_low_fps) {
    SB_LOG(INFO)
        << "Use hardware decoder as `max_video_capabilities` is set to \""
        << max_video_capabilities << "\".";
    return false;
  }

  SB_LOG(INFO) << "Use software decoder as `max_video_capabilities` ("
               << max_video_capabilities
               << ") indicates a low resolution and low fps playback.";
  return true;
}

std::optional<Size> ParseMaxResolution(
    const std::string& max_video_capabilities,
    const Size& frame_size) {
  SB_DCHECK_GT(frame_size.width, 0);
  SB_DCHECK_GT(frame_size.height, 0);

  if (max_video_capabilities.empty()) {
    return std::nullopt;
  }

  SB_LOG(INFO) << "Try to parse max resolutions from `max_video_capabilities` ("
               << max_video_capabilities << ").";

  auto mime_type =
      MimeType::Create("video/mp4; codecs=\"vp9\"; " + max_video_capabilities);
  if (!mime_type) {
    SB_LOG(WARNING) << "Failed to parse max resolutions as "
                       "`max_video_capabilities` is invalid.";
    return std::nullopt;
  }

  int width = mime_type->GetParamIntValue(MimeType::Param::kWidth, -1);
  int height = mime_type->GetParamIntValue(MimeType::Param::kHeight, -1);
  if (width <= 0 && height <= 0) {
    SB_LOG(WARNING) << "Failed to parse max resolutions as either width or "
                       "height isn't set.";
    return std::nullopt;
  }
  if (width != -1 && height != -1) {
    Size max_size = {width, height};
    SB_LOG(INFO) << "Parsed max resolution=" << max_size;
    return max_size;
  }

  if (frame_size.IsEmpty()) {
    SB_LOG(WARNING)
        << "Failed to parse max resolutions due to invalid frame resolutions ("
        << frame_size << ").";
    return std::nullopt;
  }

  if (width > 0) {
    Size max_size = {width, width * frame_size.height / frame_size.width};
    SB_LOG(INFO) << "Inferred max size=" << max_size
                 << ", frame resolution=" << frame_size;
    return max_size;
  }

  Size max_size = {height * frame_size.width / frame_size.height, height};
  SB_LOG(INFO) << "Inferred max size=" << max_size
               << ", frame resolution=" << frame_size;
  return max_size;
}

bool Equal(const SbMediaMasteringMetadata& lhs,
           const SbMediaMasteringMetadata& rhs) {
  return std::memcmp(&lhs, &rhs, sizeof(SbMediaMasteringMetadata)) == 0;
}

bool IsIdentity(const SbMediaColorMetadata& color_metadata) {
  const SbMediaMasteringMetadata kEmptyMasteringMetadata = {};
  return color_metadata.primaries == kSbMediaPrimaryIdBt709 &&
         color_metadata.transfer == kSbMediaTransferIdBt709 &&
         color_metadata.matrix == kSbMediaMatrixIdBt709 &&
         color_metadata.range == kSbMediaRangeIdLimited &&
         Equal(color_metadata.mastering_metadata, kEmptyMasteringMetadata);
}

DecodeTargetGeometry GetDecodeTargetGeometryFromMatrix(
    const std::array<float, 16>& matrix4x4,
    const Size& display_size) {
  constexpr float kEpsilon = 1e-6f;
  SB_DCHECK_LT(std::abs(matrix4x4[1]), kEpsilon);
  SB_DCHECK_LT(std::abs(matrix4x4[2]), kEpsilon);
  SB_DCHECK_LT(std::abs(matrix4x4[3]), kEpsilon);

  SB_DCHECK_LT(std::abs(matrix4x4[4]), kEpsilon);
  SB_DCHECK_LT(std::abs(matrix4x4[6]), kEpsilon);
  SB_DCHECK_LT(std::abs(matrix4x4[7]), kEpsilon);

  SB_DCHECK_LT(std::abs(matrix4x4[8]), kEpsilon);
  SB_DCHECK_LT(std::abs(matrix4x4[9]), kEpsilon);
  SB_DCHECK_LT(std::abs(matrix4x4[10] - 1.0f), kEpsilon);
  SB_DCHECK_LT(std::abs(matrix4x4[11]), kEpsilon);

  SB_DCHECK_LT(std::abs(matrix4x4[14]), kEpsilon);
  SB_DCHECK_LT(std::abs(matrix4x4[15] - 1.0f), kEpsilon);

  float raw_sx = matrix4x4[0];
  float raw_sy = matrix4x4[5];

  float tx = raw_sx > 0 ? matrix4x4[12] : (raw_sx + matrix4x4[12]);
  float ty = raw_sy > 0 ? matrix4x4[13] : (raw_sy + matrix4x4[13]);

  float sx = std::abs(raw_sx);
  float sy = std::abs(raw_sy);

  Size coded_size = display_size;
  int visible_x = 0;
  int visible_y = 0;

  if (sx > kEpsilon && sy > kEpsilon) {
    const float possible_shrinks_amounts[] = {1.0f, 0.5f, 0.0f};
    for (const float& shrink_amount : possible_shrinks_amounts) {
      if (sx < 1.0f) {
        coded_size.width =
            std::round((display_size.width - 2.0f * shrink_amount) / sx);
        visible_x = std::round(tx * coded_size.width - shrink_amount);
      }
      if (sy < 1.0f) {
        coded_size.height =
            std::round((display_size.height - 2.0f * shrink_amount) / sy);
        visible_y = std::round(ty * coded_size.height - shrink_amount);
      }

      if (visible_x >= 0 && visible_y >= 0) {
        break;
      }
    }
  }

  float left = visible_x;
  float right = visible_x + display_size.width;
  float top = visible_y;
  float bottom = visible_y + display_size.height;

  if (raw_sx < 0) {
    std::swap(left, right);
  }
  if (raw_sy > 0) {
    std::swap(top, bottom);
  }

  SbDecodeTargetInfoContentRegion content_region;
  content_region.left = left;
  content_region.right = right;
  content_region.top = top;
  content_region.bottom = bottom;
  return {content_region, coded_size};
}

}  // namespace starboard
