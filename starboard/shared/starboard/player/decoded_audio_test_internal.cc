// Copyright 2023 The Cobalt Authors. All Rights Reserved.
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

#include <cmath>
#include <utility>

#include "starboard/common/log.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/decoded_audio_internal.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {
namespace {

constexpr int kChannels = 2;
constexpr int64_t kTimestampUsec = 1'000'000;
constexpr int kSizeInBytes = 4192;
constexpr int kSampleRate = 22050;

constexpr double kPi = 3.1415926535897932384626;

constexpr SbMediaAudioSampleType kSampleTypes[] = {
    kSbMediaAudioSampleTypeInt16Deprecated, kSbMediaAudioSampleTypeFloat32};

// The following two functions fill `data` with audio samples of a sine wave
// starting from `initial_angle`.  `stride` is the number of samples per group.
// For example:
// 1. When `stride` is one, all samples filled with be continuous.
// 2. When `stride` is 2, filling `data` and `data + 1` fills an interleaved
//    stereo audio buffer with samples.
void Fill(int16_t* data,
          double initial_angle,
          double angle_step,
          int count,
          int stride) {
  SB_DCHECK(data);

  auto current_angel = initial_angle;
  for (int i = 0; i < count; ++i) {
    *data = static_cast<int16_t>(sin(current_angel) * 32767);
    current_angel += angle_step;
    data += stride;
  }
}

void Fill(float* data,
          double initial_angle,
          double angle_step,
          int count,
          int stride) {
  SB_DCHECK(data);

  auto current_angel = initial_angle;
  for (int i = 0; i < count; ++i) {
    *data = static_cast<float>(sin(current_angel));
    current_angel += angle_step;
    data += stride;
  }
}

// The following two functions verify `data` with audio samples against a sine
// wave starting from `initial_angle`.  `stride` is the number of samples per
// group.  For example:
// 1. When `stride` is one, all samples will be treated as continuous.
// 2. When `stride` is 2, verifying against `data` and `data + 1` verifies an
//    interleaved stereo audio buffer.
void Verify(const int16_t* data,
            double initial_angle,
            double angle_step,
            int count,
            int stride) {
  SB_DCHECK(data);

  auto current_angel = initial_angle;
  for (int i = 0; i < count; ++i) {
    const auto current_value = static_cast<int16_t>(sin(current_angel) * 32767);
    // Using `ASSERT_NEAR()` to allow for small value drifting due to
    // conversions between sample types.
    ASSERT_NEAR(static_cast<double>(*data), static_cast<double>(current_value),
                2.0 / 32767);
    current_angel += angle_step;
    data += stride;
  }
}

void Verify(const float* data,
            double initial_angle,
            double angle_step,
            int count,
            int stride) {
  SB_DCHECK(data);

  auto current_angel = initial_angle;
  for (int i = 0; i < count; ++i) {
    const auto current_value = static_cast<float>(sin(current_angel));
    // Using `ASSERT_NEAR()` to allow for small value drifting due to
    // conversions between sample types.
    ASSERT_NEAR(static_cast<double>(*data), static_cast<double>(current_value),
                2.0 / 32767);
    current_angel += angle_step;
    data += stride;
  }
}

// Fill `decoded_audio` with sine wave samples, with phase shift of Pi/2 on each
// channel.
void Fill(DecodedAudio* decoded_audio) {
  SB_DCHECK(decoded_audio);

  bool is_int16 =
      decoded_audio->sample_type() == kSbMediaAudioSampleTypeInt16Deprecated;

  for (int i = 0; i < decoded_audio->channels(); ++i) {
    if (is_int16) {
      Fill(decoded_audio->data_as_int16() + i, kPi / 2 * i, kPi / 180,
           decoded_audio->frames(), decoded_audio->channels());
    } else {
      Fill(decoded_audio->data_as_float32() + i, kPi / 2 * i, kPi / 180,
           decoded_audio->frames(), decoded_audio->channels());
    }
  }
}

// verify `decoded_audio` against sine wave samples, with phase shift of Pi/2 on
// each channel.
void Verify(const DecodedAudio& decoded_audio) {
  bool is_int16 =
      decoded_audio.sample_type() == kSbMediaAudioSampleTypeInt16Deprecated;

  for (int i = 0; i < decoded_audio.channels(); ++i) {
    if (is_int16) {
      ASSERT_NO_FATAL_FAILURE(
          Verify(decoded_audio.data_as_int16() + i, kPi / 2 * i, kPi / 180,
                 decoded_audio.frames(), decoded_audio.channels()));
    } else {
      ASSERT_NO_FATAL_FAILURE(
          Verify(decoded_audio.data_as_float32() + i, kPi / 2 * i, kPi / 180,
                 decoded_audio.frames(), decoded_audio.channels()));
    }
  }
}

TEST(DecodedAudioTest, CreateEOSBuffer) {
  DecodedAudio decoded_audio = DecodedAudio::CreateEOSBuffer();
  EXPECT_TRUE(decoded_audio.is_end_of_stream());
}

TEST(DecodedAudioTest, CtorWithSize) {
  for (auto sample_type : kSampleTypes) {
    DecodedAudio decoded_audio(kChannels, sample_type, kTimestampUsec,
                               kSizeInBytes);

    EXPECT_FALSE(decoded_audio.is_end_of_stream());
    EXPECT_EQ(decoded_audio.channels(), kChannels);
    EXPECT_EQ(decoded_audio.sample_type(), sample_type);
    EXPECT_EQ(decoded_audio.size_in_bytes(), kSizeInBytes);
    EXPECT_EQ(decoded_audio.frames(),
              kSizeInBytes / GetBytesPerSample(decoded_audio.sample_type()) /
                  kChannels);

    Fill(&decoded_audio);
    Verify(decoded_audio);
  }
}

TEST(DecodedAudioTest, CtorWithMoveCtor) {
  Buffer original(128);
  memset(original.data(), 'x', 128);

  const uint8_t* original_data_pointer = original.data();

  DecodedAudio decoded_audio(kChannels, kSampleTypes[0], kTimestampUsec, 128,
                             std::move(original));
  ASSERT_EQ(decoded_audio.size_in_bytes(), 128);
  ASSERT_NE(decoded_audio.data(), nullptr);
  ASSERT_EQ(decoded_audio.data(), original_data_pointer);

  for (int i = 0; i < decoded_audio.size_in_bytes(); ++i) {
    ASSERT_EQ(decoded_audio.data()[i], 'x');
  }
}

TEST(DecodedAudioTest, AdjustForSeekTime) {
  for (int channels = 1; channels <= 6; ++channels) {
    for (auto sample_type : kSampleTypes) {
      DecodedAudio original_decoded_audio(kChannels, sample_type,
                                          kTimestampUsec, kSizeInBytes);
      Fill(&original_decoded_audio);

      DecodedAudio adjusted_decoded_audio =
          original_decoded_audio.CloneForTesting();

      // Adjust to the beginning of `adjusted_decoded_audio` should be a no-op.
      adjusted_decoded_audio.AdjustForSeekTime(
          kSampleRate, adjusted_decoded_audio.timestamp());
      ASSERT_EQ(original_decoded_audio, adjusted_decoded_audio);

      // Adjust to an invalid timestamp before the time range of
      // `adjusted_decoded_audio`, it's a no-op.
      adjusted_decoded_audio.AdjustForSeekTime(
          kSampleRate, adjusted_decoded_audio.timestamp() - 1'000'000LL / 2);
      ASSERT_EQ(original_decoded_audio, adjusted_decoded_audio);

      // Adjust to an invalid timestamp after the time range of
      // `adjusted_decoded_audio`, it's also a no-op.
      adjusted_decoded_audio.AdjustForSeekTime(
          kSampleRate, adjusted_decoded_audio.timestamp() + 1'000'000LL * 100);
      ASSERT_EQ(original_decoded_audio, adjusted_decoded_audio);

      const int64_t duration =
          AudioFramesToDuration(adjusted_decoded_audio.frames(), kSampleRate);
      const int64_t duration_of_one_frame =
          AudioFramesToDuration(1, kSampleRate) + 1;
      for (int i = 1; i < 10; ++i) {
        adjusted_decoded_audio = original_decoded_audio.CloneForTesting();
        // Adjust to the middle of `adjusted_decoded_audio`.
        int64_t seek_time =
            adjusted_decoded_audio.timestamp() + duration * i / 10;
        adjusted_decoded_audio.AdjustForSeekTime(kSampleRate, seek_time);
        ASSERT_NEAR(adjusted_decoded_audio.frames(),
                    original_decoded_audio.frames() * (10 - i) / 10, 1);

        ASSERT_LE(adjusted_decoded_audio.timestamp(), seek_time);
        ASSERT_NEAR(adjusted_decoded_audio.timestamp(), seek_time,
                    duration_of_one_frame);

        auto offset_in_bytes = original_decoded_audio.size_in_bytes() -
                               adjusted_decoded_audio.size_in_bytes();
        ASSERT_TRUE(memcmp(adjusted_decoded_audio.data(),
                           original_decoded_audio.data() + offset_in_bytes,
                           adjusted_decoded_audio.size_in_bytes()) == 0);
      }
    }
  }
}

TEST(DecodedAudioTest, AdjustForDiscardedDurations) {
  for (int channels = 1; channels <= 6; ++channels) {
    for (auto sample_type : kSampleTypes) {
      DecodedAudio original_decoded_audio(kChannels, sample_type,
                                          kTimestampUsec, kSizeInBytes);
      Fill(&original_decoded_audio);

      DecodedAudio adjusted_decoded_audio =
          original_decoded_audio.CloneForTesting();

      adjusted_decoded_audio.AdjustForDiscardedDurations(kSampleRate, 0, 0);
      ASSERT_EQ(original_decoded_audio, adjusted_decoded_audio);

      auto duration_of_decoded_audio =
          AudioFramesToDuration(original_decoded_audio.frames(), kSampleRate);
      auto quarter_duration = duration_of_decoded_audio / 4;
      adjusted_decoded_audio.AdjustForDiscardedDurations(
          kSampleRate, quarter_duration, quarter_duration);
      ASSERT_NEAR(adjusted_decoded_audio.frames(),
                  original_decoded_audio.frames() / 2, 2);
      ASSERT_EQ(adjusted_decoded_audio.timestamp(),
                original_decoded_audio.timestamp());

      adjusted_decoded_audio = original_decoded_audio.CloneForTesting();
      // Adjust more frames than it has from front
      adjusted_decoded_audio.AdjustForDiscardedDurations(
          kSampleRate, duration_of_decoded_audio * 2, 0);
      ASSERT_EQ(adjusted_decoded_audio.frames(), 0);
      ASSERT_EQ(adjusted_decoded_audio.timestamp(),
                original_decoded_audio.timestamp());

      adjusted_decoded_audio = original_decoded_audio.CloneForTesting();
      // Adjust more frames than it has from back
      adjusted_decoded_audio.AdjustForDiscardedDurations(
          kSampleRate, 0, duration_of_decoded_audio * 2);
      ASSERT_EQ(adjusted_decoded_audio.frames(), 0);
      ASSERT_EQ(adjusted_decoded_audio.timestamp(),
                original_decoded_audio.timestamp());
    }
  }
}

TEST(DecodedAudioTest, SwitchFormatTo) {
  for (auto original_sample_type : kSampleTypes) {
    DecodedAudio original_decoded_audio(kChannels, original_sample_type,
                                        kTimestampUsec, kSizeInBytes);

    Fill(&original_decoded_audio);

    for (auto new_sample_type : kSampleTypes) {
      if (original_decoded_audio.sample_type() != new_sample_type) {
        DecodedAudio new_decoded_audio =
            original_decoded_audio.SwitchFormatTo(new_sample_type);

        EXPECT_FALSE(new_decoded_audio.is_end_of_stream());
        EXPECT_EQ(new_decoded_audio.channels(),
                  original_decoded_audio.channels());
        EXPECT_EQ(new_decoded_audio.timestamp(),
                  original_decoded_audio.timestamp());
        EXPECT_EQ(new_decoded_audio.frames(), original_decoded_audio.frames());
        EXPECT_EQ(new_decoded_audio.sample_type(), new_sample_type);

        ASSERT_NO_FATAL_FAILURE(Verify(new_decoded_audio));
      }
    }
  }
}

TEST(DecodedAudioTest, Clone) {
  for (auto sample_type : kSampleTypes) {
    DecodedAudio decoded_audio(kChannels, sample_type, kTimestampUsec,
                               kSizeInBytes);
    Fill(&decoded_audio);
    auto copy = decoded_audio.CloneForTesting();
    ASSERT_EQ(copy, decoded_audio);
    ASSERT_GT(decoded_audio.size_in_bytes(), 0);
    decoded_audio.data()[0] = ~decoded_audio.data()[0];
    ASSERT_NE(copy, decoded_audio);
  }
}

class DecodedAudioNeonTest : public ::testing::Test {
 protected:
  DecodedAudio CreateInt16InterleavedRamp(int total_samples) {
    int size_in_bytes = total_samples * sizeof(int16_t);
    DecodedAudio base(kChannels, kSbMediaAudioSampleTypeInt16Deprecated,
                      kTimestampUsec, size_in_bytes);
    int16_t* data = base.data_as_int16();
    for (int i = 0; i < total_samples; ++i) {
      data[i] = static_cast<int16_t>(i - 32768);
    }
    return base;
  }

