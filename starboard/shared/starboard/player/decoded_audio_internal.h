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

#ifndef STARBOARD_SHARED_STARBOARD_PLAYER_DECODED_AUDIO_INTERNAL_H_
#define STARBOARD_SHARED_STARBOARD_PLAYER_DECODED_AUDIO_INTERNAL_H_

#include <optional>
#include <ostream>

#include "starboard/media.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/player/buffer_internal.h"

namespace starboard {

// Decoded audio frames produced by an audio decoder.  It can contain multiple
// frames with continuous timestamps.
class DecodedAudio {
 public:
  static DecodedAudio CreateEOSBuffer();
  DecodedAudio(int channels,
               SbMediaAudioSampleType sample_type,
               int64_t timestamp,
               int size_in_bytes);

  DecodedAudio(int channels,
               SbMediaAudioSampleType sample_type,
               int64_t timestamp,
               int size_in_bytes,
               Buffer&& storage);

  // Move-only semantics
  DecodedAudio(DecodedAudio&& other) noexcept;
  DecodedAudio& operator=(DecodedAudio&& other) noexcept;

  // Disable copy and assignment.
  DecodedAudio(const DecodedAudio&) = delete;
  DecodedAudio& operator=(const DecodedAudio&) = delete;

  static void EnableSimdBasedAudioFormatSwitching();

  int channels() const { return channels_; }
  SbMediaAudioSampleType sample_type() const { return sample_type_; }

  bool is_end_of_stream() const { return channels_ == 0; }
  int64_t timestamp() const { return timestamp_; }
  const uint8_t* data() const { return storage_.data() + offset_in_bytes_; }
  const int16_t* data_as_int16() const {
    return reinterpret_cast<const int16_t*>(storage_.data() + offset_in_bytes_);
  }
  const float* data_as_float32() const {
    return reinterpret_cast<const float*>(storage_.data() + offset_in_bytes_);
  }
  int size_in_bytes() const { return size_in_bytes_; }

  uint8_t* data() { return storage_.data() + offset_in_bytes_; }
  int16_t* data_as_int16() {
    return reinterpret_cast<int16_t*>(storage_.data() + offset_in_bytes_);
  }
  float* data_as_float32() {
    return reinterpret_cast<float*>(storage_.data() + offset_in_bytes_);
  }
  int frames() const;

  void ShrinkTo(int new_size_in_bytes);

  // During seeking, the target time can be in the middle of the DecodedAudio
  // object.  This function will adjust the object to the seek target time by
  // removing the frames in the beginning that are before the seek target time.
  void AdjustForSeekTime(int sample_rate, int64_t seeking_to_time);
  void AdjustForDiscardedDurations(int sample_rate,
                                   int64_t discarded_duration_from_front,
                                   int64_t discarded_duration_from_back);

  // During format switching, this method can perform sample type conversions
  // on-the-fly.
  // Note: The `force_simd` parameter allows explicitly forcing or disabling
  // the SIMD path, which is primarily intended for unit testing different
  // execution paths. When left as `nullopt` (default), it automatically
  // resolves to the global experimental setting.
  DecodedAudio SwitchFormatTo(SbMediaAudioSampleType new_sample_type,
                              std::optional<bool> force_simd =
                                  std::nullopt) const SB_WARN_UNUSED_RESULT;

  DecodedAudio CloneForTesting() const;

 private:
  DecodedAudio();

  DecodedAudio SwitchSampleTypeTo(SbMediaAudioSampleType new_sample_type,
                                  bool enable_simd) const;

  // These NEON helper methods are always declared to avoid leaking platform-
  // specific preprocessor macros (like USE_NEON_FOR_AUDIO) to this header
  // file. They are only defined and called in the implementation (.cc) file
  // when NEON is enabled on the target platform.
  bool SwitchSampleTypeTo_NEON(SbMediaAudioSampleType new_sample_type,
                               DecodedAudio* destination_audio) const;

  int channels_;
  SbMediaAudioSampleType sample_type_;
  // The timestamp of the first audio frame in microseconds.
  int64_t timestamp_;
  Buffer storage_;
  // The audio samples to be played are stored in the memory region starts from
  // `storage_.data() + offset_in_bytes_`, `size_in_bytes_` bytes in total.
  int offset_in_bytes_ = 0;
  int size_in_bytes_ = 0;
};

bool operator==(const DecodedAudio& left, const DecodedAudio& right);
bool operator!=(const DecodedAudio& left, const DecodedAudio& right);

// For debugging or testing only.
std::ostream& operator<<(std::ostream& os, const DecodedAudio& decoded_audio);

}  // namespace starboard

#endif  // STARBOARD_SHARED_STARBOARD_PLAYER_DECODED_AUDIO_INTERNAL_H_
