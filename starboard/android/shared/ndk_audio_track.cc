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

#include "starboard/android/shared/ndk_audio_track.h"

#include <aaudio/AAudio.h>

#include <memory>
#include <optional>

#include "starboard/android/shared/aaudio_loader.h"
#include "starboard/android/shared/ndk_media_utils.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/span.h"
#include "starboard/common/time.h"

// Suppress availability warnings for API 26+ AAudio symbols.
#pragma clang diagnostic ignored "-Wunguarded-availability"

namespace starboard {
namespace {

constexpr int64_t kStateChangeTimeoutNs = 100'000'000;  // 100 ms

void ScaleSamples(Span<const float> samples,
                  float volume,
                  std::vector<float>* scaled_samples) {
  scaled_samples->resize(samples.size());
  for (size_t i = 0; i < samples.size(); ++i) {
    (*scaled_samples)[i] = samples.data()[i] * volume;
  }
}

}  // namespace

// static
std::unique_ptr<NdkAudioTrack> NdkAudioTrack::Create(
    SbMediaAudioCodingType coding_type,
    std::optional<SbMediaAudioSampleType> sample_type,
    int channels,
    int sampling_frequency_hz,
    int preferred_buffer_size_in_bytes,
    std::optional<int> tunnel_mode_audio_session_id,
    bool is_web_audio) {
  SB_CHECK_EQ(coding_type, kSbMediaAudioCodingTypePcm);
  SB_CHECK_EQ(sample_type, kSbMediaAudioSampleTypeFloat32);

  if (!AAudio::Load()) {
    return nullptr;
  }

  AAudioStreamBuilder* builder = nullptr;
  auto result = AAudio::CreateStreamBuilder(&builder);
  if (result != AAUDIO_OK || !builder) {
    SB_LOG(ERROR) << "AAudio_createStreamBuilder failed: "
                  << AAudio::ConvertResultToText(result);
    return nullptr;
  }

  AAudio::StreamBuilder_SetDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  AAudio::StreamBuilder_SetSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
  AAudio::StreamBuilder_SetSampleRate(builder, sampling_frequency_hz);
  AAudio::StreamBuilder_SetChannelCount(builder, channels);
  AAudio::StreamBuilder_SetFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudio::StreamBuilder_SetUsage(
      builder, is_web_audio ? AAUDIO_USAGE_NOTIFICATION : AAUDIO_USAGE_MEDIA);
  AAudio::StreamBuilder_SetContentType(
      builder,
      is_web_audio ? AAUDIO_CONTENT_TYPE_MUSIC : AAUDIO_CONTENT_TYPE_MOVIE);
  std::optional<int> preferred_frames;
  if (preferred_buffer_size_in_bytes > 0) {
    preferred_frames =
        preferred_buffer_size_in_bytes / (channels * sizeof(float));
    AAudio::StreamBuilder_SetBufferCapacityInFrames(builder, *preferred_frames);
  }

  AAudioStream* stream = nullptr;
  result = AAudio::StreamBuilder_OpenStream(builder, &stream);
  AAudio::StreamBuilder_Delete(builder);

  if (result != AAUDIO_OK || !stream) {
    SB_LOG(ERROR) << "AAudioStreamBuilder_openStream failed: "
                  << AAudio::ConvertResultToText(result);
    return nullptr;
  }

  if (preferred_frames) {
    AAudio::Stream_SetBufferSizeInFrames(stream, *preferred_frames);
  }

  return std::make_unique<NdkAudioTrack>(PassKey<NdkAudioTrack>(), stream,
                                         channels, sampling_frequency_hz,
                                         *sample_type);
}

NdkAudioTrack::NdkAudioTrack(PassKey<NdkAudioTrack>,
                             AAudioStream* stream,
                             int channels,
                             int sampling_frequency_hz,
                             SbMediaAudioSampleType sample_type)
    : stream_(stream), channels_(channels), sample_type_(sample_type) {
  SB_CHECK(stream_);
  SB_LOG(INFO) << "NdkAudioTrack is created: channels=" << channels
               << ", sampling_rate=" << sampling_frequency_hz
               << ", sample_type=" << GetMediaAudioSampleTypeName(sample_type);
}

NdkAudioTrack::~NdkAudioTrack() {
  Stop();
}

void NdkAudioTrack::Play() {
  if (auto result = AAudio::Stream_RequestStart(stream_.get());
      result != AAUDIO_OK) {
    SB_LOG(ERROR) << "stream_requestStart failed: "
                  << AAudio::ConvertResultToText(result);
  }
}

void NdkAudioTrack::Pause() {
  if (!IsStreamActive()) {
    return;
  }
  if (auto result = AAudio::Stream_RequestPause(stream_.get());
      result != AAUDIO_OK) {
    SB_LOG(ERROR) << "stream_requestPause failed: "
                  << AAudio::ConvertResultToText(result);
  }
}

void NdkAudioTrack::Stop() {
  if (!IsStreamActive()) {
    return;
  }
  if (auto result = AAudio::Stream_RequestStop(stream_.get());
      result != AAUDIO_OK) {
    SB_LOG(ERROR) << "stream_requestStop failed: "
                  << AAudio::ConvertResultToText(result);
  }
}

void NdkAudioTrack::PauseAndFlush() {
  if (IsStreamActive()) {
    if (auto result = AAudio::Stream_RequestPause(stream_.get());
        result != AAUDIO_OK) {
      SB_LOG(WARNING) << "stream_requestPause failed: "
                      << AAudio::ConvertResultToText(result);
      return;
    }
  }

  aaudio_stream_state_t current_state = AAudio::Stream_GetState(stream_.get());
  aaudio_stream_state_t next_state = AAUDIO_STREAM_STATE_UNINITIALIZED;
  while (current_state == AAUDIO_STREAM_STATE_STARTED ||
         current_state == AAUDIO_STREAM_STATE_STARTING ||
         current_state == AAUDIO_STREAM_STATE_PAUSING) {
    aaudio_result_t result = AAudio::Stream_WaitForStateChange(
        stream_.get(), current_state, &next_state, kStateChangeTimeoutNs);
    if (current_state == next_state) {
      break;
    }
    current_state = next_state;
    if (result != AAUDIO_OK) {
      SB_LOG(WARNING)
          << "Stream_WaitForStateChange failed, attempting flush anyway: error="
          << AAudio::ConvertResultToText(result)
          << ", state=" << ToString(current_state);
      break;
    }
  }

  if (auto result = AAudio::Stream_RequestFlush(stream_.get());
      result != AAUDIO_OK) {
    SB_LOG(ERROR) << "stream_requestFlush failed: error="
                  << AAudio::ConvertResultToText(result)
                  << ", state=" << ToString(current_state);
  }
}

int NdkAudioTrack::WriteSample(Span<const float> samples) {
  SB_CHECK_EQ(sample_type_, kSbMediaAudioSampleTypeFloat32);
  SB_CHECK_EQ(samples.size() % channels_, 0u);

  Span<const float> samples_to_write = samples;
  float volume = volume_.load(std::memory_order_relaxed);
  if (volume != 1.0f) {
    ScaleSamples(samples, volume, &scaled_samples_float_);
    samples_to_write =
        MakeSpan(scaled_samples_float_.data(), scaled_samples_float_.size());
  }

  int num_frames = samples_to_write.size() / channels_;
  aaudio_result_t result =
      AAudio::Stream_Write(stream_.get(), samples_to_write.data(), num_frames,
                           /*timeoutNanoseconds=*/0);
  if (result == AAUDIO_ERROR_DISCONNECTED) {
    return kAudioTrackErrorDeadObject;
  }
  if (result < 0) {
    SB_LOG(ERROR) << "stream_write (float) failed: "
                  << AAudio::ConvertResultToText(result);
    return result;
  }
  return result * channels_;
}

int NdkAudioTrack::WriteSample(Span<const uint16_t> samples,
                               int64_t sync_time) {
  SB_NOTREACHED();
  return 0;
}

int NdkAudioTrack::WriteSample(Span<const uint8_t> buffer, int64_t sync_time) {
  SB_NOTREACHED();
  return 0;
}

void NdkAudioTrack::SetPlaybackRate(double playback_rate) {
  if (playback_rate != 1.0) {
    SB_LOG(WARNING) << "AAudio NDK stream does not support setting playback "
                       "rate: playback_rate="
                    << playback_rate;
  }
}

void NdkAudioTrack::SetVolume(double volume) {
  volume_.store(static_cast<float>(volume), std::memory_order_relaxed);
}

int64_t NdkAudioTrack::GetAudioTimestamp(int64_t* updated_at) {
  int64_t frame_position = 0;
  int64_t time_nanoseconds = 0;
  aaudio_result_t result = AAudio::Stream_GetTimestamp(
      stream_.get(), CLOCK_MONOTONIC, &frame_position, &time_nanoseconds);
  if (result == AAUDIO_OK) {
    if (updated_at) {
      *updated_at = time_nanoseconds / 1000;
    }
    return frame_position;
  }

  if (updated_at) {
    *updated_at = CurrentMonotonicTime();
  }
  int64_t frames_read = AAudio::Stream_GetFramesRead(stream_.get());
  if (frames_read < 0) {
    SB_LOG(WARNING) << "stream_getFramesRead failed: "
                    << AAudio::ConvertResultToText(
                           static_cast<aaudio_result_t>(frames_read));
    return 0;
  }
  return frames_read;
}

// TODO: b/523148108 - Implement seamless switch for NDK.
AudioTrack::AudioDeviceChange NdkAudioTrack::GetAndResetAudioDeviceChange() {
  if (has_reported_device_changed_) {
    return AudioDeviceChange::kNone;
  }
  aaudio_stream_state_t state = AAudio::Stream_GetState(stream_.get());
  if (state == AAUDIO_STREAM_STATE_DISCONNECTED) {
    has_reported_device_changed_ = true;
    return AudioDeviceChange::kRestartPlayer;
  }
  return AudioDeviceChange::kNone;
}

int NdkAudioTrack::GetUnderrunCount() {
  return AAudio::Stream_GetXRunCount(stream_.get());
}

int NdkAudioTrack::GetStartThresholdInFrames() {
  return AAudio::Stream_GetFramesPerBurst(stream_.get());
}

AudioTrack::PlayState NdkAudioTrack::GetPlayState() {
  aaudio_stream_state_t state = AAudio::Stream_GetState(stream_.get());
  switch (state) {
    case AAUDIO_STREAM_STATE_STARTING:
    case AAUDIO_STREAM_STATE_STARTED:
      return AudioTrack::PlayState::kPlaying;
    case AAUDIO_STREAM_STATE_PAUSING:
    case AAUDIO_STREAM_STATE_PAUSED:
    case AAUDIO_STREAM_STATE_FLUSHING:
    case AAUDIO_STREAM_STATE_FLUSHED:
      return AudioTrack::PlayState::kPaused;
    default:
      return AudioTrack::PlayState::kStopped;
  }
}

bool NdkAudioTrack::IsStreamActive() const {
  aaudio_stream_state_t state = AAudio::Stream_GetState(stream_.get());
  return state == AAUDIO_STREAM_STATE_STARTING ||
         state == AAUDIO_STREAM_STATE_STARTED;
}

}  // namespace starboard
