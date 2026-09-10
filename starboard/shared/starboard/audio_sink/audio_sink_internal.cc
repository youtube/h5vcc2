// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/audio_sink/audio_sink_internal.h"

#include <functional>

#include "build/build_config.h"
#include "build/buildflag.h"
#include "starboard/common/command_line.h"
#include "starboard/common/log.h"
#include "starboard/shared/starboard/application.h"
#include "starboard/shared/starboard/audio_sink/stub_audio_sink_type.h"
#include "starboard/shared/starboard/features.h"

namespace starboard {
namespace {

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

bool is_fallback_to_stub_enabled;
SbAudioSinkImpl::Type* primary_audio_sink_type;
SbAudioSinkImpl::Type* fallback_audio_sink_type;

void WrapConsumeFramesFunc(SbAudioSinkConsumeFramesFunc sb_consume_frames_func,
                           int frames_consumed,
                           int64_t /*frames_consumed_at*/,
                           void* context) {
  sb_consume_frames_func(frames_consumed, context);
}

}  // namespace

// static
void SbAudioSinkImpl::Initialize() {
  fallback_audio_sink_type = new StubAudioSinkType;
  PlatformInitialize();
}

// static
void SbAudioSinkImpl::TearDown() {
  PlatformTearDown();
  delete fallback_audio_sink_type;
  fallback_audio_sink_type = NULL;
}

// static
void SbAudioSinkImpl::SetPrimaryType(Type* type) {
  primary_audio_sink_type = type;
}

// static
SbAudioSinkImpl::Type* SbAudioSinkImpl::GetPrimaryType() {
  return primary_audio_sink_type;
}
// static
void SbAudioSinkImpl::EnableFallbackToStub() {
  is_fallback_to_stub_enabled = true;
}
// static
SbAudioSinkImpl::Type* SbAudioSinkImpl::GetFallbackType() {
  if (is_fallback_to_stub_enabled) {
    return fallback_audio_sink_type;
  }
  return NULL;
}

// static
SbAudioSinkImpl::Type* SbAudioSinkImpl::GetPreferredType() {
  bool use_stub_audio_sink = false;
#if BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_STARBOARD)
  use_stub_audio_sink =
      features::FeatureList::IsEnabled(features::kUseStubAudioSink);
#else   // BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_STARBOARD)
  auto command_line = Application::Get()->GetCommandLine();
  use_stub_audio_sink = command_line->HasSwitch("use_stub_audio_sink");
#endif  // BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_STARBOARD)

  SbAudioSinkImpl::Type* audio_sink_type = nullptr;
  if (!use_stub_audio_sink) {
    audio_sink_type = SbAudioSinkImpl::GetPrimaryType();
  }
  if (!audio_sink_type) {
    SB_LOG(WARNING) << "Primary audio sink type not selected or missing, "
                       "opting to use Fallback instead.";
    audio_sink_type = SbAudioSinkImpl::GetFallbackType();
  }
  if (audio_sink_type == nullptr) {
    SB_LOG(WARNING) << "Fallback audio sink type is not enabled.";
  }
  return audio_sink_type;
}

SbAudioSink SbAudioSinkImpl::Create(
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType audio_sample_type,
    SbMediaAudioFrameStorageType audio_frame_storage_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frame_buffers_size_in_frames,
    SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
    ConsumeFramesFunc consume_frames_func,
    ErrorFunc error_func,
    void* context) {
  if (channels <= 0 || channels > SbAudioSinkGetMaxChannels()) {
    SB_LOG(WARNING) << "Invalid audio channels " << channels;
    return kSbAudioSinkInvalid;
  }

  if (sampling_frequency_hz <= 0) {
    SB_LOG(WARNING) << "Invalid audio sampling frequency "
                    << sampling_frequency_hz;
    return kSbAudioSinkInvalid;
  }

  if (!SbAudioSinkIsAudioSampleTypeSupported(audio_sample_type)) {
    SB_LOG(WARNING) << "Invalid audio sample type " << audio_sample_type;
    return kSbAudioSinkInvalid;
  }

  if (!SbAudioSinkIsAudioFrameStorageTypeSupported(audio_frame_storage_type)) {
    SB_LOG(WARNING) << "Invalid audio frame storage type "
                    << audio_frame_storage_type;
    return kSbAudioSinkInvalid;
  }

  if (frame_buffers == NULL) {
    SB_LOG(WARNING) << "Pointer to frame buffers cannot be NULL";
    return kSbAudioSinkInvalid;
  }

  if (frame_buffers_size_in_frames <= 0) {
    SB_LOG(WARNING) << "Invalid frame buffer size "
                    << frame_buffers_size_in_frames;
    return kSbAudioSinkInvalid;
  }

  if (update_source_status_func == NULL) {
    SB_LOG(WARNING) << "update_source_status_func cannot be NULL";
    return kSbAudioSinkInvalid;
  }

  if (!consume_frames_func) {
    SB_LOG(WARNING) << "consume_frames_func cannot be NULL";
    return kSbAudioSinkInvalid;
  }

  if (auto audio_sink_type = GetPreferredType()) {
    auto audio_sink = audio_sink_type->Create(
        channels, sampling_frequency_hz, audio_sample_type,
        audio_frame_storage_type, frame_buffers, frame_buffers_size_in_frames,
        update_source_status_func, consume_frames_func, error_func, context);
    if (audio_sink_type->IsValid(audio_sink)) {
      return audio_sink;
    }
    SB_LOG(ERROR) << "Failed to create SbAudioSink from preferred type.";
    audio_sink_type->Destroy(audio_sink);
  } else {
    SB_LOG(WARNING) << "Preferred Sink Type is invalid.";
  }

  SB_LOG(WARNING) << "Try to create AudioSink using fallback type.";
  if (auto fallback_type = SbAudioSinkImpl::GetFallbackType()) {
    auto audio_sink = fallback_type->Create(
        channels, sampling_frequency_hz, audio_sample_type,
        audio_frame_storage_type, frame_buffers, frame_buffers_size_in_frames,
        update_source_status_func, consume_frames_func, error_func, context);
    if (fallback_type->IsValid(audio_sink)) {
      return audio_sink;
    }
    SB_LOG(ERROR) << "Failed to create SbAudioSink from Fallback type.";
    fallback_type->Destroy(audio_sink);
  } else {
    SB_LOG(WARNING) << "Fallback Sink Type is invalid.";
  }
  return kSbAudioSinkInvalid;
}

// static
SbAudioSink SbAudioSinkImpl::Create(
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType audio_sample_type,
    SbMediaAudioFrameStorageType audio_frame_storage_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frame_buffers_size_in_frames,
    SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
    SbAudioSinkConsumeFramesFunc sb_consume_frames_func,
    ErrorFunc error_func,
    void* context) {
  return Create(channels, sampling_frequency_hz, audio_sample_type,
                audio_frame_storage_type, frame_buffers,
                frame_buffers_size_in_frames, update_source_status_func,
                sb_consume_frames_func
                    ? std::bind(&WrapConsumeFramesFunc, sb_consume_frames_func,
                                _1, _2, _3)
                    : ConsumeFramesFunc(),
                error_func, context);
}

}  // namespace starboard
