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

#include "starboard/android/shared/audio_track_audio_sink_type.h"

#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "starboard/android/shared/audio_output_manager.h"
#include "starboard/android/shared/media_capabilities_cache.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/common/check_op.h"
#include "starboard/common/scoped_timer.h"
#include "starboard/common/string.h"
#include "starboard/common/time.h"
#include "starboard/shared/starboard/features.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/filter/common.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {
namespace {

using jni_zero::AttachCurrentThread;

// Most Android audio HAL updates audio time for A/V synchronization on audio
// sync frames. For example, audio HAL may try to render when it gets an entire
// sync frame and then update audio time. Shorter duration of sync frame
// improves the accuracy of audio time, especially at the beginning of a
// playback, as otherwise the audio time during the initial update may be too
// large (non zero) and results in dropped video frames.
const int64_t kMaxDurationPerRequestInTunnelMode = 16'000;  // 16ms

const size_t kSilenceFramesPerAppend = 1024;

const int kMaxRequiredFramesLocal = 16 * 1024;
const int kMaxRequiredFramesRemote = 32 * 1024;
const int kMaxRequiredFrames = kMaxRequiredFramesRemote;
const int kRequiredFramesIncrement = 4 * 1024;
const int kMinStablePlayedFrames = 12 * 1024;

const int kSampleFrequency22050 = 22050;
const int kSampleFrequency48000 = 48000;

std::unique_ptr<AudioTrackAudioSinkType> audio_track_audio_sink_type_;

void* IncrementPointerByBytes(void* pointer, size_t offset) {
  return static_cast<uint8_t*>(pointer) + offset;
}

int GetMaxFramesPerRequestForTunnelMode(int sampling_frequency_hz) {
  auto max_frames =
      kMaxDurationPerRequestInTunnelMode * sampling_frequency_hz / 1'000'000LL;
  return (max_frames + 15) / 16 * 16;  // align to 16
}

bool HasRemoteAudioOutput() {
  // SbPlayerBridge::GetAudioConfigurations() reads up to 32 configurations. The
  // limit here is to avoid infinite loop and also match
  // SbPlayerBridge::GetAudioConfigurations().
  const int kMaxAudioConfigurations = 32;
  SbMediaAudioConfiguration configuration;
  int index = 0;
  while (index < kMaxAudioConfigurations &&
         MediaCapabilitiesCache::GetInstance()->GetAudioConfiguration(
             index, &configuration)) {
    switch (configuration.connector) {
      case kSbMediaAudioConnectorUnknown:
      case kSbMediaAudioConnectorAnalog:
      case kSbMediaAudioConnectorBuiltIn:
      case kSbMediaAudioConnectorHdmi:
      case kSbMediaAudioConnectorSpdif:
      case kSbMediaAudioConnectorUsb:
        break;
      case kSbMediaAudioConnectorBluetooth:
      case kSbMediaAudioConnectorRemoteWired:
      case kSbMediaAudioConnectorRemoteWireless:
      case kSbMediaAudioConnectorRemoteOther:
        return true;
    }
    index++;
  }
  return false;
}

}  // namespace

class AudioTrackAudioSink::AudioTrackOutThread : public Thread {
 public:
  explicit AudioTrackOutThread(AudioTrackAudioSink* sink)
      : Thread("audio_track_out",
               ThreadOptions().SetPriority(ThreadPriority::kRealTime)),
        sink_(sink) {}

  void Run() override { sink_->AudioThreadFunc(); }

 private:
  const raw_ptr<AudioTrackAudioSink> sink_;
};