  DecodedAudio CreateFloat32InterleavedRamp(int total_samples) {
    int size_in_bytes = total_samples * sizeof(float);
    DecodedAudio base(kChannels, kSbMediaAudioSampleTypeFloat32, kTimestampUsec,
                      size_in_bytes);
    float* data = base.data_as_float32();
    for (int i = 0; i < total_samples; ++i) {
      data[i] = -2.0f + 4.0f * (static_cast<float>(i) / total_samples);
    }
    return base;
  }

  void VerifyContent(const DecodedAudio& ref, const DecodedAudio& simd) {
    ASSERT_EQ(ref.size_in_bytes(), simd.size_in_bytes());
    if (ref.sample_type() == kSbMediaAudioSampleTypeFloat32) {
      const float* ref_data = ref.data_as_float32();
      const float* simd_data = simd.data_as_float32();
      int num_floats = ref.size_in_bytes() / sizeof(float);
      for (int i = 0; i < num_floats; ++i) {
        ASSERT_FLOAT_EQ(ref_data[i], simd_data[i]) << "Mismatch at index " << i;
      }
    } else {
      const int16_t* ref_data = ref.data_as_int16();
      const int16_t* simd_data = simd.data_as_int16();
      int num_ints = ref.size_in_bytes() / sizeof(int16_t);
      for (int i = 0; i < num_ints; ++i) {
        ASSERT_EQ(ref_data[i], simd_data[i]) << "Mismatch at index " << i;
      }
    }
  }

