// Copyright 2018 The Cobalt Authors. All Rights Reserved.
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

#include <unistd.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "starboard/common/check_op.h"
#include "starboard/common/media.h"
#include "starboard/common/ref_counted.h"
#include "starboard/common/time.h"
#include "starboard/configuration_constants.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/media_support_internal.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/decoded_audio_internal.h"
#include "starboard/shared/starboard/player/filter/audio_decoder_internal.h"
#include "starboard/shared/starboard/player/filter/player_components.h"
#include "starboard/shared/starboard/player/filter/stub_player_components_factory.h"
#include "starboard/shared/starboard/player/filter/testing/test_util.h"
#include "starboard/shared/starboard/player/input_buffer_internal.h"
#include "starboard/shared/starboard/player/job_queue.h"
#include "starboard/shared/starboard/player/video_dmp_reader.h"
#include "starboard/system.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {
namespace {

using ::testing::Bool;
using ::testing::Combine;
using ::testing::ValuesIn;

const int64_t kWaitForNextEventTimeOut = 5'000'000;  // 5 seconds

DecodedAudio ConsolidateDecodedAudios(
    const std::vector<DecodedAudio>& decoded_audios) {
  if (decoded_audios.empty()) {
    return DecodedAudio(2, kSbMediaAudioSampleTypeFloat32, /*timestamp=*/0,
                        /*size_in_bytes=*/0);
  }

  int total_size_in_bytes = 0;
  int channels = decoded_audios.front().channels();
  auto sample_type = decoded_audios.front().sample_type();

  for (const auto& decoded_audio : decoded_audios) {
    SB_DCHECK_EQ(decoded_audio.channels(), channels);
    SB_DCHECK_EQ(decoded_audio.sample_type(), sample_type);
    total_size_in_bytes += decoded_audio.size_in_bytes();
  }

  DecodedAudio consolidated(channels, sample_type,
                            decoded_audios.front().timestamp(),
                            total_size_in_bytes);

  int offset_in_bytes = 0;
  for (const auto& decoded_audio : decoded_audios) {
    memcpy(consolidated.data() + offset_in_bytes, decoded_audio.data(),
           decoded_audio.size_in_bytes());
    offset_in_bytes += decoded_audio.size_in_bytes();
  }

  return consolidated;
}

int GetTotalFrames(const std::vector<DecodedAudio>& decoded_audios) {
  int total_frames = 0;
  for (const auto& decoded_audio : decoded_audios) {
    total_frames += decoded_audio.frames();
  }
  return total_frames;
}

class AudioDecoderTest
    : public ::testing::TestWithParam<std::tuple<const char*, bool>> {
 public:
  AudioDecoderTest()
      : test_filename_(std::get<0>(GetParam())),
        using_stub_decoder_(std::get<1>(GetParam())),
        dmp_reader_(test_filename_, VideoDmpReader::kEnableReadOnDemand) {
    SB_LOG(INFO) << "Testing " << test_filename_
                 << (using_stub_decoder_ ? " with stub audio decoder." : ".");
  }
  void SetUp() override {
    ASSERT_NE(dmp_reader_.audio_stream_info().codec, kSbMediaAudioCodecNone);
    ASSERT_GT(dmp_reader_.number_of_audio_buffers(), 0u);

    CreateComponents(dmp_reader_.audio_stream_info(), &audio_decoder_,
                     &audio_renderer_sink_);
    ASSERT_TRUE(audio_decoder_);
  }

 protected:
  enum Event { kConsumed, kOutput, kError };

  void CreateComponents(
      const AudioStreamInfo& audio_stream_info,
      std::unique_ptr<AudioDecoder>* audio_decoder,
      std::unique_ptr<AudioRendererSink>* audio_renderer_sink) {
    if (CreateAudioComponents(using_stub_decoder_, &job_queue_,
                              audio_stream_info, audio_decoder,
                              audio_renderer_sink)) {
      SB_CHECK(*audio_decoder);
      (*audio_decoder)
          ->Initialize(std::bind(&AudioDecoderTest::OnOutput, this),
                       std::bind(&AudioDecoderTest::OnError, this));
    }
  }

  void OnOutput() {
    std::lock_guard lock(event_queue_mutex_);
    event_queue_.push_back(kOutput);
  }

  void OnError() {
    std::lock_guard lock(event_queue_mutex_);
    event_queue_.push_back(kError);
  }

  void OnConsumed() {
    std::lock_guard lock(event_queue_mutex_);
    event_queue_.push_back(kConsumed);
  }

  void WaitForNextEvent(Event* event) {
    int64_t start = CurrentMonotonicTime();
    while (CurrentMonotonicTime() - start < kWaitForNextEventTimeOut) {
      job_queue_.RunUntilIdle();
      {
        std::lock_guard lock(event_queue_mutex_);
        if (!event_queue_.empty()) {
          *event = event_queue_.front();
          event_queue_.pop_front();

          if (*event == kConsumed) {
            ASSERT_FALSE(can_accept_more_input_);
            can_accept_more_input_ = true;
          }
          return;
        }
      }
      usleep(1000);
    }
    *event = kError;
  }

  // TODO: Add test to ensure that |consumed_cb| is not reused by the decoder.
  AudioDecoder::ConsumedCB consumed_cb() {
    return std::bind(&AudioDecoderTest::OnConsumed, this);
  }

  // This has to be called when the decoder is just initialized/reset or when
  // OnConsumed() is called.
  void WriteSingleInput(size_t index) {
    ASSERT_TRUE(can_accept_more_input_);
    ASSERT_LT(index, dmp_reader_.number_of_audio_buffers());

    can_accept_more_input_ = false;

    last_input_buffer_ = GetTestAudioInputBuffer(index);
    audio_decoder_->Decode({last_input_buffer_}, consumed_cb());
    written_inputs_.push_back(last_input_buffer_);
  }

  void WriteSingleInput(size_t index,
                        int64_t discarded_duration_from_front,
                        int64_t discarded_duration_from_back) {
    ASSERT_TRUE(can_accept_more_input_);
    ASSERT_LT(index, dmp_reader_.number_of_audio_buffers());

    can_accept_more_input_ = false;

    last_input_buffer_ = GetTestAudioInputBuffer(
        index, discarded_duration_from_front, discarded_duration_from_back);
    audio_decoder_->Decode({last_input_buffer_}, consumed_cb());
    written_inputs_.push_back(last_input_buffer_);
  }

  // This has to be called when OnOutput() is called.
  void ReadFromDecoder(bool* is_eos) {
    ASSERT_TRUE(is_eos);

    int decoded_sample_rate;
    std::optional<DecodedAudio> local_decoded_audio =
        audio_decoder_->Read(&decoded_sample_rate);
    ASSERT_TRUE(local_decoded_audio.has_value());
    if (!first_output_received_) {
      first_output_received_ = true;
      decoded_audio_sample_type_ = local_decoded_audio->sample_type();
      decoded_audio_sample_rate_ = decoded_sample_rate;
    }

    *is_eos = local_decoded_audio->is_end_of_stream();
    if (*is_eos) {
      return;
    }

    ASSERT_EQ(decoded_audio_sample_type_, local_decoded_audio->sample_type());
    ASSERT_EQ(decoded_audio_sample_rate_, decoded_sample_rate);

    if (!decoded_audios_.empty()) {
      ASSERT_LT(decoded_audios_.back().timestamp(),
                local_decoded_audio->timestamp());
    }
    if (!using_stub_decoder_ && invalid_inputs_.empty()) {
      ASSERT_NEAR(local_decoded_audio->timestamp(),
                  written_inputs_.front()->timestamp(), 5);
      written_inputs_.pop_front();
    }
    decoded_audios_.push_back(std::move(*local_decoded_audio));
  }

  void WriteMultipleInputs(size_t start_index,
                           size_t number_of_inputs_to_write,
                           bool* error_occurred = nullptr) {
    ASSERT_LE(start_index + number_of_inputs_to_write,
              dmp_reader_.number_of_audio_buffers());

    if (error_occurred) {
      *error_occurred = false;
    }

    ASSERT_NO_FATAL_FAILURE(WriteSingleInput(start_index));
    ++start_index;
    --number_of_inputs_to_write;

    while (number_of_inputs_to_write > 0) {
      Event event = kError;
      ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
      if (event == kConsumed) {
        ASSERT_NO_FATAL_FAILURE(WriteSingleInput(start_index));
        ++start_index;
        --number_of_inputs_to_write;
        continue;
      }
      if (event == kError) {
        ASSERT_TRUE(error_occurred);
        *error_occurred = true;
        return;
      }
      ASSERT_EQ(kOutput, event);
      bool is_eos = false;
      ASSERT_NO_FATAL_FAILURE(ReadFromDecoder(&is_eos));
      ASSERT_FALSE(is_eos);
    }
  }

  // The start_index will be updated to the new position.
  void WriteTimeLimitedInputs(size_t* start_index, int64_t time_limit) {
    SB_DCHECK(start_index);
    SB_DCHECK_LT(*start_index, dmp_reader_.number_of_audio_buffers());
    ASSERT_NO_FATAL_FAILURE(WriteSingleInput(*start_index));
    SB_DCHECK(last_input_buffer_);
    int64_t last_timestamp = last_input_buffer_->timestamp();
    int64_t first_timestamp = last_timestamp;
    ++(*start_index);

    while (last_timestamp - first_timestamp < time_limit &&
           *start_index < dmp_reader_.number_of_audio_buffers()) {
      Event event = kError;
      ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
      if (event == kConsumed) {
        ASSERT_NO_FATAL_FAILURE(WriteSingleInput(*start_index));
        SB_DCHECK(last_input_buffer_);
        last_timestamp = last_input_buffer_->timestamp();
        ++(*start_index);
        continue;
      }
      ASSERT_EQ(kOutput, event);
      bool is_eos = false;
      ASSERT_NO_FATAL_FAILURE(ReadFromDecoder(&is_eos));
      ASSERT_FALSE(is_eos);
    }
  }

  void DrainOutputs(bool* error_occurred = nullptr) {
    if (error_occurred) {
      *error_occurred = false;
    }

    for (;;) {
      Event event = kError;
      ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
      if (event == kError) {
        if (error_occurred) {
          *error_occurred = true;
        } else {
          FAIL();
        }
        return;
      }
      if (event == kConsumed) {
        continue;
      }
      ASSERT_EQ(kOutput, event);
      bool is_eos = false;
      ASSERT_NO_FATAL_FAILURE(ReadFromDecoder(&is_eos));
      if (is_eos) {
        break;
      }
    }
  }

  void ResetInternal() {
    can_accept_more_input_ = true;
    last_input_buffer_ = nullptr;
    decoded_audios_.clear();
    written_inputs_.clear();
    eos_written_ = false;
    decoded_audio_sample_rate_ = 0;
    first_output_received_ = false;
    {
      std::lock_guard lock(event_queue_mutex_);
      event_queue_.clear();
    }
  }

  void ResetDecoder() {
    audio_decoder_->Reset();
    ResetInternal();
  }

  void RecreateDecoder() {
    // TODO(b/332955827): Investigate the output difference after flush() codec.
    audio_decoder_.reset();
    audio_renderer_sink_.reset();
    ResetInternal();
    SetUp();
  }

  void WaitForDecodedAudio() {
    Event event;
    while (decoded_audios_.empty()) {
      ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
      if (event == kConsumed) {
        continue;
      }
      ASSERT_EQ(kOutput, event);
      bool is_eos = false;
      ASSERT_NO_FATAL_FAILURE(ReadFromDecoder(&is_eos));
      ASSERT_FALSE(is_eos);
    }
  }

  void WaitForDecoderInRunningState() {
    // Write multiple inputs to avoid reset codec too soon after start.
    const size_t kMaxNumberOfInputsToWrite = 50;
    size_t start_index = 0;
    size_t number_of_inputs_to_write = std::min(
        kMaxNumberOfInputsToWrite, dmp_reader_.number_of_audio_buffers());

    // Wait for at least one output available to ensure codec is in running
    // state.
    ASSERT_NO_FATAL_FAILURE(WriteSingleInput(start_index));
    ++start_index;
    --number_of_inputs_to_write;

    while (number_of_inputs_to_write > 0) {
      Event event = kError;
      ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
      if (event == kConsumed) {
        ASSERT_NO_FATAL_FAILURE(WriteSingleInput(start_index));
        ++start_index;
        --number_of_inputs_to_write;
        continue;
      }
      if (event == kError) {
        FAIL();
      }
      ASSERT_EQ(kOutput, event);
      bool is_eos = false;
      ASSERT_NO_FATAL_FAILURE(ReadFromDecoder(&is_eos));
      ASSERT_FALSE(is_eos);
      break;
    }
  }

  scoped_refptr<InputBuffer> GetTestAudioInputBuffer(size_t index) {
    auto input_buffer = GetAudioInputBuffer(&dmp_reader_, index);
    auto iter = invalid_inputs_.find(index);
    if (iter != invalid_inputs_.end()) {
      std::vector<uint8_t> content(input_buffer->size(), iter->second);
      // Replace the content with invalid data.
      input_buffer->SetDecryptedContent(std::move(content));
    }
    return input_buffer;
  }

  scoped_refptr<InputBuffer> GetTestAudioInputBuffer(
      size_t index,
      int64_t discarded_duration_from_front,
      int64_t discarded_duration_from_back) {
    auto input_buffer =
        GetAudioInputBuffer(&dmp_reader_, index, discarded_duration_from_front,
                            discarded_duration_from_back);
    auto iter = invalid_inputs_.find(index);
    if (iter != invalid_inputs_.end()) {
      std::vector<uint8_t> content(input_buffer->size(), iter->second);
      // Replace the content with invalid data.
      input_buffer->SetDecryptedContent(std::move(content));
    }
    return input_buffer;
  }

  void UseInvalidDataForInput(size_t index, uint8_t byte_to_fill) {
    invalid_inputs_[index] = byte_to_fill;
  }

  void WriteEndOfStream() {
    SB_DCHECK(!eos_written_);
    audio_decoder_->WriteEndOfStream();
    eos_written_ = true;
  }

  void AssertOutputFormatValid() {
    ASSERT_TRUE(decoded_audio_sample_type_ == kSbMediaAudioSampleTypeFloat32 ||
                decoded_audio_sample_type_ ==
                    kSbMediaAudioSampleTypeInt16Deprecated);
    ASSERT_TRUE(decoded_audio_sample_rate_ > 0 &&
                decoded_audio_sample_rate_ <= 480000);
  }

  void AssertExpectedAndOutputFramesMatch(int expected_output_frames) {
    if (using_stub_decoder_) {
      // The number of output frames is not applicable in the case of the
      // StubAudioDecoder, because it is not actually doing any decoding work.
      return;
    }

    ASSERT_LE(abs(expected_output_frames - GetTotalFrames(decoded_audios_)), 1);
  }

  std::mutex event_queue_mutex_;
  std::deque<Event> event_queue_;

  // Test parameter for the filename to load with the VideoDmpReader.
  const char* test_filename_;

  // Test parameter to configure whether the test is run with the
  // StubAudioDecoder, or the platform-specific AudioDecoderImpl
  bool using_stub_decoder_;

  JobQueue job_queue_;
  VideoDmpReader dmp_reader_;
  std::unique_ptr<AudioDecoder> audio_decoder_;
  std::unique_ptr<AudioRendererSink> audio_renderer_sink_;

  bool can_accept_more_input_ = true;
  scoped_refptr<InputBuffer> last_input_buffer_;
  std::deque<scoped_refptr<InputBuffer>> written_inputs_;
  std::vector<DecodedAudio> decoded_audios_;

  bool eos_written_ = false;

  std::map<size_t, uint8_t> invalid_inputs_;

  SbMediaAudioSampleType decoded_audio_sample_type_ =
      kSbMediaAudioSampleTypeInt16Deprecated;
  int decoded_audio_sample_rate_ = 0;

  bool first_output_received_ = false;
};

std::string GetAudioDecoderTestConfigName(
    ::testing::TestParamInfo<std::tuple<const char*, bool>> info) {
  std::string filename(std::get<0>(info.param));
  bool using_stub_decoder = std::get<1>(info.param);

  std::replace(filename.begin(), filename.end(), '.', '_');

  return filename + (using_stub_decoder ? "__stub" : "");
}

TEST_P(AudioDecoderTest, MultiDecoders) {
  const int kDecodersToCreate = 100;
  const int kMinimumNumberOfExtraDecodersRequired = 3;

  std::unique_ptr<AudioDecoder> audio_decoders[kDecodersToCreate];
  std::unique_ptr<AudioRendererSink> audio_renderer_sinks[kDecodersToCreate];

  for (int i = 0; i < kDecodersToCreate; ++i) {
    CreateComponents(dmp_reader_.audio_stream_info(), &audio_decoders[i],
                     &audio_renderer_sinks[i]);
    if (!audio_decoders[i]) {
      ASSERT_GE(i, kMinimumNumberOfExtraDecodersRequired);
    }
  }
}

TEST_P(AudioDecoderTest, SingleInput) {
  ASSERT_NO_FATAL_FAILURE(WriteSingleInput(0));
  WriteEndOfStream();

  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_FALSE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());
}

