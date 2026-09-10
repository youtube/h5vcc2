// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

// clang-format off
#include "starboard/shared/starboard/player/filter/audio_resampler.h"
// clang-format on

#include <deque>

#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/configuration.h"
#include "starboard/shared/starboard/player/filter/interleaved_sinc_resampler.h"

namespace starboard {

namespace {

// CPU based simple AudioResampler implementation.
class AudioResamplerImpl : public AudioResampler {
 public:
  AudioResamplerImpl(SbMediaAudioSampleType source_sample_type,
                     int source_sample_rate,
                     SbMediaAudioSampleType destination_sample_type,
                     int destination_sample_rate,
                     int channels)
      : destination_sample_type_(destination_sample_type),
        interleaved_resampler_(static_cast<double>(source_sample_rate) /
                                   static_cast<double>(destination_sample_rate),
                               channels) {}

  std::optional<DecodedAudio> Resample(DecodedAudio buffer) override;

  std::optional<DecodedAudio> WriteEndOfStream() override;

 private:
  const SbMediaAudioSampleType destination_sample_type_;

  InterleavedSincResampler interleaved_resampler_;

  std::deque<DecodedAudio> audio_inputs_;

  size_t frames_to_resample_ = 0;
  size_t frames_resampled_ = 0;
  size_t frames_outputted_ = 0;
};

}  // namespace

// static
std::unique_ptr<AudioResampler> AudioResampler::Create(
    SbMediaAudioSampleType source_sample_type,
    int source_sample_rate,
    SbMediaAudioSampleType destination_sample_type,
    int destination_sample_rate,
    int channels) {
  return std::unique_ptr<AudioResampler>(new AudioResamplerImpl(
      source_sample_type, source_sample_rate, destination_sample_type,
      destination_sample_rate, channels));
}

std::optional<DecodedAudio> AudioResamplerImpl::WriteEndOfStream() {
  double sample_rate_ratio = interleaved_resampler_.GetSampleRateRatio();
  int out_num_of_frames =
      round(frames_to_resample_ / sample_rate_ratio) - frames_outputted_;
  if (out_num_of_frames > 0) {
    SB_DCHECK(!audio_inputs_.empty());
    // If there're frames left in |interleaved_resampler_|, |audio_inputs_|
    // should not be empty. To be cautious in production, we add a check here.
    if (!audio_inputs_.empty()) {
      interleaved_resampler_.QueueBuffer(nullptr, 0);
      int channels = interleaved_resampler_.channels();
      int resampled_audio_size = out_num_of_frames * channels * sizeof(float);
      DecodedAudio resampled_audio(channels, kSbMediaAudioSampleTypeFloat32,
                                   audio_inputs_.front().timestamp(),
                                   resampled_audio_size);

      float* dst = reinterpret_cast<float*>(resampled_audio.data());
      interleaved_resampler_.Resample(dst, out_num_of_frames);

      if (resampled_audio.sample_type() != destination_sample_type_) {
        resampled_audio =
            resampled_audio.SwitchFormatTo(destination_sample_type_);
      }
      return resampled_audio;
    }
  }

  return std::nullopt;
}

std::optional<DecodedAudio> AudioResamplerImpl::Resample(
    DecodedAudio audio_input) {
  SB_DCHECK_EQ(audio_input.channels(), interleaved_resampler_.channels());

  // It does nothing if source sample type is float.
  if (audio_input.sample_type() != kSbMediaAudioSampleTypeFloat32) {
    audio_input = audio_input.SwitchFormatTo(kSbMediaAudioSampleTypeFloat32);
  }

  int num_of_input_frames = audio_input.frames();
  frames_to_resample_ += num_of_input_frames;

  audio_inputs_.push_back(std::move(audio_input));
  float* input_samples = reinterpret_cast<float*>(audio_inputs_.back().data());
  interleaved_resampler_.QueueBuffer(input_samples, num_of_input_frames);

  // Check if we have enough frames to output.
  DecodedAudio& next_audio_to_output = audio_inputs_.front();
  int num_of_output_frames =
      static_cast<int>(
          ceil((frames_resampled_ + next_audio_to_output.frames()) /
               interleaved_resampler_.GetSampleRateRatio())) -
      frames_outputted_;
  int channels = next_audio_to_output.channels();

  std::optional<DecodedAudio> resampled_audio;
  if (interleaved_resampler_.HasEnoughData(num_of_output_frames)) {
    int output_audio_size = num_of_output_frames * channels * sizeof(float);
    DecodedAudio output(channels, kSbMediaAudioSampleTypeFloat32,
                        next_audio_to_output.timestamp(), output_audio_size);
    float* dst = reinterpret_cast<float*>(output.data());
    interleaved_resampler_.Resample(dst, num_of_output_frames);
    frames_resampled_ += next_audio_to_output.frames();
    frames_outputted_ += num_of_output_frames;

    if (output.sample_type() != destination_sample_type_) {
      output = output.SwitchFormatTo(destination_sample_type_);
    }
    resampled_audio = std::move(output);

    audio_inputs_.pop_front();
  }

  return resampled_audio;
}

}  // namespace starboard
