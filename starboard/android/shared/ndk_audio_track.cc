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
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <optional>

#include "starboard/android/shared/aaudio_loader.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/span.h"
#include "starboard/common/time.h"

// Suppress availability warnings for API 26+ AAudio symbols.
#pragma clang diagnostic ignored "-Wunguarded-availability"

namespace starboard {
namespace {

constexpr int64_t kWriteTimeoutNanoseconds = 1'000'000'000;  // 1 second

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
  if (coding_type != kSbMediaAudioCodingTypePcm || !sample_type ||
      sample_type != kSbMediaAudioSampleTypeFloat32) {
    return nullptr;
  }

  AAudioLoader* loader = AAudioLoader::GetInstance();
  if (!loader) {
    return nullptr;
  }

  AAudioStreamBuilder* builder = nullptr;
  aaudio_result_t result = loader->createStreamBuilder(&builder);
  if (result != AAUDIO_OK || !builder) {
    return nullptr;
  }

  loader->streamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  loader->streamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
  loader->streamBuilder_setSampleRate(builder, sampling_frequency_hz);
  loader->streamBuilder_setChannelCount(builder, channels);
  loader->streamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);

  if (preferred_buffer_size_in_bytes > 0) {
    int frames = preferred_buffer_size_in_bytes / (channels * sizeof(float));
    loader->streamBuilder_setBufferCapacityInFrames(builder, frames);
  }

  AAudioStream* stream = nullptr;
  result = loader->streamBuilder_openStream(builder, &stream);
  loader->streamBuilder_delete(builder);

  if (result != AAUDIO_OK || !stream) {
    SB_LOG(ERROR) << "AAudioStreamBuilder_openStream failed: "
                  << loader->convertResultToText(result);
    return nullptr;
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
  if (stream_) {
    AAudioLoader* loader = AAudioLoader::GetInstance();
    aaudio_result_t result = loader->stream_requestStart(stream_.get());
    if (result != AAUDIO_OK) {
      SB_LOG(ERROR) << "stream_requestStart failed: "
                    << loader->convertResultToText(result);
    }
  }
}

void NdkAudioTrack::Pause() {
  if (stream_) {
    AAudioLoader* loader = AAudioLoader::GetInstance();
    aaudio_result_t result = loader->stream_requestPause(stream_.get());
    if (result != AAUDIO_OK) {
      SB_LOG(ERROR) << "stream_requestPause failed: "
                    << loader->convertResultToText(result);
    }
  }
}

void NdkAudioTrack::Stop() {
  if (stream_) {
    AAudioLoader* loader = AAudioLoader::GetInstance();
    aaudio_result_t result = loader->stream_requestStop(stream_.get());
    if (result != AAUDIO_OK) {
      SB_LOG(ERROR) << "stream_requestStop failed: "
                    << loader->convertResultToText(result);
    }
  }
}

void NdkAudioTrack::PauseAndFlush() {
  if (stream_) {
    AAudioLoader* loader = AAudioLoader::GetInstance();
    aaudio_result_t result = loader->stream_requestPause(stream_.get());
    if (result != AAUDIO_OK) {
      SB_LOG(ERROR) << "stream_requestPause failed: "
                    << loader->convertResultToText(result);
    }
    aaudio_stream_state_t state = loader->stream_getState(stream_.get());
    int attempts = 0;
    while (state != AAUDIO_STREAM_STATE_PAUSED &&
           state != AAUDIO_STREAM_STATE_STOPPED &&
           state != AAUDIO_STREAM_STATE_FLUSHED && attempts < 100) {
      usleep(1000);
      state = loader->stream_getState(stream_.get());
      attempts++;
    }
    result = loader->stream_requestFlush(stream_.get());
    if (result != AAUDIO_OK) {
      SB_LOG(ERROR) << "stream_requestFlush failed: "
                    << loader->convertResultToText(result);
    }
  }
}

int NdkAudioTrack::WriteSample(Span<const float> samples) {
  if (!stream_) {
    return kAudioTrackErrorDeadObject;
  }
  SB_CHECK_EQ(sample_type_, kSbMediaAudioSampleTypeFloat32);
  SB_CHECK_EQ(samples.size() % channels_, 0u);

  Span<const float> samples_to_write = ScaleSamplesIfNeeded(samples);

  int num_frames = samples.size() / channels_;
  aaudio_result_t result = AAudioLoader::GetInstance()->stream_write(
      stream_.get(), samples_to_write.data(), num_frames,
      kWriteTimeoutNanoseconds);
  if (result == AAUDIO_ERROR_DISCONNECTED) {
    return kAudioTrackErrorDeadObject;
  }
  if (result < 0) {
    SB_LOG(ERROR) << "stream_write (float) failed: "
                  << AAudioLoader::GetInstance()->convertResultToText(result);
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
  playback_rate_.store(playback_rate, std::memory_order_relaxed);
}

void NdkAudioTrack::SetVolume(double volume) {
  volume_.store(volume, std::memory_order_relaxed);
}

Span<const float> NdkAudioTrack::ScaleSamplesIfNeeded(
    Span<const float> samples) {
  double volume = volume_.load(std::memory_order_relaxed);
  if (volume == 1.0) {
    return samples;
  }
  scaled_samples_float_.resize(samples.size());
  for (size_t i = 0; i < samples.size(); ++i) {
    scaled_samples_float_[i] = samples.data()[i] * volume;
  }
  return MakeSpan(scaled_samples_float_.data(), scaled_samples_float_.size());
}

int64_t NdkAudioTrack::GetAudioTimestamp(int64_t* updated_at) {
  if (!stream_) {
    if (updated_at) {
      *updated_at = CurrentMonotonicTime();
    }
    return 0;
  }

  int64_t frame_position = 0;
  int64_t time_nanoseconds = 0;
  aaudio_result_t result = AAudioLoader::GetInstance()->stream_getTimestamp(
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
  return AAudioLoader::GetInstance()->stream_getFramesRead(stream_.get());
}

bool NdkAudioTrack::GetAndResetHasAudioDeviceChanged() {
  if (!stream_) {
    return true;
  }
  aaudio_stream_state_t state =
      AAudioLoader::GetInstance()->stream_getState(stream_.get());
  return state == AAUDIO_STREAM_STATE_DISCONNECTED;
}

int NdkAudioTrack::GetUnderrunCount() {
  if (!stream_) {
    return 0;
  }
  return AAudioLoader::GetInstance()->stream_getXRunCount(stream_.get());
}

int NdkAudioTrack::GetStartThresholdInFrames() {
  if (!stream_) {
    return 0;
  }
  return AAudioLoader::GetInstance()->stream_getFramesPerBurst(stream_.get());
}

AudioTrack::PlayState NdkAudioTrack::GetPlayState() {
  if (!stream_) {
    return AudioTrack::PlayState::kStopped;
  }
  aaudio_stream_state_t state =
      AAudioLoader::GetInstance()->stream_getState(stream_.get());
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

}  // namespace starboard
