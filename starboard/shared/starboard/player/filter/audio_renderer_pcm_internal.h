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

#ifndef STARBOARD_SHARED_STARBOARD_PLAYER_FILTER_AUDIO_RENDERER_PCM_INTERNAL_H_
#define STARBOARD_SHARED_STARBOARD_PLAYER_FILTER_AUDIO_RENDERER_PCM_INTERNAL_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "starboard/common/log.h"
#include "starboard/media.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/experimental_features.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/decoded_audio_internal.h"
#include "starboard/shared/starboard/player/filter/audio_decoder_internal.h"
#include "starboard/shared/starboard/player/filter/audio_frame_tracker.h"
#include "starboard/shared/starboard/player/filter/audio_renderer_internal.h"
#include "starboard/shared/starboard/player/filter/audio_renderer_sink.h"
#include "starboard/shared/starboard/player/filter/audio_resampler.h"
#include "starboard/shared/starboard/player/filter/audio_time_stretcher.h"
#include "starboard/shared/starboard/player/filter/media_time_provider.h"
#include "starboard/shared/starboard/player/input_buffer_internal.h"
#include "starboard/shared/starboard/player/job_queue.h"

// Uncomment the following statement to log the media time stats with deviation
// when GetCurrentMediaTime() is called.
// #define SB_LOG_MEDIA_TIME_STATS 1