  void VerifySwitchFormat(const DecodedAudio& base_audio,
                          SbMediaAudioSampleType target_sample_type) {
    DecodedAudio ref =
        base_audio.SwitchFormatTo(target_sample_type, /*force_simd=*/false);
    DecodedAudio simd =
        base_audio.SwitchFormatTo(target_sample_type, /*force_simd=*/true);
    VerifyContent(ref, simd);
  }
};

TEST_F(DecodedAudioNeonTest, SwitchFormatTo_NeonSimdExhaustive) {
  // 1. Test Int16 Interleaved -> Float32 Interleaved
  auto int16_interleaved = CreateInt16InterleavedRamp(65536);
  VerifySwitchFormat(int16_interleaved, kSbMediaAudioSampleTypeFloat32);

  // 2. Test Float32 Interleaved -> Int16 Interleaved
  auto float_interleaved = CreateFloat32InterleavedRamp(8000);
  VerifySwitchFormat(float_interleaved, kSbMediaAudioSampleTypeInt16Deprecated);
}

TEST_F(DecodedAudioNeonTest, SwitchFormatTo_NeonSimdUnaligned) {
  // Test with unaligned sizes (non-multiples of 8/16) to verify C++ scalar
  // fallbacks.
  auto int16_interleaved = CreateInt16InterleavedRamp(65536 + 14);
  VerifySwitchFormat(int16_interleaved, kSbMediaAudioSampleTypeFloat32);

  auto float_interleaved = CreateFloat32InterleavedRamp(8000 + 14);
  VerifySwitchFormat(float_interleaved, kSbMediaAudioSampleTypeInt16Deprecated);
}

TEST(DecodedAudioTest, MoveConstructor) {
  DecodedAudio original(kChannels, kSampleTypes[0], kTimestampUsec,
                        kSizeInBytes);
  Fill(&original);

  const uint8_t* original_data = original.data();
  int original_channels = original.channels();
  auto original_sample_type = original.sample_type();
  int64_t original_timestamp = original.timestamp();
  int original_size = original.size_in_bytes();

  DecodedAudio moved(std::move(original));

  EXPECT_EQ(moved.data(), original_data);
  EXPECT_EQ(moved.channels(), original_channels);
  EXPECT_EQ(moved.sample_type(), original_sample_type);
  EXPECT_EQ(moved.timestamp(), original_timestamp);
  EXPECT_EQ(moved.size_in_bytes(), original_size);
  Verify(moved);

  EXPECT_TRUE(original.is_end_of_stream());
  EXPECT_EQ(original.channels(), 0);
  EXPECT_EQ(original.size_in_bytes(), 0);
  EXPECT_EQ(original.timestamp(), 0);
  EXPECT_EQ(original.data(), nullptr);
}

TEST(DecodedAudioTest, MoveAssignment) {
  DecodedAudio original(kChannels, kSampleTypes[0], kTimestampUsec,
                        kSizeInBytes);
  Fill(&original);

  const uint8_t* original_data = original.data();
  int original_channels = original.channels();
  auto original_sample_type = original.sample_type();
  int64_t original_timestamp = original.timestamp();
  int original_size = original.size_in_bytes();

  DecodedAudio moved = DecodedAudio::CreateEOSBuffer();
  moved = std::move(original);

  EXPECT_EQ(moved.data(), original_data);
  EXPECT_EQ(moved.channels(), original_channels);
  EXPECT_EQ(moved.sample_type(), original_sample_type);
  EXPECT_EQ(moved.timestamp(), original_timestamp);
  EXPECT_EQ(moved.size_in_bytes(), original_size);
  Verify(moved);

  EXPECT_TRUE(original.is_end_of_stream());
  EXPECT_EQ(original.channels(), 0);
  EXPECT_EQ(original.size_in_bytes(), 0);
  EXPECT_EQ(original.timestamp(), 0);
  EXPECT_EQ(original.data(), nullptr);
}

}  // namespace
}  // namespace starboard
