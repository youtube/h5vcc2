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

#ifndef STARBOARD_ANDROID_SHARED_NDK_AUDIO_TRACK_H_
#define STARBOARD_ANDROID_SHARED_NDK_AUDIO_TRACK_H_

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "starboard/android/shared/aaudio_loader.h"
#include "starboard/android/shared/audio_track.h"
#include "starboard/common/pass_key.h"
#include "starboard/common/span.h"
#include "starboard/media.h"

// Suppress availability warnings for API 26+ AAudio symbols.
#pragma clang diagnostic ignored "-Wunguarded-availability"

namespace starboard {

// NdkAudioTrack is an implementation of the AudioTrack interface using the
// Android NDK AAudio API.
//
// Lifetime and Ownership:
// Objects of derived classes are typically owned and managed by the audio sink
// (e.g., via std::unique_ptr).
//
// Threading Model:
// This class is expected to be called on a single audio thread, except for
// SetVolume and SetPlaybackRate which may be called concurrently from the
// player thread.
class NdkAudioTrack : public AudioTrack {
 public:
  static std::unique_ptr<NdkAudioTrack> Create(
      SbMediaAudioCodingType coding_type,
      std::optional<SbMediaAudioSampleType> sample_type,
      int channels,
      int sampling_frequency_hz,
      int preferred_buffer_size_in_bytes,
      std::optional<int> tunnel_mode_audio_session_id,
      bool is_web_audio);

  NdkAudioTrack(PassKey<NdkAudioTrack>,
                AAudioStream* stream,
                int channels,
                int sampling_frequency_hz,
                SbMediaAudioSampleType sample_type);
  ~NdkAudioTrack() override;

  void Play() override;
  void Pause() override;
  void Stop() override;
  void PauseAndFlush() override;

  int WriteSample(Span<const float> samples) override;
  int WriteSample(Span<const uint16_t> samples, int64_t sync_time) override;
  int WriteSample(Span<const uint8_t> buffer, int64_t sync_time) override;

  void SetPlaybackRate(double playback_rate) override;
  void SetVolume(double volume) override;

  int64_t GetAudioTimestamp(int64_t* updated_at) override;
  bool GetAndResetHasAudioDeviceChanged() override;
  int GetUnderrunCount() override;
  int GetStartThresholdInFrames() override;
  PlayState GetPlayState() override;

 private:
  Span<const float> ScaleSamplesIfNeeded(Span<const float> samples);

  struct AAudioStreamDeleter {
    void operator()(AAudioStream* stream) const {
      if (stream) {
        AAudioLoader::GetInstance()->stream_close(stream);
      }
    }
  };

  std::unique_ptr<AAudioStream, AAudioStreamDeleter> stream_;
  const int channels_;
  const SbMediaAudioSampleType sample_type_;
  std::atomic<double> volume_{1.0};
  std::atomic<double> playback_rate_{1.0};
  std::vector<float> scaled_samples_float_;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_NDK_AUDIO_TRACK_H_