TEST_P(AudioDecoderTest, SingleInputHEAAC) {
  static const int kAacFrameSize = 1024;

  if (dmp_reader_.audio_codec() != kSbMediaAudioCodecAac) {
    return;
  }

  ASSERT_NO_FATAL_FAILURE(WriteSingleInput(0));
  WriteEndOfStream();

  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_FALSE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());

  int input_sample_rate =
      last_input_buffer_->audio_stream_info().samples_per_second;
  ASSERT_NE(0, decoded_audio_sample_rate_);
  int expected_output_frames =
      kAacFrameSize * decoded_audio_sample_rate_ / input_sample_rate;
  AssertExpectedAndOutputFramesMatch(expected_output_frames);
}

TEST_P(AudioDecoderTest, InvalidCodec) {
  auto invalid_codec = dmp_reader_.audio_codec() == kSbMediaAudioCodecAac
                           ? kSbMediaAudioCodecOpus
                           : kSbMediaAudioCodecAac;
  auto audio_stream_info = dmp_reader_.audio_stream_info();

  audio_stream_info.codec = invalid_codec;

  CreateComponents(audio_stream_info, &audio_decoder_, &audio_renderer_sink_);
  if (!audio_decoder_) {
    return;
  }

  WriteSingleInput(0);
  WriteEndOfStream();

  bool error_occurred = true;
  ASSERT_NO_FATAL_FAILURE(DrainOutputs(&error_occurred));
}

