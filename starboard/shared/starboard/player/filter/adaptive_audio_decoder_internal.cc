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

#include "starboard/shared/starboard/player/filter/adaptive_audio_decoder_internal.h"

#include <utility>

#include "build/build_config.h"
#include "starboard/audio_sink.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/decoded_audio_internal.h"

namespace starboard {

template <typename T>
T ResetAndReturn(T* t) {
  T result(std::move(*t));
  *t = T();
  return result;
}

AdaptiveAudioDecoder::AdaptiveAudioDecoder(
    JobQueue* job_queue,
    const AudioStreamInfo& audio_stream_info,
    SbDrmSystem drm_system,
    const AudioDecoderCreator& audio_decoder_creator,
    const OutputFormatAdjustmentCallback& output_adjustment_callback)
    : JobOwner(job_queue),
      initial_samples_per_second_(audio_stream_info.samples_per_second),
      drm_system_(drm_system),
      audio_decoder_creator_(audio_decoder_creator),
      output_adjustment_callback_(output_adjustment_callback),
      output_number_of_channels_(audio_stream_info.number_of_channels) {
  SB_DCHECK_NE(audio_stream_info.codec, kSbMediaAudioCodecNone);
}

AdaptiveAudioDecoder::AdaptiveAudioDecoder(
    JobQueue* job_queue,
    const AudioStreamInfo& audio_stream_info,
    SbDrmSystem drm_system,
    const AudioDecoderCreator& audio_decoder_creator,
    bool enable_reset_audio_decoder,
    const OutputFormatAdjustmentCallback& output_adjustment_callback)
    : AdaptiveAudioDecoder(job_queue,
                           audio_stream_info,
                           drm_system,
                           audio_decoder_creator,
                           output_adjustment_callback) {
  enable_reset_audio_decoder_ = enable_reset_audio_decoder;
}

AdaptiveAudioDecoder::~AdaptiveAudioDecoder() {
  SB_CHECK(BelongsToCurrentThread());

  if (audio_decoder_) {
    TeardownAudioDecoder();
  }
  ResetInternal();
}

void AdaptiveAudioDecoder::Initialize(const OutputCB& output_cb,
                                      const ErrorCB& error_cb) {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(output_cb);
  SB_DCHECK(!output_cb_);
  SB_DCHECK(error_cb);
  SB_DCHECK(!error_cb_);
  SB_DCHECK(!audio_decoder_);
  SB_DCHECK(!stream_ended_);

  output_cb_ = output_cb;
  error_cb_ = error_cb;
}

void AdaptiveAudioDecoder::Decode(const InputBuffers& input_buffers,
                                  const ConsumedCB& consumed_cb) {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(!stream_ended_);
  SB_DCHECK(output_cb_);
  SB_DCHECK(error_cb_);
  SB_DCHECK(!flushing_);
  SB_DCHECK(pending_input_buffers_.empty());
  SB_DCHECK(!pending_consumed_cb_);
  SB_DCHECK(!input_buffers.empty());
  SB_DCHECK_EQ(input_buffers.front()->sample_type(), kSbMediaTypeAudio);
  SB_DCHECK_NE(input_buffers.front()->audio_stream_info().codec,
               kSbMediaAudioCodecNone);

  if (!first_input_written_) {
    first_input_written_ = true;
  }
  if (!audio_decoder_) {
    InitializeAudioDecoder(input_buffers.front()->audio_stream_info());
    if (audio_decoder_) {
      audio_decoder_->Decode(input_buffers, consumed_cb);
    }
    return;
  }
  if (IsAudioSampleInfoSubstantiallyDifferent(
          input_audio_stream_info_,
          input_buffers.front()->audio_stream_info())) {
    flushing_ = true;
    pending_input_buffers_ = input_buffers;
    pending_consumed_cb_ = consumed_cb;
    audio_decoder_->WriteEndOfStream();
    return;
  }
#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  for (size_t i = 1; i < input_buffers.size(); i++) {
    if (IsAudioSampleInfoSubstantiallyDifferent(
            input_audio_stream_info_, input_buffers[i]->audio_stream_info())) {
      error_cb_(kSbPlayerErrorDecode,
                "Configuration switches should NOT happen within a batch.");
      return;
    }
  }
#endif
  audio_decoder_->Decode(input_buffers, consumed_cb);
}

void AdaptiveAudioDecoder::WriteEndOfStream() {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(!stream_ended_);
  SB_DCHECK(output_cb_);
  SB_DCHECK(error_cb_);
  SB_DCHECK(pending_input_buffers_.empty());
  SB_DCHECK(!pending_consumed_cb_);

  stream_ended_ = true;
  if (first_input_written_) {
    audio_decoder_->WriteEndOfStream();
  } else {
    decoded_audios_.push(DecodedAudio::CreateEOSBuffer());
    Schedule(output_cb_);
  }
}

std::optional<DecodedAudio> AdaptiveAudioDecoder::Read(
    int* samples_per_second) {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(!decoded_audios_.empty());
  if (decoded_audios_.empty()) {
    return std::nullopt;
  }

  std::optional<DecodedAudio> ret = std::move(decoded_audios_.front());
  decoded_audios_.pop();

  SB_DCHECK(ret->is_end_of_stream() ||
            ret->sample_type() == output_sample_type_);
  SB_DCHECK(ret->is_end_of_stream() ||
            ret->channels() == output_number_of_channels_);

  SB_DCHECK(first_output_received_ || ret->is_end_of_stream());
  *samples_per_second = first_output_received_ ? output_samples_per_second_
                                               : initial_samples_per_second_;
  return ret;
}

