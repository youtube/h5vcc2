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

#include "starboard/android/shared/exoplayer/exoplayer_util.h"

#include <jni.h>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/mime_type.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "cobalt/android/jni_headers/ExoPlayerManager_jni.h"
#pragma GCC diagnostic pop

namespace starboard {
namespace {
using base::android::ConvertUTF8ToJavaString;
using base::android::ToJavaByteArray;
using jni_zero::AttachCurrentThread;
using jni_zero::ScopedJavaLocalRef;

ScopedJavaLocalRef<jobject> CreateExoPlayerColorInfo(
    const SbMediaColorMetadata& metadata) {
  if (IsSDR(metadata)) {
    return ScopedJavaLocalRef<jobject>();
  }

  jint color_standard = SbMediaPrimaryIdToColorStandard(metadata.primaries);
  jint color_transfer = SbMediaTransferIdToColorTransfer(metadata.transfer);
  jint color_range = SbMediaRangeIdToColorRange(metadata.range);

  if (color_standard == COLOR_VALUE_UNKNOWN ||
      color_transfer == COLOR_VALUE_UNKNOWN ||
      color_range == COLOR_VALUE_UNKNOWN) {
    return ScopedJavaLocalRef<jobject>();
  }

  const auto& mastering_metadata = metadata.mastering_metadata;
  return Java_ExoPlayerManager_createExoPlayerColorInfo(
      AttachCurrentThread(), color_range, color_standard, color_transfer,
      mastering_metadata.primary_r_chromaticity_x,
      mastering_metadata.primary_r_chromaticity_y,
      mastering_metadata.primary_g_chromaticity_x,
      mastering_metadata.primary_g_chromaticity_y,
      mastering_metadata.primary_b_chromaticity_x,
      mastering_metadata.primary_b_chromaticity_y,
      mastering_metadata.white_point_chromaticity_x,
      mastering_metadata.white_point_chromaticity_y,
      mastering_metadata.luminance_max, mastering_metadata.luminance_min,
      metadata.max_cll, metadata.max_fall);
}

}  // namespace

bool ShouldEnableTunneledPlayback(const SbMediaVideoStreamInfo& stream_info) {
  auto mime_type = MimeType::Create(stream_info.mime);
  if (stream_info.codec == kSbMediaVideoCodecNone || !mime_type ||
      !mime_type->ValidateBoolParameter("tunnelmode")) {
    return false;
  }

  return mime_type->GetParamBoolValue("tunnelmode", false);
}

ScopedJavaLocalRef<jobject> CreateAudioFormat(
    const SbMediaAudioStreamInfo& stream_info) {
  if (stream_info.codec == kSbMediaAudioCodecNone) {
    SB_LOG(ERROR)
        << "Cannot create an audio MediaSource for kSbMediaAudioCodecNone";
    return ScopedJavaLocalRef<jobject>();
  }

  if (!MimeType::Create(stream_info.mime)) {
    SB_LOG(ERROR) << "Invalid audio MIME: '" << stream_info.mime << "'";
    return ScopedJavaLocalRef<jobject>();
  }

  int sample_rate = stream_info.samples_per_second;
  int channels = stream_info.number_of_channels;

  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jbyteArray> configuration_data;
  if (stream_info.audio_specific_config_size > 0) {
    configuration_data = ToJavaByteArray(
        env,
        reinterpret_cast<const uint8_t*>(stream_info.audio_specific_config),
        stream_info.audio_specific_config_size);
  }

  ScopedJavaLocalRef<jstring> j_audio_mime;

  if (stream_info.codec == kSbMediaAudioCodecAc3) {
    j_audio_mime = ConvertUTF8ToJavaString(env, "audio/ac3");
  } else if (stream_info.codec == kSbMediaAudioCodecEac3) {
    j_audio_mime = ConvertUTF8ToJavaString(env, "audio/eac3");
  } else {
    bool is_passthrough_unused;
    j_audio_mime = ConvertUTF8ToJavaString(
        env, SupportedAudioCodecToMimeType(stream_info.codec,
                                           &is_passthrough_unused));
  }

  return Java_ExoPlayerManager_createAudioFormat(
      env, j_audio_mime, configuration_data, sample_rate, channels);
}

ScopedJavaLocalRef<jobject> CreateVideoFormat(
    const SbMediaVideoStreamInfo& stream_info) {
  if (stream_info.codec == kSbMediaVideoCodecNone) {
    SB_LOG(ERROR)
        << "Cannot create a video MediaSource for kSbMediaVideoCodecNone";
    return ScopedJavaLocalRef<jobject>();
  }

  auto mime_type = MimeType::Create(stream_info.mime);
  if (!mime_type) {
    SB_LOG(ERROR) << "Invalid video MIME: '" << stream_info.mime << "'";
    return ScopedJavaLocalRef<jobject>();
  }

  int width = stream_info.frame_width;
  int height = stream_info.frame_height;

  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jstring> j_mime(ConvertUTF8ToJavaString(
      env, SupportedVideoCodecToMimeType(stream_info.codec)));

  int framerate = mime_type->GetParamIntValue("framerate", 0);
  int bitrate = mime_type->GetParamIntValue("bitrate", 0);

  ScopedJavaLocalRef<jobject> j_hdr_color_info =
      CreateExoPlayerColorInfo(stream_info.color_metadata);

  return Java_ExoPlayerManager_createVideoFormat(
      env, j_mime, width, height, framerate, bitrate, j_hdr_color_info);
}

}  // namespace starboard