TEST_P(AudioDecoderTest, InvalidConfig) {
  auto original_audio_stream_info = dmp_reader_.audio_stream_info();

  for (size_t i = 0;
       i < original_audio_stream_info.audio_specific_config.size(); ++i) {
    auto audio_stream_info = original_audio_stream_info;

    audio_stream_info.audio_specific_config[i] =
        ~audio_stream_info.audio_specific_config[i];

    CreateComponents(audio_stream_info, &audio_decoder_, &audio_renderer_sink_);
    if (!audio_decoder_) {
      return;
    }
    WriteSingleInput(0);
    WriteEndOfStream();

    bool error_occurred = true;
    ASSERT_NO_FATAL_FAILURE(DrainOutputs(&error_occurred));

    ResetDecoder();
  }

  for (size_t i = 0;
       i < original_audio_stream_info.audio_specific_config.size(); ++i) {
    auto audio_stream_info = original_audio_stream_info;

    audio_stream_info.audio_specific_config.resize(i);

    CreateComponents(audio_stream_info, &audio_decoder_, &audio_renderer_sink_);
    if (!audio_decoder_) {
      return;
    }
    WriteSingleInput(0);
    WriteEndOfStream();

    bool error_occurred = true;
    ASSERT_NO_FATAL_FAILURE(DrainOutputs(&error_occurred));

    ResetDecoder();
  }
}