namespace starboard {

const int kFramesInBufferBeginUnderflow = 1024;

// A class that sits in between the audio decoder, the audio sink and the
// pipeline to coordinate data transfer between these parties.  It also serves
// as the authority of playback time.
class AudioRendererPcm : public AudioRenderer,
                         public MediaTimeProvider,
                         private AudioRendererSink::RenderCallback,
                         private JobQueue::JobOwner {
 public:
  // |max_cached_frames| is a soft limit for the max audio frames this class can
  // cache so it can:
  // 1. Avoid using too much memory.
  // 2. Have the audio cache full to simulate the state that the renderer can no
  //    longer accept more data.
  // |min_frames_per_append| is the min number of frames that the audio renderer
  // tries to append to the sink buffer at once.
  AudioRendererPcm(JobQueue* job_queue,
                   std::unique_ptr<AudioDecoder> decoder,
                   std::unique_ptr<AudioRendererSink> audio_renderer_sink,
                   const AudioStreamInfo& audio_stream_info,
                   int max_cached_frames,
                   int min_frames_per_append,
                   const ExperimentalFeatures& experimental_features);
  ~AudioRendererPcm() override;

  void Initialize(const ErrorCB& error_cb,
                  const PrerolledCB& prerolled_cb,
                  const EndedCB& ended_cb) override;
  void WriteSamples(const InputBuffers& input_buffers) override;
  void WriteEndOfStream() override;

  void SetVolume(double volume) override;

  // TODO: Remove the eos state querying functions and their tests.
  bool IsEndOfStreamWritten() const override;
  bool IsEndOfStreamPlayed() const override;
  bool CanAcceptMoreData() const override;

  // MediaTimeProvider methods
  void Play() override;
  void Pause() override;
  void SetPlaybackRate(double playback_rate) override;
  void Seek(int64_t seek_to_time) override;
  int64_t GetCurrentMediaTime(bool* is_playing,
                              bool* is_eos_played,
                              bool* is_underflow,
                              double* playback_rate) override;
  int64_t GetAudioWriteHead() override;
  int64_t AdjustTimestampToAudioClock(int64_t timestamp) override;

 private:
  enum EOSState {
    kEOSNotReceived,
    kEOSWrittenToDecoder,
    kEOSDecoded,
    kEOSSentToSink
  };

  const int max_cached_frames_;
  const int min_frames_per_append_;
  const bool allow_audio_writing_on_pause_;
  // |buffered_frames_to_start_| would be initialized in OnFirstOutput().
  // Before it's initialized, set it to a large number.
  int buffered_frames_to_start_ = 48 * 1024;

  ErrorCB error_cb_;
  PrerolledCB prerolled_cb_;
  EndedCB ended_cb_;

  mutable std::mutex mutex_;

  bool paused_ = true;
  bool consume_frames_called_ = false;
  bool seeking_ = false;
  int64_t seeking_to_time_ = 0;  // microseconds
  int64_t last_media_time_ = 0;  // microseconds
  AudioFrameTracker audio_frame_tracker_;
  bool ended_cb_called_ = false;

  int64_t total_frames_sent_to_sink_ = 0;
  int64_t total_frames_consumed_by_sink_ = 0;
  int32_t frames_consumed_by_sink_since_last_get_current_time_;

  std::unique_ptr<AudioDecoder> decoder_;

  int64_t frames_consumed_set_at_;
  double playback_rate_ = 1.0;

  // AudioRendererSink methods
  void GetSourceStatus(int* frames_in_buffer,
                       int* offset_in_frames,
                       bool* is_playing,
                       bool* is_eos_reached) override;
  void ConsumeFrames(int frames_consumed, int64_t frames_consumed_at) override;
  void OnError(bool capability_changed,
               const std::string& error_message) override;

  void UpdateVariablesOnSinkThread_Locked(
      int64_t system_time_on_consume_frames);

  void OnFirstOutput(const SbMediaAudioSampleType decoded_sample_type,
                     const int decoded_sample_rate);
  bool IsEndOfStreamPlayed_Locked() const;

  void OnDecoderConsumed();
  void OnDecoderOutput();
  void ProcessAudioData();
  void FillResamplerAndTimeStretcher();
  bool AppendAudioToFrameBuffer(bool* is_frame_buffer_full);

  EOSState eos_state_ = kEOSNotReceived;
  const int channels_;
  const SbMediaAudioSampleType sink_sample_type_;
  const int bytes_per_frame_;

  std::unique_ptr<AudioResampler> resampler_;
  std::optional<int> decoder_sample_rate_;
  AudioTimeStretcher time_stretcher_;

  std::vector<uint8_t> frame_buffer_;
  uint8_t* frame_buffers_[1];

  int32_t pending_decoder_outputs_ = 0;

  bool can_accept_more_data_ = true;
  JobQueue::JobToken process_audio_data_job_token_ =
      JobQueue::JobToken::kUnscheduled;
  std::function<void()> process_audio_data_job_;

  // Our owner will attempt to seek to time 0 when playback begins.  In
  // general, seeking could require a full reset of the underlying decoder on
  // some platforms, so we make an effort to improve playback startup
  // performance by keeping track of whether we already have a fresh decoder,
  // and can thus avoid doing a full reset.
  bool first_input_written_ = false;

  const std::unique_ptr<AudioRendererSink> audio_renderer_sink_;
  bool is_eos_reached_on_sink_thread_ = false;
  bool is_playing_on_sink_thread_ = false;
  int64_t frames_in_buffer_on_sink_thread_ = 0;
  int64_t offset_in_frames_on_sink_thread_ = 0;
  int64_t frames_consumed_on_sink_thread_ = 0;
  int64_t frames_consumed_set_at_on_sink_thread_ = 0;  // microseconds
  int64_t silence_frames_written_after_eos_on_sink_thread_ = 0;

#if SB_LOG_MEDIA_TIME_STATS
  int64_t system_and_media_time_offset_ = -1;                // microseconds
  int64_t min_drift_ = std::numeric_limits<int64_t>::max();  // microseconds
  int64_t max_drift_ = 0;                                    // microseconds
  int64_t total_frames_consumed_ = 0;
#endif  // SB_LOG_MEDIA_TIME_STATS

  // Set to true when there are fewer than |kFramesInBufferBeginUnderflow|
  // frames in buffer. Set to false when the queue is full or EOS.
  bool underflow_ = false;

#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  static const int32_t kMaxSinkCallbacksBetweenCheck = 1024;
  static const int64_t kCheckAudioSinkStatusInterval = 1'000'000;  // 1 second
  void CheckAudioSinkStatus();

  std::atomic<int32_t> sink_callbacks_since_last_check_{0};
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
};

}  // namespace starboard

#endif  // STARBOARD_SHARED_STARBOARD_PLAYER_FILTER_AUDIO_RENDERER_PCM_INTERNAL_H_