std::unique_ptr<AudioTrackAudioSink> AudioTrackAudioSink::Create(
    Type* type,
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType sample_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frames_per_channel,
    int preferred_buffer_size,
    AudioTrackAudioSinkType::Callbacks callbacks,
    int64_t start_media_time,
    std::optional<int> tunnel_mode_audio_session_id,
    bool is_web_audio,
    bool allow_audio_writing_on_pause,
    bool pause_using_audio_track_state,
    void* context) {
  std::unique_ptr<AudioTrack> audio_track = AudioTrack::Create(
      kSbMediaAudioCodingTypePcm, sample_type, channels, sampling_frequency_hz,
      preferred_buffer_size, tunnel_mode_audio_session_id, is_web_audio);
  if (!audio_track) {
    return nullptr;
  }

  auto audio_sink = std::make_unique<AudioTrackAudioSink>(
      PassKey<AudioTrackAudioSink>(), type, channels, sampling_frequency_hz,
      sample_type, frame_buffers, frames_per_channel, preferred_buffer_size,
      callbacks, start_media_time, tunnel_mode_audio_session_id,
      allow_audio_writing_on_pause, pause_using_audio_track_state,
      std::move(audio_track), context);

  audio_sink->SpawnThread();
  return audio_sink;
}

// static
std::unique_ptr<AudioTrackAudioSink> AudioTrackAudioSink::CreateForTesting(
    Type* type,
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType sample_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frames_per_channel,
    int preferred_buffer_size,
    AudioTrackAudioSinkType::Callbacks callbacks,
    int64_t start_media_time,
    std::optional<int> tunnel_mode_audio_session_id,
    bool allow_audio_writing_on_pause,
    bool pause_using_audio_track_state,
    std::unique_ptr<AudioTrack> fake_audio_track,
    void* context) {
  if (!fake_audio_track) {
    return nullptr;
  }

  auto audio_sink = std::make_unique<AudioTrackAudioSink>(
      PassKey<AudioTrackAudioSink>(), type, channels, sampling_frequency_hz,
      sample_type, frame_buffers, frames_per_channel, preferred_buffer_size,
      callbacks, start_media_time, tunnel_mode_audio_session_id,
      allow_audio_writing_on_pause, pause_using_audio_track_state,
      std::move(fake_audio_track), context);

  audio_sink->SpawnThread();
  return audio_sink;
}

AudioTrackAudioSink::AudioTrackAudioSink(
    PassKey<AudioTrackAudioSink>,
    Type* type,
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType sample_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frames_per_channel,
    int preferred_buffer_size,
    AudioTrackAudioSinkType::Callbacks callbacks,
    int64_t start_media_time,
    std::optional<int> tunnel_mode_audio_session_id,
    bool allow_audio_writing_on_pause,
    bool pause_using_audio_track_state,
    std::unique_ptr<AudioTrack> audio_track,
    void* context)
    : type_(type),
      channels_(channels),
      sampling_frequency_hz_(sampling_frequency_hz),
      sample_type_(sample_type),
      frame_buffer_(frame_buffers[0]),
      frames_per_channel_(frames_per_channel),
      callbacks_(callbacks),
      start_time_(start_media_time),
      max_frames_per_request_(
          !tunnel_mode_audio_session_id
              ? AudioTrack::kMaxFramesPerRequest
              : GetMaxFramesPerRequestForTunnelMode(sampling_frequency_hz_)),
      context_(context),
      allow_audio_writing_on_pause_(allow_audio_writing_on_pause),
      pause_using_audio_track_state_(pause_using_audio_track_state),
      audio_track_(std::move(audio_track)),
      audio_out_thread_(std::make_unique<AudioTrackOutThread>(this)) {
  SB_DCHECK(callbacks_.update_source_status);
  SB_DCHECK(callbacks_.consume_frames);
  SB_DCHECK(frame_buffer_);
  SB_CHECK(audio_track_);

  SB_LOG(INFO) << "Creating audio sink starts at " << start_time_.load();
}

AudioTrackAudioSink::~AudioTrackAudioSink() {
  quit_ = true;

  audio_out_thread_->Join();
}