TEST_P(AudioDecoderTest, SingleInvalidInput) {
  UseInvalidDataForInput(0, 0xab);

  WriteSingleInput(0);
  WriteEndOfStream();

  bool error_occurred = true;
  ASSERT_NO_FATAL_FAILURE(DrainOutputs(&error_occurred));
}

TEST_P(AudioDecoderTest, MultipleValidInputsAfterInvalidInput) {
  const size_t kMaxNumberOfInputToWrite = 10;
  const size_t number_of_input_to_write =
      std::min(kMaxNumberOfInputToWrite, dmp_reader_.number_of_audio_buffers());

  UseInvalidDataForInput(0, 0xab);

  bool error_occurred = true;
  // Write first few frames.  The first one is invalid and the rest are valid.
  WriteMultipleInputs(0, number_of_input_to_write, &error_occurred);

  if (!error_occurred) {
    WriteEndOfStream();
    ASSERT_NO_FATAL_FAILURE(DrainOutputs(&error_occurred));
  }
}

TEST_P(AudioDecoderTest, MultipleInvalidInput) {
  const size_t kMaxNumberOfInputToWrite = 128;
  const size_t number_of_input_to_write =
      std::min(kMaxNumberOfInputToWrite, dmp_reader_.number_of_audio_buffers());
  // Replace the content of the first few input buffers with invalid data.
  // Every test instance loads its own copy of data so this won't affect other
  // tests.
  for (size_t i = 0; i < number_of_input_to_write; ++i) {
    UseInvalidDataForInput(i, static_cast<uint8_t>(0xab + i));
  }

  bool error_occurred = true;
  WriteMultipleInputs(0, number_of_input_to_write, &error_occurred);
  if (!error_occurred) {
    WriteEndOfStream();
    ASSERT_NO_FATAL_FAILURE(DrainOutputs(&error_occurred));
  }
}