void AdaptiveAudioDecoder::Reset() {
  SB_CHECK(BelongsToCurrentThread());

  if (audio_decoder_) {
    if (enable_reset_audio_decoder_) {
      audio_decoder_->Reset();
      resampler_.reset();
      channel_mixer_.reset();
    } else {
      TeardownAudioDecoder();
    }
  }
  ResetInternal();
}

void AdaptiveAudioDecoder::InitializeAudioDecoder(
    const AudioStreamInfo& audio_stream_info) {
  SB_DCHECK(!audio_decoder_);
  SB_DCHECK(output_cb_);
  SB_DCHECK(error_cb_);
  SB_DCHECK(!resampler_);
  SB_DCHECK(!channel_mixer_);

  input_audio_stream_info_ = audio_stream_info;
  output_format_checked_ = false;
  audio_decoder_ =
      audio_decoder_creator_(input_audio_stream_info_, drm_system_);

  if (!audio_decoder_) {
    error_cb_(kSbPlayerErrorDecode, "Decoder adapter cannot create decoder.");
    return;
  }
  audio_decoder_->Initialize(
      std::bind(&AdaptiveAudioDecoder::OnDecoderOutput, this), error_cb_);
}

void AdaptiveAudioDecoder::TeardownAudioDecoder() {
  audio_decoder_.reset();
  resampler_.reset();
  channel_mixer_.reset();
  first_input_written_ = false;
}

void AdaptiveAudioDecoder::OnDecoderOutput() {
  if (!BelongsToCurrentThread()) {
    Schedule(std::bind(&AdaptiveAudioDecoder::OnDecoderOutput, this));
    return;
  }
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(output_cb_);

  int decoded_sample_rate;
  std::optional<DecodedAudio> decoded_audio =
      audio_decoder_->Read(&decoded_sample_rate);

  SB_DCHECK(decoded_audio.has_value());

  if (!decoded_audio->is_end_of_stream() && !decoded_audio->frames()) {
    // Empty output doesn't need further process.
    return;
  }

  if (!first_output_received_) {
    first_output_received_ = true;
    output_sample_type_ = decoded_audio->sample_type();
    output_samples_per_second_ = decoded_sample_rate;
    if (output_adjustment_callback_) {
      output_adjustment_callback_(&output_sample_type_,
                                  &output_samples_per_second_,
                                  &output_number_of_channels_);
    }
  }

  if (decoded_audio->is_end_of_stream()) {
    // Flush resampler.
    if (resampler_) {
      std::optional<DecodedAudio> resampler_output =
          resampler_->WriteEndOfStream();
      if (resampler_output && resampler_output->size_in_bytes() > 0) {
        if (channel_mixer_) {
          resampler_output = channel_mixer_->Mix(std::move(*resampler_output));
        }
        decoded_audios_.push(std::move(*resampler_output));
        Schedule(output_cb_);
      }
    }
    if (flushing_) {
      SB_DCHECK(audio_decoder_);
      TeardownAudioDecoder();
      flushing_ = false;
      InputBuffers input_buffers = std::move(pending_input_buffers_);
      Decode(input_buffers, ResetAndReturn(&pending_consumed_cb_));
    } else {
      SB_DCHECK(stream_ended_);
      decoded_audios_.push(std::move(*decoded_audio));
      Schedule(output_cb_);
    }
    return;
  }

  SB_DCHECK_EQ(input_audio_stream_info_.number_of_channels,
               decoded_audio->channels());
  if (!output_format_checked_) {
    SB_DCHECK(!resampler_);
    SB_DCHECK(!channel_mixer_);
    output_format_checked_ = true;
    if (output_sample_type_ != decoded_audio->sample_type() ||
        output_samples_per_second_ != decoded_sample_rate) {
      resampler_ = AudioResampler::Create(
          decoded_audio->sample_type(), decoded_sample_rate,
          output_sample_type_, output_samples_per_second_,
          input_audio_stream_info_.number_of_channels);
    }
    if (input_audio_stream_info_.number_of_channels !=
        output_number_of_channels_) {
      channel_mixer_ = AudioChannelLayoutMixer::Create(
          output_sample_type_, output_number_of_channels_);
    }
  }
  if (resampler_) {
    decoded_audio = resampler_->Resample(std::move(*decoded_audio));
  } else {
    // If |resampler_| is NULL, |output_samples_per_second_| should be the same
    // as |decoded_sample_rate|.
    SB_DCHECK_EQ(output_samples_per_second_, decoded_sample_rate);
  }
  if (decoded_audio && decoded_audio->size_in_bytes() > 0) {
    if (channel_mixer_) {
      decoded_audio = channel_mixer_->Mix(std::move(*decoded_audio));
    }
    decoded_audios_.push(std::move(*decoded_audio));
    Schedule(output_cb_);
  }
}

void AdaptiveAudioDecoder::ResetInternal() {
  CancelPendingJobs();
  while (!decoded_audios_.empty()) {
    decoded_audios_.pop();
  }
  pending_input_buffers_.clear();
  pending_consumed_cb_ = nullptr;
  flushing_ = false;
  stream_ended_ = false;
  first_output_received_ = false;
  first_input_written_ = false;
  output_format_checked_ = false;
}

}  // namespace starboard
