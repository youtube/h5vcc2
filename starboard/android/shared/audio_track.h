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

#ifndef STARBOARD_ANDROID_SHARED_AUDIO_TRACK_H_
#define STARBOARD_ANDROID_SHARED_AUDIO_TRACK_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "starboard/common/span.h"
#include "starboard/media.h"

namespace starboard {

// AudioTrack is an abstract interface for Android audio playback functionality,
// providing a unified API for both JNI-based (AudioTrackBridge) and NDK-based
// (NdkAudioTrack) implementations.
//
// Lifetime and Ownership:
// Objects of derived classes are typically owned and managed by the audio sink
// (e.g., via std::unique_ptr).
//
// Threading Model:
// Implementations of this interface are not guaranteed to be thread-safe and
// are expected to be called on a single audio thread.
class AudioTrack {
 public:
  // These must be in sync with AudioTrack.PLAYSTATE_XXX constants in
  // AudioTrack.java.
  enum class PlayState {
    kStopped = 1,
    kPaused = 2,
    kPlaying = 3,
  };

  // GENERATED_JAVA_ENUM_PACKAGE: dev.cobalt.media
  enum class AudioDeviceChange {
    kNone,
    kResetAndContinue,  // Seamless: flush & re-feed without destroying player
    kRestartPlayer,     // Capability changed: tear down player & recreate
  };

  // The maximum number of frames that can be written to android audio track per
  // write request.
  static constexpr int kMaxFramesPerRequest = 65'536;
  // Error code returned as negative value from WriteSample().
  // The same as Android AudioTrack.ERROR_DEAD_OBJECT.
  static constexpr int kAudioTrackErrorDeadObject = -6;

  static void SetNdkAudioTrackEnabled(bool enabled);

  static std::unique_ptr<AudioTrack> Create(
      SbMediaAudioCodingType coding_type,
      std::optional<SbMediaAudioSampleType> sample_type,
      int channels,
      int sampling_frequency_hz,
      int preferred_buffer_size_in_bytes,
      std::optional<int> tunnel_mode_audio_session_id,
      bool is_web_audio);

  virtual ~AudioTrack() = default;

  virtual void Play() = 0;
  virtual void Pause() = 0;
  virtual void Stop() = 0;
  virtual void PauseAndFlush() = 0;

  // Returns zero or the positive number of samples written, or a negative error
  // code.
  virtual int WriteSample(Span<const float> samples) = 0;
  // This method is called when tunnel mode is used.
  // Returns zero or the positive number of samples written, or a negative error
  // code.
  virtual int WriteSample(Span<const uint16_t> samples, int64_t sync_time) = 0;
  // This is used by passthrough, treating buffer as compressed bitstream bytes.
  // Returns zero or the positive number of bytes written, or a negative error
  // code.
  virtual int WriteSample(Span<const uint8_t> buffer, int64_t sync_time) = 0;

  virtual void SetPlaybackRate(double playback_rate) = 0;
  virtual void SetVolume(double volume) = 0;

  // |updated_at| contains the timestamp when the audio timestamp is updated on
  // return.  It can be nullptr.
  virtual int64_t GetAudioTimestamp(int64_t* updated_at) = 0;
  virtual AudioDeviceChange GetAndResetAudioDeviceChange() = 0;
  virtual int GetUnderrunCount() = 0;
  virtual int GetStartThresholdInFrames() = 0;
  virtual PlayState GetPlayState() = 0;
};

using AudioDeviceChange = AudioTrack::AudioDeviceChange;

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_AUDIO_TRACK_H_