TEST_P(AudioDecoderTest, EndOfStreamWithoutAnyInput) {
  WriteEndOfStream();

  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_TRUE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());
}

TEST_P(AudioDecoderTest, ResetBeforeInput) {
  ResetDecoder();

  ASSERT_NO_FATAL_FAILURE(WriteSingleInput(0));
  WriteEndOfStream();

  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_FALSE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());
}

TEST_P(AudioDecoderTest, MultipleInputs) {
  const size_t kMaxNumberOfInputsToWrite = 5;
  const size_t number_of_inputs_to_write = std::min(
      kMaxNumberOfInputsToWrite, dmp_reader_.number_of_audio_buffers());

  ASSERT_NO_FATAL_FAILURE(WriteMultipleInputs(0, number_of_inputs_to_write));

  WriteEndOfStream();

  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_FALSE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());
}

TEST_P(AudioDecoderTest, LimitedInput) {
  int64_t duration = 500'000;  // 0.5 seconds

  ASSERT_TRUE(decoded_audios_.empty());
  size_t start_index = 0;
  ASSERT_NO_FATAL_FAILURE(WriteTimeLimitedInputs(&start_index, duration));

  if (start_index >= dmp_reader_.number_of_audio_buffers()) {
    WriteEndOfStream();
  }

  // Wait for decoded audio.
  WaitForDecodedAudio();
}

