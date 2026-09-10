// Copyright 2021 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_ANDROID_SHARED_AUDIO_RENDERER_PASSTHROUGH_H_
#define STARBOARD_ANDROID_SHARED_AUDIO_RENDERER_PASSTHROUGH_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>

#include "starboard/android/shared/audio_track_bridge.h"
#include "starboard/android/shared/drm_system.h"
#include "starboard/common/pass_key.h"
#include "starboard/common/ref_counted.h"
#include "starboard/common/result.h"
#include "starboard/drm.h"
#include "starboard/media.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/decoded_audio_internal.h"
#include "starboard/shared/starboard/player/filter/audio_decoder_internal.h"
#include "starboard/shared/starboard/player/filter/audio_renderer_internal.h"
#include "starboard/shared/starboard/player/filter/common.h"
#include "starboard/shared/starboard/player/filter/media_time_provider.h"
#include "starboard/shared/starboard/player/input_buffer_internal.h"
#include "starboard/shared/starboard/player/job_queue.h"
#include "starboard/shared/starboard/player/job_thread.h"

namespace starboard {

// TODO: The audio receiver often requires some warm up time to switch the
//       output to eac3.  Consider pushing some silence at the very beginning so
//       the sound at the very beginning won't get lost during the switching.
class AudioRendererPassthrough : public AudioRenderer,
                                 public MediaTimeProvider,
                                 private JobQueue::JobOwner {
 public:
  static NonNullResult<std::unique_ptr<AudioRendererPassthrough>> Create(
      JobQueue* job_queue,
      const AudioStreamInfo& audio_stream_info,
      SbDrmSystem drm_system,
      bool enable_flush_during_seek);

  AudioRendererPassthrough(PassKey<AudioRendererPassthrough>,
                           JobQueue* job_queue,
                           const AudioStreamInfo& audio_stream_info,
                           std::unique_ptr<AudioDecoder> decoder);
  ~AudioRendererPassthrough() override;

  // AudioRenderer methods
  void Initialize(const ErrorCB& error_cb,
                  const PrerolledCB& prerolled_cb,
                  const EndedCB& ended_cb) override;
  void WriteSamples(const InputBuffers& input_buffers) override;
  void WriteEndOfStream() override;

  void SetVolume(double volume) override;

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
  int64_t GetAudioWriteHead() override { return 0; }
  int64_t AdjustTimestampToAudioClock(int64_t timestamp) override {
    return timestamp;
  }

 private:
  struct AudioTrackState {
    double volume = 1.0;
    bool paused = true;
    double playback_rate = 1.0;

    bool playing() const { return !paused && playback_rate > 0.0; }
  };

  void CreateAudioTrackAndStartProcessing();
  void FlushAudioTrackAndStopProcessing(int64_t seek_to_time);
  void UpdateStatusAndWriteData(const AudioTrackState previous_state);
  void ReportCapabilityChanged();
  void OnDecoderConsumed();
  void OnDecoderOutput();

  // The following two variables are set in the ctor.
  const AudioStreamInfo audio_stream_info_;
  // The AudioDecoder is used as a decryptor when the stream is encrypted.
  // TODO: Revisit to encapsulate the AudioDecoder as a SbDrmSystemPrivate
  //       instead.  This would need to turn SbDrmSystemPrivate::Decrypt() into
  //       asynchronous, which comes with extra risks.
  const std::unique_ptr<AudioDecoder> decoder_;

  // The following three variables are set in Initialize().
  ErrorCB error_cb_;
  PrerolledCB prerolled_cb_;
  EndedCB ended_cb_;

  int frames_per_input_buffer_ = 0;  // Set once before all uses.
  std::atomic_bool can_accept_more_data_{true};
  std::atomic_bool prerolled_{false};
  std::atomic_bool end_of_stream_played_{false};

  bool end_of_stream_written_ = false;  // Only accessed on PlayerWorker thread.

  mutable std::mutex mutex_;
  bool stop_called_ = false;
  int64_t total_frames_written_ = 0;
  int64_t playback_head_position_when_stopped_ = 0;
  int64_t stopped_at_ = 0;              // microseconds
  int64_t seek_to_time_ = 0;            // microseconds
  int64_t first_audio_timestamp_ = -1;  // microseconds
  double volume_ = 1.0;
  bool paused_ = true;
  double playback_rate_ = 1.0;
  std::queue<DecodedAudio> decoded_audios_;

  // The following variable group is only accessed on |audio_track_thread_|, or
  // after |audio_track_thread_| is destroyed (in Seek()).
  std::optional<DecodedAudio> decoded_audio_writing_in_progress_;
  int decoded_audio_writing_offset_ = 0;
  JobQueue::JobToken update_status_and_write_data_token_ =
      JobQueue::JobToken::kUnscheduled;
  int64_t total_frames_written_on_audio_track_thread_ = 0;

  std::atomic_bool audio_track_paused_{true};

  // |audio_track_thread_| must be declared after |audio_track_bridge_| to
  // ensure the thread completes all tasks before |audio_track_bridge_| is
  // invalidated.
  std::unique_ptr<AudioTrackBridge> audio_track_bridge_;
  std::unique_ptr<JobThread> audio_track_thread_;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_AUDIO_RENDERER_PASSTHROUGH_H_
