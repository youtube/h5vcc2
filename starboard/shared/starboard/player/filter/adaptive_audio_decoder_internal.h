// Copyright 2019 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_SHARED_STARBOARD_PLAYER_FILTER_ADAPTIVE_AUDIO_DECODER_INTERNAL_H_
#define STARBOARD_SHARED_STARBOARD_PLAYER_FILTER_ADAPTIVE_AUDIO_DECODER_INTERNAL_H_

#include <memory>
#include <optional>
#include <queue>

#include "starboard/media.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/filter/audio_channel_layout_mixer.h"
#include "starboard/shared/starboard/player/filter/audio_decoder_internal.h"
#include "starboard/shared/starboard/player/filter/audio_resampler.h"
#include "starboard/shared/starboard/player/job_queue.h"

namespace starboard {

class AdaptiveAudioDecoder : public AudioDecoder, private JobQueue::JobOwner {
 public:
  typedef std::function<std::unique_ptr<AudioDecoder>(
      const AudioStreamInfo& audio_stream_info,
      SbDrmSystem drm_system)>
      AudioDecoderCreator;

  typedef std::function<void(SbMediaAudioSampleType* output_sample_type,
                             int* output_samples_per_second,
                             int* output_number_of_channels)>
      OutputFormatAdjustmentCallback;

  AdaptiveAudioDecoder(JobQueue* job_queue,
                       const AudioStreamInfo& audio_stream_info,
                       SbDrmSystem drm_system,
                       const AudioDecoderCreator& audio_decoder_creator,
                       const OutputFormatAdjustmentCallback&
                           output_adjustment_callback = nullptr);
  AdaptiveAudioDecoder(JobQueue* job_queue,
                       const AudioStreamInfo& audio_stream_info,
                       SbDrmSystem drm_system,
                       const AudioDecoderCreator& audio_decoder_creator,
                       bool enable_reset_audio_decoder,
                       const OutputFormatAdjustmentCallback&
                           output_adjustment_callback = nullptr);
  ~AdaptiveAudioDecoder() override;

  void Initialize(const OutputCB& output_cb, const ErrorCB& error_cb) override;
  void Decode(const InputBuffers& input_buffers,
              const ConsumedCB& consumed_cb) override;
  void WriteEndOfStream() override;
  std::optional<DecodedAudio> Read(int* samples_per_second) override;
  void Reset() override;

 private:
  void InitializeAudioDecoder(const AudioStreamInfo& audio_stream_info);
  void TeardownAudioDecoder();
  void OnDecoderOutput();
  void ResetInternal();

  const uint32_t initial_samples_per_second_;
  const SbDrmSystem drm_system_;
  const AudioDecoderCreator audio_decoder_creator_;
  const OutputFormatAdjustmentCallback output_adjustment_callback_;
  SbMediaAudioSampleType output_sample_type_;
  int output_samples_per_second_;
  int output_number_of_channels_;
  AudioStreamInfo input_audio_stream_info_;

  OutputCB output_cb_ = nullptr;
  ErrorCB error_cb_ = nullptr;

  std::unique_ptr<AudioDecoder> audio_decoder_;
  std::unique_ptr<AudioResampler> resampler_;
  std::unique_ptr<AudioChannelLayoutMixer> channel_mixer_;
  InputBuffers pending_input_buffers_;
  ConsumedCB pending_consumed_cb_;
  std::queue<DecodedAudio> decoded_audios_;
  bool flushing_ = false;
  bool stream_ended_ = false;
  bool first_output_received_ = false;
  bool output_format_checked_ = false;
  bool first_input_written_ = false;
  bool enable_reset_audio_decoder_ = false;
};

}  // namespace starboard

#endif  // STARBOARD_SHARED_STARBOARD_PLAYER_FILTER_ADAPTIVE_AUDIO_DECODER_INTERNAL_H_
