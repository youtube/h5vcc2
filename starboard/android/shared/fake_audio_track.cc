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

#include "starboard/android/shared/fake_audio_track.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#include "starboard/common/check_op.h"
#include "starboard/common/time.h"

namespace starboard {

FakeAudioTrack::FakeAudioTrack(int channels,
                               int sampling_frequency_hz,
                               SbMediaAudioSampleType sample_type)
    : channels_(channels), sample_type_(sample_type) {
  SB_CHECK_GT(channels_, 0);
}

void FakeAudioTrack::Play() {
  std::lock_guard lock(mutex_);
  play_state_ = PlayState::kPlaying;
}

void FakeAudioTrack::Pause() {
  std::lock_guard lock(mutex_);
  play_state_ = PlayState::kPaused;
}

void FakeAudioTrack::Stop() {
  std::lock_guard lock(mutex_);
  play_state_ = PlayState::kStopped;
}

void FakeAudioTrack::PauseAndFlush() {
  std::lock_guard lock(mutex_);
  play_state_ = PlayState::kPaused;
  written_frames_ = 0;
  consumed_frames_ = 0;
  ++pause_and_flush_count_;
}

int FakeAudioTrack::WriteSample(Span<const float> samples) {
  std::lock_guard lock(mutex_);
  if (write_error_code_ != 0) {
    return write_error_code_;
  }
  SB_CHECK_EQ(sample_type_, kSbMediaAudioSampleTypeFloat32);
  SB_CHECK_EQ(samples.size() % channels_, 0u);

  written_frames_ += samples.size() / channels_;
  return static_cast<int>(samples.size());
}

int FakeAudioTrack::WriteSample(Span<const uint16_t> samples,
                                int64_t sync_time) {
  std::lock_guard lock(mutex_);
  if (write_error_code_ != 0) {
    return write_error_code_;
  }
  SB_CHECK_EQ(sample_type_, kSbMediaAudioSampleTypeInt16Deprecated);
  SB_CHECK_EQ(samples.size() % channels_, 0u);

  written_frames_ += samples.size() / channels_;
  return static_cast<int>(samples.size());
}

int FakeAudioTrack::WriteSample(Span<const uint8_t> buffer, int64_t sync_time) {
  std::lock_guard lock(mutex_);
  if (write_error_code_ != 0) {
    return write_error_code_;
  }
  written_frames_ += buffer.size() / channels_;
  return static_cast<int>(buffer.size());
}

void FakeAudioTrack::SetPlaybackRate(double playback_rate) {
  std::lock_guard lock(mutex_);
  playback_rate_ = playback_rate;
}

void FakeAudioTrack::SetVolume(double volume) {
  std::lock_guard lock(mutex_);
  volume_ = volume;
}

int64_t FakeAudioTrack::GetAudioTimestamp(int64_t* updated_at) {
  std::lock_guard lock(mutex_);
  if (updated_at) {
    *updated_at = CurrentMonotonicTime();
  }
  return consumed_frames_;
}

AudioTrack::AudioDeviceChange FakeAudioTrack::GetAndResetAudioDeviceChange() {
  std::lock_guard lock(mutex_);
  AudioDeviceChange change = device_change_;
  device_change_ = AudioDeviceChange::kNone;
  return change;
}

int FakeAudioTrack::GetUnderrunCount() {
  std::lock_guard lock(mutex_);
  return underrun_count_;
}

int FakeAudioTrack::GetStartThresholdInFrames() {
  std::lock_guard lock(mutex_);
  return start_threshold_;
}

AudioTrack::PlayState FakeAudioTrack::GetPlayState() {
  std::lock_guard lock(mutex_);
  return play_state_;
}

int64_t FakeAudioTrack::written_frames() const {
  std::lock_guard lock(mutex_);
  return written_frames_;
}

int64_t FakeAudioTrack::consumed_frames() const {
  std::lock_guard lock(mutex_);
  return consumed_frames_;
}

double FakeAudioTrack::playback_rate() const {
  std::lock_guard lock(mutex_);
  return playback_rate_;
}

double FakeAudioTrack::volume() const {
  std::lock_guard lock(mutex_);
  return volume_;
}

AudioTrack::PlayState FakeAudioTrack::play_state() const {
  std::lock_guard lock(mutex_);
  return play_state_;
}

int FakeAudioTrack::pause_and_flush_count() const {
  std::lock_guard lock(mutex_);
  return pause_and_flush_count_;
}

void FakeAudioTrack::set_consumed_frames(int64_t consumed) {
  std::lock_guard lock(mutex_);
  consumed_frames_ = consumed;
}

void FakeAudioTrack::simulate_device_change(AudioDeviceChange change) {
  std::lock_guard lock(mutex_);
  device_change_ = change;
}

void FakeAudioTrack::simulate_device_change(bool changed) {
  simulate_device_change(changed ? AudioDeviceChange::kRestartPlayer
                                 : AudioDeviceChange::kNone);
}

void FakeAudioTrack::set_underrun_count(int count) {
  std::lock_guard lock(mutex_);
  underrun_count_ = count;
}

void FakeAudioTrack::set_write_error(int error_code) {
  std::lock_guard lock(mutex_);
  write_error_code_ = error_code;
}

}  // namespace starboard