TEST_P(AudioDecoderTest, ContinuedLimitedInput) {
  constexpr size_t kMaxAccessUnitsToDecode = 256;
  int64_t duration = 500'000;  // 0.5 seconds

  int64_t start = CurrentMonotonicTime();
  size_t start_index = 0;
  Event event;
  while (true) {
    ASSERT_NO_FATAL_FAILURE(WriteTimeLimitedInputs(&start_index, duration));
    if (start_index >= std::min<size_t>(dmp_reader_.number_of_audio_buffers(),
                                        kMaxAccessUnitsToDecode)) {
      break;
    }
    SB_DCHECK(last_input_buffer_);
    WaitForDecodedAudio();
    ASSERT_FALSE(decoded_audios_.empty());
    while ((last_input_buffer_->timestamp() -
            decoded_audios_.back().timestamp()) > duration ||
           !can_accept_more_input_) {
      ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
      if (event == kConsumed) {
        continue;
      }
      ASSERT_EQ(kOutput, event);
      bool is_eos = false;
      ASSERT_NO_FATAL_FAILURE(ReadFromDecoder(&is_eos));
      ASSERT_FALSE(is_eos);
    }
  }
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  int64_t elapsed = CurrentMonotonicTime() - start;
  SB_LOG(INFO) << "Decoding " << dmp_reader_.number_of_audio_buffers()
               << " access units of "
               << GetMediaAudioCodecName(dmp_reader_.audio_codec()) << " takes "
               << elapsed << " microseconds.";

  ASSERT_FALSE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());
}

