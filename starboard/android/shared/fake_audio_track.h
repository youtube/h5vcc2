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

#ifndef STARBOARD_ANDROID_SHARED_FAKE_AUDIO_TRACK_H_
#define STARBOARD_ANDROID_SHARED_FAKE_AUDIO_TRACK_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "starboard/android/shared/audio_track.h"

namespace starboard {

// FakeAudioTrack is a thread-safe simulation of Android's AudioTrack,
// used to verify audio sink logic in unit tests.
//
// This class is thread-safe.
class FakeAudioTrack : public AudioTrack {
 public:
  explicit FakeAudioTrack(int channels,
                          int sampling_frequency_hz,
                          SbMediaAudioSampleType sample_type);
  ~FakeAudioTrack() override = default;

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
  AudioDeviceChange GetAndResetAudioDeviceChange() override;
  int GetUnderrunCount() override;
  int GetStartThresholdInFrames() override;
  PlayState GetPlayState() override;

  // Test control & inspection methods
  int64_t written_frames() const;
  int64_t consumed_frames() const;
  double playback_rate() const;
  double volume() const;
  PlayState play_state() const;
  int pause_and_flush_count() const;
  void set_consumed_frames(int64_t consumed);
  void simulate_device_change(AudioDeviceChange change);
  void simulate_device_change(bool changed);
  void set_underrun_count(int count);
  void set_write_error(int error_code);

 private:
  const int channels_;
  const SbMediaAudioSampleType sample_type_;

  mutable std::mutex mutex_;
  PlayState play_state_ = PlayState::kStopped;
  int pause_and_flush_count_ = 0;
  int64_t written_frames_ = 0;
  int64_t consumed_frames_ = 0;
  double playback_rate_ = 1.0;
  double volume_ = 1.0;
  AudioDeviceChange device_change_ = AudioDeviceChange::kNone;
  int underrun_count_ = 0;
  int start_threshold_ = 1024;
  int write_error_code_ = 0;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_FAKE_AUDIO_TRACK_H_