void AudioTrackAudioSink::SetPlaybackRate(double playback_rate) {
  SB_DCHECK_GE(playback_rate, 0.0);
  SB_DLOG(INFO) << "Set playback rate to " << playback_rate;

  {
    std::lock_guard lock(mutex_);
    playback_rate_ = playback_rate;
  }

  // AudioTrack doesn't support playback speed of 0.
  if (playback_rate > 0.0) {
    // AudioTrackBridge.setPlaybackRate() currently is only enabled for tunnel
    // mode. It will be no-op for non tunnel player.
    audio_track_->SetPlaybackRate(playback_rate);
  }
}

void AudioTrackAudioSink::SetVolume(double volume) {
  audio_track_->SetVolume(volume);
}

int AudioTrackAudioSink::GetUnderrunCount() {
  return audio_track_->GetUnderrunCount();
}

int AudioTrackAudioSink::GetStartThresholdInFrames() {
  return audio_track_->GetStartThresholdInFrames();
}

bool AudioTrackAudioSink::Flush() {
  flush_requested_ = true;
  return true;
}

// TODO: Break down the function into manageable pieces.
void AudioTrackAudioSink::AudioThreadFunc() {
  bool was_playing = false;
  int frames_in_audio_track = 0;
  AudioTrack::PlayState audio_track_play_state =
      AudioTrack::PlayState::kStopped;

  SB_LOG(INFO) << "AudioTrackAudioSink thread started.";

  int64_t accumulated_written_frames = 0;
  int64_t last_playback_head_event_at = -1;  // microseconds

  int64_t last_playback_head_position = 0;

#if !BUILDFLAG(IS_STARBOARD)
  bool release_frames_after_audio_starts = features::FeatureList::IsEnabled(
      features::kReleaseVideoFramesAfterAudioStarts);
#else
  bool release_frames_after_audio_starts = false;
#endif

  auto reset_and_flush = [&]() {
    audio_track_->PauseAndFlush();
    frames_in_audio_track = 0;
    // Set to -1 to ignore the first playback head position read after a
    // flush. This avoids using a stale value from before the reset, which
    // could lead to incorrect frame consumption calculations.
    last_playback_head_position = -1;
    last_playback_head_event_at = -1;
    is_flushed_ = true;
  };

  while (!quit_) {
    if (flush_requested_) {
      reset_and_flush();
      accumulated_written_frames = 0;
      flush_requested_ = false;
      continue;
    }

    int64_t playback_head_position = 0;
    int64_t frames_consumed_at = 0;
    AudioDeviceChange device_change =
        audio_track_->GetAndResetAudioDeviceChange();
    if (device_change == AudioDeviceChange::kRestartPlayer) {
      SB_LOG(INFO) << "Audio device changed, raising a capability changed "
                      "error to restart playback.";
      ReportError(true, "Audio device capability changed");
      break;
    }
    if (device_change == AudioDeviceChange::kResetAndContinue) {
      SB_LOG(INFO) << "Seamless audio device changed, resetting audio track "
                      "and sink buffer.";
      // On seamless route switches (e.g., 2-ch speaker <-> Bluetooth), flush
      // queued audio to reset timestamps and latency. Unconsumed frames in the
      // buffer will be re-fed on the next iteration when playback resumes.
      reset_and_flush();
      was_playing = false;
      continue;
    }

    if (pause_using_audio_track_state_) {
      // The audio data at the returned position by
      // |audio_track_->GetAudioTimestamp()| may either (1) already have been
      // presented, or (2) may have not yet been presented but is committed to
      // be presented. It is possible after |audio_track_->Pause()|, the audio
      // data is still committed to be presented as (2), which causes advancing
      // media time gap when player resumes and dropping video frames, so
      // player updates playback head positions when |audio_track_| doesn't
      // stop.
      audio_track_play_state = audio_track_->GetPlayState();

      if (audio_track_play_state == AudioTrack::PlayState::kPlaying) {
        is_flushed_ = false;
      }
    }

    // |pause_using_audio_track_state_| is enabled: update media time if
    // |audio_track_play_state| is PLAYSTATE_PLAYING or PLAYSTATE_PAUSED (unless
    // flushed). |pause_using_audio_track_state_| is disabled: by default, only
    // use |was_playing|.
    bool should_update_media_time = was_playing;
    if (pause_using_audio_track_state_) {
      should_update_media_time =
          (audio_track_play_state == AudioTrack::PlayState::kPlaying ||
           (audio_track_play_state == AudioTrack::PlayState::kPaused &&
            !is_flushed_));
    }
    if (should_update_media_time) {
      playback_head_position =
          audio_track_->GetAudioTimestamp(&frames_consumed_at);

      int frames_consumed = 0;
      if (last_playback_head_position == -1) {
        last_playback_head_position = playback_head_position;
      } else {
        frames_consumed =
            std::max(0, static_cast<int>(playback_head_position -
                                         last_playback_head_position));
      }

      int64_t now = CurrentMonotonicTime();

      if (last_playback_head_event_at == -1) {
        last_playback_head_event_at = now;
      }
      if (last_playback_head_position == playback_head_position) {
        int64_t elapsed = now - last_playback_head_event_at;
        if (elapsed > 5'000'000LL) {
          last_playback_head_event_at = now;
          SB_LOG(INFO) << "last playback head position is "
                       << last_playback_head_position
                       << " and it hasn't been updated for " << elapsed
                       << " microseconds.";
        }
      } else {
        last_playback_head_event_at = now;
      }

      last_playback_head_position = playback_head_position;
      frames_consumed = std::min(frames_consumed, frames_in_audio_track);

      if (frames_consumed != 0) {
        SB_DCHECK_GE(frames_consumed, 0);
        callbacks_.consume_frames(frames_consumed, frames_consumed_at,
                                  context_);
        frames_in_audio_track -= frames_consumed;
      }
    }

    int frames_in_buffer;
    int offset_in_frames;
    bool is_playing;
    bool is_eos_reached;
    callbacks_.update_source_status(&frames_in_buffer, &offset_in_frames,
                                    &is_playing, &is_eos_reached, context_);
    {
      std::lock_guard lock(mutex_);
      if (playback_rate_ == 0.0) {
        is_playing = false;
      }
    }

    // |pause_using_audio_track_state_| is enabled: pause/play AudioTrack
    // depending on PLAYSTATE_PLAYING or not.
    // |pause_using_audio_track_state_| is disabled: by default, only use
    // |was_playing|.
    bool is_currently_playing = was_playing;
    if (pause_using_audio_track_state_) {
      is_currently_playing =
          (audio_track_play_state == AudioTrack::PlayState::kPlaying);
    }
    if (is_currently_playing && !is_playing) {
      was_playing = false;
      ScopedTimer timer("Pause");
      audio_track_->Pause();
    } else if (!is_currently_playing && is_playing) {
      was_playing = true;
      last_playback_head_event_at = -1;
      ScopedTimer timer("Play");
      audio_track_->Play();
      if (release_frames_after_audio_starts) {
        // To promptly re-evaluate and update audio state, we restart the loop
        // after calling AudioTrack.play() on Android, as this operation often
        // takes hundreds of milliseconds.
        continue;
      }
    }

    // When |allow_audio_writing_on_pause| feature is enabled, continue writing
    // audio regardless of whether audio is playing.
    if ((!is_playing && !allow_audio_writing_on_pause_) ||
        frames_in_buffer == 0) {
      usleep(10'000);
      continue;
    }

    int start_position =
        (offset_in_frames + frames_in_audio_track) % frames_per_channel_;
    int expected_written_frames = 0;
    if (frames_per_channel_ > offset_in_frames + frames_in_audio_track) {
      expected_written_frames = std::min(
          frames_per_channel_ - (offset_in_frames + frames_in_audio_track),
          frames_in_buffer - frames_in_audio_track);
    } else {
      expected_written_frames = frames_in_buffer - frames_in_audio_track;
    }

    if (expected_written_frames < 0) {
      // TODO(cobalt, b/549849936): resolve negative expected_written_frames
      // issue.
      ReportError(false, "AudioTrack expected written frames is negative.");
      break;
    }

    expected_written_frames =
        std::min(expected_written_frames, max_frames_per_request_);

    if (expected_written_frames == 0) {
      // It is possible that all the frames in buffer are written to the
      // soundcard, but those are not being consumed. If eos is reached,
      // write silence to make sure audio track start working and avoid
      // underflow. Android audio track would not start working before
      // its buffer is fully filled once.
      if (is_eos_reached) {
        // Currently AudioDevice and AudioRenderer will write tail silence.
        // It should be reached only in tests. It's not ideal to allocate
        // a new silence buffer every time.
        const int silence_frames_per_append =
            std::min<int>(kSilenceFramesPerAppend, max_frames_per_request_);
        std::vector<uint8_t> silence_buffer(channels_ *
                                            GetBytesPerSample(sample_type_) *
                                            silence_frames_per_append);
        int64_t sync_time = start_time_.load() +
                            GetFramesDurationUs(accumulated_written_frames);
        // Not necessary to handle error of WriteData(), as the audio has
        // reached the end of stream.
        WriteData(silence_buffer.data(), silence_frames_per_append, sync_time);
      }

      usleep(10'000);
      continue;
    }
    SB_DCHECK_GT(expected_written_frames, 0);
    int64_t sync_time =
        start_time_.load() + GetFramesDurationUs(accumulated_written_frames);
    SB_DCHECK_LE(start_position + expected_written_frames, frames_per_channel_)
        << "start_position: " << start_position
        << ", expected_written_frames: " << expected_written_frames
        << ", frames_per_channel_: " << frames_per_channel_
        << ", frames_in_buffer: " << frames_in_buffer
        << ", frames_in_audio_track: " << frames_in_audio_track
        << ", offset_in_frames: " << offset_in_frames;

    int written_frames =
        WriteData(IncrementPointerByBytes(frame_buffer_,
                                          start_position * channels_ *
                                              GetBytesPerSample(sample_type_)),
                  expected_written_frames, sync_time);
    int64_t now = CurrentMonotonicTime();

    if (written_frames < 0) {
      if (written_frames == AudioTrack::kAudioTrackErrorDeadObject) {
        // There might be an audio device change, try to recreate the player.
        ReportError(true,
                    "Failed to write data and received dead object error.");
      } else {
        ReportError(false, FormatString("Failed to write data and received %d.",
                                        written_frames));
      }
      break;
    }
    frames_in_audio_track += written_frames;
    accumulated_written_frames += written_frames;

    bool written_fully = (written_frames == expected_written_frames);
    auto unplayed_frames_in_time =
        GetFramesDurationUs(frames_in_audio_track) - (now - frames_consumed_at);
    // As long as there is enough data in the buffer, run the loop in lower
    // frequency to avoid taking too much CPU.  Note that the threshold should
    // be big enough to account for the unstable playback head reported at the
    // beginning of the playback and during underrun.
    if (playback_head_position > 0 && unplayed_frames_in_time > 500'000) {
      usleep(40'000);
    } else if (!written_fully) {
      // Only sleep if the buffer is nearly full and the last write is partial.
      usleep(10'000);
    }
  }

  audio_track_->PauseAndFlush();
}

void AudioTrackAudioSink::SpawnThread() {
  audio_out_thread_->Start();
}

int AudioTrackAudioSink::WriteData(const void* buffer,
                                   int expected_written_frames,
                                   int64_t sync_time) {
  int samples_written = 0;
  if (sample_type_ == kSbMediaAudioSampleTypeFloat32) {
    samples_written = audio_track_->WriteSample(
        MakeSpan(static_cast<const float*>(buffer),
                 expected_written_frames * channels_));
  } else if (sample_type_ == kSbMediaAudioSampleTypeInt16Deprecated) {
    samples_written =
        audio_track_->WriteSample(MakeSpan(static_cast<const uint16_t*>(buffer),
                                           expected_written_frames * channels_),
                                  sync_time);
  } else {
    SB_NOTREACHED();
  }
  if (samples_written < 0) {
    // Error code returned as negative value, like kAudioTrackErrorDeadObject.
    return samples_written;
  }
  SB_DCHECK_EQ(samples_written % channels_, 0);
  return samples_written / channels_;
}

void AudioTrackAudioSink::ReportError(bool capability_changed,
                                      const std::string& error_message) {
  SB_LOG(INFO) << "AudioTrackAudioSink error: " << error_message;
  if (callbacks_.error) {
    callbacks_.error(capability_changed, error_message, context_);
  }
}

int64_t AudioTrackAudioSink::GetFramesDurationUs(int64_t frames) const {
  return frames * 1'000'000LL / sampling_frequency_hz_;
}

// static
int AudioTrackAudioSinkType::GetMinBufferSizeInFrames(
    int channels,
    SbMediaAudioSampleType sample_type,
    int sampling_frequency_hz) {
  SB_CHECK(audio_track_audio_sink_type_);
  JNIEnv* env = AttachCurrentThread();

  return std::max(
      AudioOutputManager::GetInstance()->GetMinBufferSizeInFrames(
          env, sample_type, channels, sampling_frequency_hz),
      audio_track_audio_sink_type_->GetMinBufferSizeInFramesInternal(
          channels, sample_type, sampling_frequency_hz));
}

AudioTrackAudioSinkType::AudioTrackAudioSinkType()
    : min_required_frames_tester_(kMaxRequiredFrames,
                                  kRequiredFramesIncrement,
                                  kMinStablePlayedFrames) {}

SbAudioSink AudioTrackAudioSinkType::Create(
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType audio_sample_type,
    SbMediaAudioFrameStorageType audio_frame_storage_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frames_per_channel,
    SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
    SbAudioSinkPrivate::ConsumeFramesFunc consume_frames_func,
    SbAudioSinkPrivate::ErrorFunc error_func,
    void* context) {
  return Create(channels, sampling_frequency_hz, audio_sample_type,
                audio_frame_storage_type, frame_buffers, frames_per_channel,
                {update_source_status_func, consume_frames_func, error_func},
                /*start_media_time=*/0,
                /*tunnel_mode_audio_session_id=*/std::nullopt,
                /*is_web_audio=*/true,
                /*allow_audio_writing_on_pause=*/false,
                /*pause_using_audio_track_state=*/false, context);
}

SbAudioSink AudioTrackAudioSinkType::Create(
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType audio_sample_type,
    SbMediaAudioFrameStorageType audio_frame_storage_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frames_per_channel,
    Callbacks callbacks,
    int64_t start_media_time,
    std::optional<int> tunnel_mode_audio_session_id,
    bool is_web_audio,
    bool allow_audio_writing_on_pause,
    bool pause_using_audio_track_state,
    void* context) {
  int min_required_frames = SbAudioSinkGetMinBufferSizeInFrames(
      channels, audio_sample_type, sampling_frequency_hz);
  SB_DCHECK_GE(frames_per_channel, min_required_frames);
  int preferred_buffer_size_in_bytes =
      min_required_frames * channels * GetBytesPerSample(audio_sample_type);

  auto audio_sink = AudioTrackAudioSink::Create(
      this, channels, sampling_frequency_hz, audio_sample_type, frame_buffers,
      frames_per_channel, preferred_buffer_size_in_bytes, callbacks,
      start_media_time, tunnel_mode_audio_session_id, is_web_audio,
      allow_audio_writing_on_pause, pause_using_audio_track_state, context);
  if (!audio_sink) {
    SB_DLOG(ERROR)
        << "AudioTrackAudioSinkType::Create failed to create audio track";
    return kSbAudioSinkInvalid;
  }
  return audio_sink.release();
}

void AudioTrackAudioSinkType::TestMinRequiredFrames() {
  auto onMinRequiredFramesForWebAudioReceived =
      [&](int number_of_channels, SbMediaAudioSampleType sample_type,
          int sample_rate, int min_required_frames) {
        bool has_remote_audio_output = HasRemoteAudioOutput();
        SB_LOG(INFO) << "Received min required frames "
                     << FormatWithDigitSeparators(min_required_frames)
                     << " for " << number_of_channels << " channels, "
                     << FormatWithDigitSeparators(sample_rate) << "hz, with "
                     << (has_remote_audio_output ? "remote" : "local")
                     << " audio output device.";
        std::lock_guard lock(min_required_frames_map_mutex_);
        has_remote_audio_output_ = has_remote_audio_output;
        min_required_frames_map_[sample_rate] =
            std::min(min_required_frames, has_remote_audio_output_
                                              ? kMaxRequiredFramesRemote
                                              : kMaxRequiredFramesLocal);
      };

  SbMediaAudioSampleType sample_type = kSbMediaAudioSampleTypeFloat32;
  if (!SbAudioSinkIsAudioSampleTypeSupported(sample_type)) {
    sample_type = kSbMediaAudioSampleTypeInt16Deprecated;
    SB_DCHECK(SbAudioSinkIsAudioSampleTypeSupported(sample_type));
  }
  JNIEnv* env = AttachCurrentThread();
  min_required_frames_tester_.AddTest(
      2, sample_type, kSampleFrequency48000,
      onMinRequiredFramesForWebAudioReceived,
      std::max(8 * 1024,
               AudioOutputManager::GetInstance()->GetMinBufferSizeInFrames(
                   env, sample_type, 2, kSampleFrequency48000)));
  min_required_frames_tester_.AddTest(
      2, sample_type, kSampleFrequency22050,
      onMinRequiredFramesForWebAudioReceived,
      std::max(4 * 1024,
               AudioOutputManager::GetInstance()->GetMinBufferSizeInFrames(
                   env, sample_type, 2, kSampleFrequency22050)));
  min_required_frames_tester_.Start();
}

int AudioTrackAudioSinkType::GetMinBufferSizeInFramesInternal(
    int channels,
    SbMediaAudioSampleType sample_type,
    int sampling_frequency_hz) {
  bool has_remote_audio_output = HasRemoteAudioOutput();
  std::lock_guard lock(min_required_frames_map_mutex_);
  if (has_remote_audio_output == has_remote_audio_output_) {
    // There's no audio output type change, we can use the numbers we got from
    // the tests at app launch.
    if (sampling_frequency_hz <= kSampleFrequency22050) {
      if (min_required_frames_map_.find(kSampleFrequency22050) !=
          min_required_frames_map_.end()) {
        return min_required_frames_map_[kSampleFrequency22050];
      }
    } else if (sampling_frequency_hz <= kSampleFrequency48000) {
      if (min_required_frames_map_.find(kSampleFrequency48000) !=
          min_required_frames_map_.end()) {
        return min_required_frames_map_[kSampleFrequency48000];
      }
    }
  }
  // We cannot find a matched result from our tests, or the audio output type
  // has changed. We use the default max required frames to avoid underruns.
  return has_remote_audio_output ? kMaxRequiredFramesRemote
                                 : kMaxRequiredFramesLocal;
}

}  // namespace starboard

namespace starboard {

// static
void SbAudioSinkImpl::PlatformInitialize() {
  static std::once_flag once_flag;
  std::call_once(once_flag, [] {
    SB_LOG(INFO) << "Creating AudioTrackAudioSinkType.";
    audio_track_audio_sink_type_ = std::make_unique<AudioTrackAudioSinkType>();
    SetPrimaryType(audio_track_audio_sink_type_.get());
    EnableFallbackToStub();
    audio_track_audio_sink_type_->TestMinRequiredFrames();
  });
}

// static
void SbAudioSinkImpl::PlatformTearDown() {
  SB_LOG(FATAL) << "Android application does not call PlatformTearDown().";
}

}  // namespace starboard