TEST_P(AudioDecoderTest, PartialAudio) {
  const int max_number_of_input_to_write =
      std::min(7, static_cast<int>(dmp_reader_.number_of_audio_buffers()));

  for (int number_of_input_to_write = 1;
       number_of_input_to_write < max_number_of_input_to_write;
       ++number_of_input_to_write) {
    SB_LOG(INFO) << "Testing " << number_of_input_to_write
                 << " access units for partial audio.";
    RecreateDecoder();

    // Decode InputBuffers without partial audio and use the output as reference
    for (int i = 0; i < number_of_input_to_write; ++i) {
      ASSERT_NO_FATAL_FAILURE(WriteSingleInput(i));
      if (i == number_of_input_to_write - 1) {
        WriteEndOfStream();
        break;
      }

      for (;;) {
        Event event = kError;
        ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
        ASSERT_NE(event, kError);
        if (event == kConsumed) {
          break;
        }
        ASSERT_EQ(kOutput, event);
        bool is_eos = false;
        ASSERT_NO_FATAL_FAILURE(ReadFromDecoder(&is_eos));
        ASSERT_FALSE(is_eos);
      }
    }

    ASSERT_NO_FATAL_FAILURE(DrainOutputs());
    ASSERT_FALSE(decoded_audios_.empty());
    ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());

    DecodedAudio reference_decoded_audio =
        ConsolidateDecodedAudios(decoded_audios_);
    ASSERT_GT(reference_decoded_audio.frames(), 1);

    // Discard 1/4 of the duration from front, and back.  The resulting audio
    // will keep 1/2 of the frames in the middle. This has to be called before
    // `ResetDecoder()` as it resets `decoded_audio_sample_rate_`.
    ASSERT_GT(decoded_audio_sample_rate_, 0);
    auto frames_per_access_unit =
        reference_decoded_audio.frames() / number_of_input_to_write;
    int64_t duration_to_discard =
        AudioFramesToDuration(frames_per_access_unit,
                              decoded_audio_sample_rate_) /
        4;

    RecreateDecoder();

    for (int i = 0; i < number_of_input_to_write; ++i) {
      int64_t duration_to_discard_from_front = i == 0 ? duration_to_discard : 0;
      int64_t duration_to_discard_from_back =
          i == number_of_input_to_write - 1 ? duration_to_discard : 0;
      ASSERT_NO_FATAL_FAILURE(WriteSingleInput(
          i, duration_to_discard_from_front, duration_to_discard_from_back));

      if (i == number_of_input_to_write - 1) {
        WriteEndOfStream();
        break;
      }
      for (;;) {
        Event event = kError;
        ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
        ASSERT_NE(event, kError);
        if (event == kConsumed) {
          break;
        }
        ASSERT_EQ(kOutput, event);
        bool is_eos = false;
        ASSERT_NO_FATAL_FAILURE(ReadFromDecoder(&is_eos));
        ASSERT_FALSE(is_eos);
      }
    }

    ASSERT_NO_FATAL_FAILURE(DrainOutputs());
    ASSERT_FALSE(decoded_audios_.empty());
    ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());

    DecodedAudio partial_decoded_audio =
        ConsolidateDecodedAudios(decoded_audios_);

    ASSERT_EQ(reference_decoded_audio.sample_type(),
              partial_decoded_audio.sample_type());
    ASSERT_GT(reference_decoded_audio.frames(), partial_decoded_audio.frames());

    auto bytes_per_frame = reference_decoded_audio.size_in_bytes() /
                           reference_decoded_audio.frames();
    // |partial_decoded_audio| should contain exactly the same data as
    // |reference_decoded_audio|, begin from about 1/4 of an access unit.  We
    // search from (1/4 access unit - 1) in |reference_decoded_audio| to allow
    // for up to one frame of error during calculation.
    auto reference_search_begin =
        reference_decoded_audio.data() +
        (frames_per_access_unit / 4 - 1) * bytes_per_frame;
    auto reference_search_end = reference_decoded_audio.data() +
                                reference_decoded_audio.size_in_bytes();
    auto offset_index = std::search(
        reference_search_begin, reference_search_end,
        partial_decoded_audio.data(),
        partial_decoded_audio.data() + partial_decoded_audio.size_in_bytes());
    ASSERT_FALSE(offset_index == reference_search_end);
    auto offset_in_bytes = offset_index - reference_search_begin;
    auto offset_in_frames = offset_in_bytes / bytes_per_frame;

    constexpr int kEpsilonInFrames = 2;
    EXPECT_LE(offset_in_frames, kEpsilonInFrames);
    EXPECT_NEAR(reference_decoded_audio.frames() - frames_per_access_unit / 2,
                partial_decoded_audio.frames(), kEpsilonInFrames);
  }
}

