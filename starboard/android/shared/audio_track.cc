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

#include "starboard/android/shared/audio_track.h"

#include <android/api-level.h>

#include <memory>
#include <optional>

#include "starboard/android/shared/aaudio_loader.h"
#include "starboard/android/shared/audio_track_bridge.h"
#include "starboard/android/shared/ndk_audio_track.h"
#include "starboard/common/log.h"
#include "starboard/shared/starboard/features.h"

namespace starboard {
namespace {

bool CanUseNdkAudioTrack(SbMediaAudioCodingType coding_type,
                         std::optional<SbMediaAudioSampleType> sample_type,
                         std::optional<int> tunnel_mode_audio_session_id) {
  if (!features::FeatureList::IsEnabled(features::kNdkAudio)) {
    return false;
  }

  if (coding_type != kSbMediaAudioCodingTypePcm ||
      sample_type != kSbMediaAudioSampleTypeFloat32) {
    return false;
  }

  if (!AAudioLoader::GetInstance()) {
    return false;
  }

  // NDK audio track does not support tunnel mode.
  if (tunnel_mode_audio_session_id) {
    return false;
  }
  // NDK AAudio requires API level >= 28.
  if (android_get_device_api_level() < 28) {
    return false;
  }

  return true;
}

}  // namespace

// static
std::unique_ptr<AudioTrack> AudioTrack::Create(
    SbMediaAudioCodingType coding_type,
    std::optional<SbMediaAudioSampleType> sample_type,
    int channels,
    int sampling_frequency_hz,
    int preferred_buffer_size_in_bytes,
    std::optional<int> tunnel_mode_audio_session_id,
    bool is_web_audio) {
  if (CanUseNdkAudioTrack(coding_type, sample_type,
                          tunnel_mode_audio_session_id)) {
    auto ndk_audio_track = NdkAudioTrack::Create(
        coding_type, sample_type, channels, sampling_frequency_hz,
        preferred_buffer_size_in_bytes, tunnel_mode_audio_session_id,
        is_web_audio);
    if (ndk_audio_track) {
      return ndk_audio_track;
    }
    SB_LOG(WARNING)
        << "Failed to create NdkAudioTrack. Falling back to AudioTrackBridge.";
  }

  return AudioTrackBridge::Create(coding_type, sample_type, channels,
                                  sampling_frequency_hz,
                                  preferred_buffer_size_in_bytes,
                                  tunnel_mode_audio_session_id, is_web_audio);
}

}  // namespace starboard
