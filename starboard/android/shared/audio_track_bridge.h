// Copyright 2021 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_ANDROID_SHARED_AUDIO_TRACK_BRIDGE_H_
#define STARBOARD_ANDROID_SHARED_AUDIO_TRACK_BRIDGE_H_

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

#include "starboard/android/shared/audio_track.h"
#include "starboard/common/pass_key.h"
#include "starboard/media.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {

// The C++ encapsulation of the Java class AudioTrackBridge.
class AudioTrackBridge : public AudioTrack {
 public:
  using FloatArray = jni_zero::ScopedJavaGlobalRef<jfloatArray>;
  using ByteArray = jni_zero::ScopedJavaGlobalRef<jbyteArray>;
  using DataArray = std::variant<FloatArray, ByteArray>;

  static std::unique_ptr<AudioTrackBridge> Create(
      SbMediaAudioCodingType coding_type,
      std::optional<SbMediaAudioSampleType> sample_type,
      int channels,
      int sampling_frequency_hz,
      int preferred_buffer_size_in_bytes,
      std::optional<int> tunnel_mode_audio_session_id,
      bool is_web_audio);

  AudioTrackBridge(
      PassKey<AudioTrackBridge>,
      int max_samples_per_write,
      const jni_zero::ScopedJavaLocalRef<jobject>& j_audio_track_bridge,
      const DataArray& j_audio_data);
  ~AudioTrackBridge() override;

  void Play() override;
  void Pause() override;
  void Stop() override;
  void PauseAndFlush() override;

  // Returns zero or the positive number of samples written, or a negative error
  // code.
  int WriteSample(Span<const float> samples) override;
  int WriteSample(Span<const uint16_t> samples, int64_t sync_time) override;
  // This is used by passthrough, it treats samples as if they are in bytes.
  // Returns zero or the positive number of samples written, or a negative error
  // code.
  int WriteSample(Span<const uint8_t> buffer, int64_t sync_time) override;

  void SetPlaybackRate(double playback_rate) override;
  void SetVolume(double volume) override;

  // |updated_at| contains the timestamp when the audio timestamp is updated on
  // return.  It can be nullptr.
  int64_t GetAudioTimestamp(int64_t* updated_at) override;
  AudioDeviceChange GetAndResetAudioDeviceChange() override;
  int GetUnderrunCount() override;
  int GetStartThresholdInFrames() override;
  PlayState GetPlayState() override;

 private:
  const int max_samples_per_write_;

  const jni_zero::ScopedJavaGlobalRef<jobject> j_audio_track_bridge_;
  // The audio data has to be copied into a Java Array before writing into the
  // audio track. Allocating a large array and saves as a member variable
  // avoids an array being allocated repeatedly.
  const DataArray j_audio_data_;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_AUDIO_TRACK_BRIDGE_H_