TEST_P(AudioDecoderTest, ResetAfterEndOfStream) {
  ASSERT_NO_FATAL_FAILURE(WriteSingleInput(0));
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());

  ResetDecoder();
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_TRUE(decoded_audios_.empty());
}

TEST_P(AudioDecoderTest, ResetAfterEndOfStreamWithSingleInput) {
  ASSERT_NO_FATAL_FAILURE(WriteSingleInput(0));
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  int decoded_audio_frames_before_reset = GetTotalFrames(decoded_audios_);

  ResetDecoder();

  ASSERT_NO_FATAL_FAILURE(WriteSingleInput(0));
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_FALSE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());

  int decoded_audio_frames_after_reset = GetTotalFrames(decoded_audios_);
  ASSERT_EQ(decoded_audio_frames_before_reset,
            decoded_audio_frames_after_reset);
}

TEST_P(AudioDecoderTest, ResetAfterEndOfStreamWithMultipleInputs) {
  const size_t kMaxNumberOfInputsToWrite = 5;
  const size_t number_of_inputs_to_write = std::min(
      kMaxNumberOfInputsToWrite, dmp_reader_.number_of_audio_buffers());

  ASSERT_NO_FATAL_FAILURE(WriteMultipleInputs(0, number_of_inputs_to_write));
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  int decoded_audio_frames_before_reset = GetTotalFrames(decoded_audios_);

  ResetDecoder();

  ASSERT_NO_FATAL_FAILURE(WriteMultipleInputs(0, number_of_inputs_to_write));
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_FALSE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());

  int decoded_audio_frames_after_reset = GetTotalFrames(decoded_audios_);
  ASSERT_EQ(decoded_audio_frames_before_reset,
            decoded_audio_frames_after_reset);
}

TEST_P(AudioDecoderTest, ResetInRunningStateWithInput) {
  const size_t kMaxNumberOfInputsToWrite = 5;
  const size_t number_of_inputs_to_write = std::min(
      kMaxNumberOfInputsToWrite, dmp_reader_.number_of_audio_buffers());

  ASSERT_NO_FATAL_FAILURE(WriteMultipleInputs(0, number_of_inputs_to_write));
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  int decoded_audio_frames_before_reset = GetTotalFrames(decoded_audios_);
  ResetDecoder();

  WaitForDecoderInRunningState();

  ResetDecoder();
  ASSERT_NO_FATAL_FAILURE(WriteMultipleInputs(0, number_of_inputs_to_write));
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_FALSE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());

  int decoded_audio_frames_after_reset = GetTotalFrames(decoded_audios_);
  ASSERT_EQ(decoded_audio_frames_before_reset,
            decoded_audio_frames_after_reset);
}

TEST_P(AudioDecoderTest, ResetInRunningStateWithEndOfStream) {
  WaitForDecoderInRunningState();

  ResetDecoder();
  WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(DrainOutputs());
  ASSERT_TRUE(decoded_audios_.empty());
  ASSERT_NO_FATAL_FAILURE(AssertOutputFormatValid());
}

INSTANTIATE_TEST_SUITE_P(
    AudioDecoderTests,
    AudioDecoderTest,
    Combine(ValuesIn(GetSupportedAudioTestFiles(kIncludeHeaac,
                                                6,
                                                "audiopassthrough=false")),
            Bool()),
    GetAudioDecoderTestConfigName);

}  // namespace

}  // namespace starboard
