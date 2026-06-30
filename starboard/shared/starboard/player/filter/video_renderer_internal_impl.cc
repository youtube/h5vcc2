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

#include "starboard/shared/starboard/player/filter/video_renderer_internal_impl.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "starboard/common/check_op.h"
#include "starboard/common/string.h"
#include "starboard/common/time.h"
#include "starboard/shared/starboard/experimental_features.h"

namespace starboard {

namespace {

using std::placeholders::_1;
using std::placeholders::_2;

const int64_t kSeekTimeoutRetryInterval = 25'000;  // 25ms

}  // namespace

VideoRendererImpl::VideoRendererImpl(
    JobQueue* job_queue,
    std::unique_ptr<VideoDecoder> decoder,
    MediaTimeProvider* media_time_provider,
    std::unique_ptr<VideoRenderAlgorithm> algorithm,
    scoped_refptr<VideoRendererSink> sink,
    const ExperimentalFeatures& experimental_features)
    : JobOwner(job_queue),
      media_time_provider_(media_time_provider),
      algorithm_(std::move(algorithm)),
      sink_(sink),
      decoder_(std::move(decoder)),
      experimental_features_(experimental_features) {
  SB_CHECK(decoder_);
  SB_CHECK(algorithm_);
  SB_DCHECK_GT(decoder_->GetMaxNumberOfCachedFrames(), 1U);
  SB_DLOG_IF(WARNING, decoder_->GetMaxNumberOfCachedFrames() < 4)
      << "VideoDecoder::GetMaxNumberOfCachedFrames() returns "
      << decoder_->GetMaxNumberOfCachedFrames() << ", which is less than 4."
      << " Playback performance may not be ideal.";

#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  last_buffering_state_update_ = CurrentMonotonicTime();
  last_output_ = last_buffering_state_update_;
  last_can_accept_more_data = last_buffering_state_update_;
  Schedule(std::bind(&VideoRendererImpl::CheckBufferingState, this),
           kCheckBufferingStateInterval);
  time_of_last_lag_warning_ = CurrentMonotonicTime() - kMinLagWarningInterval;
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  auto min_input_buffers =
      experimental_features_.Get(kMediaVideoRendererMinInputBuffers);
  auto min_decoded_frames =
      experimental_features_.Get(kMediaVideoRendererMinDecodedFrames);
  if (min_input_buffers && min_decoded_frames) {
    SB_LOG(INFO) << "VideoRendererImpl is created: preroll_params="
                 << "{min_input_buffers=" << *min_input_buffers
                 << ", min_decoded_frames=" << *min_decoded_frames << "}";
  } else {
    SB_LOG(INFO) << "VideoRendererImpl is created: using default preroll logic";
  }
}

VideoRendererImpl::~VideoRendererImpl() {
  SB_CHECK(BelongsToCurrentThread());

  sink_ = NULL;

  // Be sure to release anything created by the decoder_ before releasing the
  // decoder_ itself.
  if (first_input_written_) {
    decoder_->ResetForTeardown();
  }

  // Now both the decoder thread and the sink thread should have been shutdown.
  decoder_frames_.clear();
  sink_frames_.clear();
  number_of_frames_.store(0);
  pending_input_buffers_.clear();

  decoder_.reset();
}

void VideoRendererImpl::Initialize(const ErrorCB& error_cb,
                                   const PrerolledCB& prerolled_cb,
                                   const EndedCB& ended_cb) {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(prerolled_cb);
  SB_DCHECK(ended_cb);
  SB_DCHECK(!prerolled_cb_);
  SB_DCHECK(!ended_cb_);

  prerolled_cb_ = prerolled_cb;
  ended_cb_ = ended_cb;

  decoder_->Initialize(
      std::bind(&VideoRendererImpl::OnDecoderStatus, this, _1, _2), error_cb);
  if (sink_) {
    sink_->SetRenderCB(std::bind(&VideoRendererImpl::Render, this, _1));
  }
}

void VideoRendererImpl::WriteSamples(const InputBuffers& input_buffers) {
  SB_CHECK(BelongsToCurrentThread());
  SB_CHECK(pending_input_buffers_.empty());
  SB_DCHECK(!input_buffers.empty());
  for (const auto& input_buffer : input_buffers) {
    SB_DCHECK(input_buffer);
  }

#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  buffering_state_ = kWaitForConsumption;
  last_buffering_state_update_ = CurrentMonotonicTime();
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK

  if (end_of_stream_written_.load()) {
    SB_LOG(ERROR) << "Appending video samples from "
                  << input_buffers.front()->timestamp() << " to "
                  << input_buffers.back()->timestamp() << " after EOS reached.";
    return;
  }

  if (!first_input_written_) {
    first_input_written_ = true;
#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
    first_input_written_at_ = CurrentMonotonicTime();
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  }

  need_more_input_.store(false);

  auto write_to_decoder = [&](const InputBuffers& buffers) {
    input_buffers_sent_.fetch_add(static_cast<int32_t>(buffers.size()));
    decoder_->WriteInputBuffers(buffers);
  };

  if (!experimental_features_.GetBool(kMediaEnableVideoRendererVspAdjustment) ||
      (!is_vsp_adjustment_enabled_ && seeking_)) {
    write_to_decoder(input_buffers);
    return;
  }

  // When the |enable_video_renderer_vsp_adjustment| feature is enabled and
  // after playback is prerolled, we hold video frames until the audio head
  // reaches their presentation time to ensure a smooth playback rate change.
  int64_t audio_write_head = media_time_provider_->GetAudioWriteHead();
  auto first_input_after_audio_head_it = input_buffers.end();
  for (auto it = input_buffers.begin(); it != input_buffers.end(); ++it) {
    if ((*it)->timestamp() > audio_write_head) {
      first_input_after_audio_head_it = it;
      break;
    } else if (is_vsp_adjustment_enabled_) {
      // Adjust timestamps of frames that will be sent to the decoder.
      int64_t adjusted_timestamp =
          media_time_provider_->AdjustTimestampToAudioClock((*it)->timestamp());
      SB_CHECK_GE(adjusted_timestamp, 0)
          << "Failed to adjust frame at " << (*it)->timestamp();
      (*it)->SetTimestamp(adjusted_timestamp);
    }
  }

  if (first_input_after_audio_head_it == input_buffers.end()) {
    write_to_decoder(input_buffers);
    return;
  }

  // Split the input buffers into those ready to be written and those that
  // need to be delayed.
  InputBuffers buffers_to_write(input_buffers.begin(),
                                first_input_after_audio_head_it);
  pending_input_buffers_.insert(pending_input_buffers_.end(),
                                first_input_after_audio_head_it,
                                input_buffers.end());
  if (!buffers_to_write.empty()) {
    write_to_decoder(buffers_to_write);
  }
  if (!pending_input_buffers_.empty()) {
    // Check again in 5ms to see if more frames can be sent to the decoder.
    const int64_t kWritePendingInputsDelay = 5'000;
    Schedule(std::bind(&VideoRendererImpl::WritePendingInputs, this),
             kWritePendingInputsDelay);
  }
}

void VideoRendererImpl::WriteEndOfStream() {
  SB_CHECK(BelongsToCurrentThread());

  if (end_of_stream_written_.load() || pending_end_of_stream_) {
    SB_LOG(WARNING) << "Try to write EOS after EOS is reached";
    return;
  }

  if (!pending_input_buffers_.empty()) {
    // If a playback only have a few video samples, WriteEndOfStream() might
    // be called very early, even before audio is prerolled. When there're
    // pending inputs, we need to write eos after all inputs are written into
    // the decoder.
    pending_end_of_stream_ = true;
    return;
  }

  end_of_stream_written_.store(true);
  if (!first_input_written_) {
    first_input_written_ = true;
  }
  decoder_->WriteEndOfStream();
}

void VideoRendererImpl::Seek(int64_t seek_to_time) {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK_GE(seek_to_time, 0);

  if (first_input_written_) {
    decoder_->Reset();
    first_input_written_ = false;
  }

  // After decoder_->Reset(), OnDecoderStatus() won't be called before another
  // WriteSample().  So it is safe to modify |seeking_to_time_| here.
  seeking_to_time_ = std::max<int64_t>(seek_to_time, 0);
  seeking_.store(true);
  input_buffers_sent_.store(0);
  end_of_stream_written_.store(false);
  end_of_stream_decoded_.store(false);
  ended_cb_called_.store(false);
  need_more_input_.store(true);

  CancelPendingJobs();
  pending_input_buffers_.clear();
  pending_end_of_stream_ = false;

  auto preroll_timeout = decoder_->GetPrerollTimeout();
  if (preroll_timeout != std::numeric_limits<int64_t>::max()) {
    Schedule(std::bind(&VideoRendererImpl::OnSeekTimeout, this),
             preroll_timeout);
  }

  std::scoped_lock lock(decoder_frames_mutex_, sink_frames_mutex_);
  decoder_frames_.clear();
  sink_frames_.clear();
  number_of_frames_.store(0);

#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  buffering_state_ = kWaitForBuffer;
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK

  // This is also guarded by |sink_frames_mutex_|.
  algorithm_->Seek(seek_to_time);
}

void VideoRendererImpl::SetPlaybackRate(double playback_rate) {
  SB_DCHECK(BelongsToCurrentThread());
  SB_DCHECK_GE(playback_rate, 0);

  if (experimental_features_.GetBool(kMediaEnableVideoRendererVspAdjustment) &&
      playback_rate != 0.0 && playback_rate != 1.0) {
    // Start vsp adjustment of video input presentation timepstamps in video
    // renderer after we see a none 0.0x or 1.0x playback rate.
    is_vsp_adjustment_enabled_ = true;
  }

  if (sink_) {
    if (is_vsp_adjustment_enabled_) {
      // Force video sink playback rate to be 0.0x or 1.0x when video renderer
      // vsp adjustment is enabled.
      sink_->SetPlaybackRate(playback_rate > 0 ? 1.0 : 0.0);
    } else {
      sink_->SetPlaybackRate(playback_rate);
    }
  }
}

bool VideoRendererImpl::CanAcceptMoreData() const {
  SB_CHECK(BelongsToCurrentThread());
  bool can_accept_more_data =
      number_of_frames_.load() <
          static_cast<int32_t>(decoder_->GetMaxNumberOfCachedFrames()) &&
      !end_of_stream_written_.load() && need_more_input_.load() &&
      pending_input_buffers_.empty() && !pending_end_of_stream_;
#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  if (can_accept_more_data) {
    last_can_accept_more_data = CurrentMonotonicTime();
  }
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  return can_accept_more_data;
}

void VideoRendererImpl::SetBounds(int z_index, const Rect& rect) {
  if (sink_) {
    sink_->SetBounds(z_index, rect);
  }
}

SbDecodeTarget VideoRendererImpl::GetCurrentDecodeTarget() {
  // FilterBasedPlayerWorkerHandler::Stop() ensures that this function won't be
  // called right before VideoRenderer dtor is called and |decoder_| is set to
  // NULL inside the dtor.
  SB_CHECK(decoder_);

#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  auto start = CurrentMonotonicTime();
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK

  // TODO: Ensure that |sink_| is NULL when decode target is used across all
  // platforms.
  if (!sink_) {
    Render(VideoRendererSink::DrawFrameCB());
  }

  auto decode_target = decoder_->GetCurrentDecodeTarget();

#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  auto end = CurrentMonotonicTime();
  if (end - start > kMaxGetCurrentDecodeTargetDuration) {
    SB_LOG(WARNING) << "VideoRendererImpl::GetCurrentDecodeTarget() takes "
                    << end - start << " microseconds.";
  }
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  return decode_target;
}

void VideoRendererImpl::OnDecoderStatus(
    VideoDecoder::Status status,
    const scoped_refptr<VideoFrame>& frame) {
  if (status == VideoDecoder::kReleaseAllFrames) {
    std::scoped_lock scoped_lock(decoder_frames_mutex_, sink_frames_mutex_);
    decoder_frames_.clear();
    sink_frames_.clear();
    number_of_frames_.store(0);
    return;
  }

  if (frame) {
#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
    last_output_ = CurrentMonotonicTime();
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK

    SB_DCHECK(first_input_written_);

    bool frame_too_early = false;
    if (frame->is_end_of_stream()) {
      if (seeking_.exchange(false)) {
        Schedule(prerolled_cb_);
      }
      end_of_stream_decoded_.store(true);
    } else if (seeking_.load() && frame->timestamp() < seeking_to_time_) {
      frame_too_early = true;
    }

    if (!frame_too_early) {
#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
      if (!frame->is_end_of_stream()) {
        CheckForFrameLag(frame->timestamp());
      }
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
      std::lock_guard scoped_lock(decoder_frames_mutex_);
      if (decoder_frames_.empty() || frame->is_end_of_stream() ||
          frame->timestamp() > decoder_frames_.back()->timestamp()) {
        decoder_frames_.push_back(frame);
        ++number_of_frames_;
      }
    }

    bool preroll_completed = false;
    auto min_input_buffers =
        experimental_features_.Get(kMediaVideoRendererMinInputBuffers);
    auto min_decoded_frames =
        experimental_features_.Get(kMediaVideoRendererMinDecodedFrames);
    if (min_input_buffers && min_decoded_frames) {
      preroll_completed = input_buffers_sent_.load() >= *min_input_buffers &&
                          number_of_frames_.load() >= *min_decoded_frames;
    } else {
      preroll_completed =
          number_of_frames_.load() >=
          static_cast<int32_t>(decoder_->GetPrerollFrameCount());
    }

    bool should_call_preroll_cb;
    if (experimental_features_.GetBool(kMediaEnableTrivialOptimizations)) {
      // Avoid unconditional cache thrashing.
      should_call_preroll_cb =
          preroll_completed && seeking_.load() && seeking_.exchange(false);
    } else {
      // The previous logic.
      should_call_preroll_cb = preroll_completed && seeking_.exchange(false);
    }

    if (should_call_preroll_cb) {
#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
      SB_LOG(INFO) << "Video preroll complete: elapsed(msec)="
                   << FormatWithDigitSeparators(
                          (CurrentMonotonicTime() - first_input_written_at_) /
                          1'000)
                   << ", input_buffers_sent=" << input_buffers_sent_.load()
                   << ", decoded_frames=" << number_of_frames_.load();
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
      Schedule(prerolled_cb_);
    }
  }

#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  if (status == VideoDecoder::kNeedMoreInput) {
    buffering_state_ = kWaitForBuffer;
    last_buffering_state_update_ = CurrentMonotonicTime();
  }
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK

  need_more_input_.store(status == VideoDecoder::kNeedMoreInput);
}

void VideoRendererImpl::Render(VideoRendererSink::DrawFrameCB draw_frame_cb) {
#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  auto now = CurrentMonotonicTime();
  if (time_of_last_render_call_ != -1) {
    auto time_since_last_call = now - time_of_last_render_call_;
    if (time_since_last_call > kMaxRenderIntervalBeforeWarning) {
      SB_LOG(WARNING) << "Render() hasn't been called for "
                      << time_since_last_call << " microseconds.";
    }
  }
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  {
    std::lock_guard scoped_lock_decoder_frames(decoder_frames_mutex_);
    sink_frames_mutex_.lock();
    if (experimental_features_.GetBool(kMediaEnableTrivialOptimizations)) {
      auto it = decoder_frames_.begin();
      while (it != decoder_frames_.end()) {
        auto next = std::next(it);
        if (sink_frames_.empty()) {
          sink_frames_.splice(sink_frames_.end(), decoder_frames_, it);
        } else if (sink_frames_.back()->is_end_of_stream()) {
          // If the sink reached end of stream, no more frames can be appended.
          // Remaining frames in decoder_frames_ will be cleared below.
          break;
        } else if ((*it)->is_end_of_stream() ||
                   (*it)->timestamp() > sink_frames_.back()->timestamp()) {
          sink_frames_.splice(sink_frames_.end(), decoder_frames_, it);
        }
        it = next;
      }
    } else {
      for (auto decoder_frame : decoder_frames_) {
        if (sink_frames_.empty()) {
          sink_frames_.push_back(decoder_frame);
          continue;
        }
        if (sink_frames_.back()->is_end_of_stream()) {
          continue;
        }
        if (decoder_frame->is_end_of_stream() ||
            decoder_frame->timestamp() > sink_frames_.back()->timestamp()) {
          sink_frames_.push_back(decoder_frame);
        }
      }
    }
    decoder_frames_.clear();
  }
  size_t number_of_sink_frames = sink_frames_.size();
  algorithm_->Render(media_time_provider_, &sink_frames_, draw_frame_cb);
  number_of_frames_.fetch_sub(
      static_cast<int32_t>(number_of_sink_frames - sink_frames_.size()));
  if (number_of_frames_.load() <= 1 && end_of_stream_decoded_.load() &&
      !ended_cb_called_.load()) {
    ended_cb_called_.store(true);
    Schedule(ended_cb_);
  }
  sink_frames_mutex_.unlock();

#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
  // Update this at last to ensure that the delay of Render() call isn't caused
  // by the slowness of Render() itself.
  time_of_last_render_call_ = CurrentMonotonicTime();
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
}

void VideoRendererImpl::OnSeekTimeout() {
  SB_CHECK(BelongsToCurrentThread());
  if (number_of_frames_.load() > 0) {
    if (seeking_.exchange(false)) {
#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
      SB_LOG(INFO) << "Video preroll timeout:"
                   << " input_buffers_sent=" << input_buffers_sent_.load()
                   << ", decoded_frames=" << number_of_frames_.load();
#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK
      Schedule(prerolled_cb_);
    }
  } else if (seeking_.load()) {
    Schedule(std::bind(&VideoRendererImpl::OnSeekTimeout, this),
             kSeekTimeoutRetryInterval);
  }
}

void VideoRendererImpl::WritePendingInputs() {
  SB_CHECK(BelongsToCurrentThread());
  InputBuffers input_buffers = std::move(pending_input_buffers_);
  WriteSamples(input_buffers);
  if (pending_end_of_stream_ && pending_input_buffers_.empty()) {
    // All pending buffers have been sent to the decoder; now it's safe to
    // send the end-of-stream signal.
    pending_end_of_stream_ = false;
    WriteEndOfStream();
  }
}

#if SB_PLAYER_FILTER_ENABLE_STATE_CHECK
void VideoRendererImpl::CheckBufferingState() {
  if (end_of_stream_decoded_.load()) {
    return;
  }
  auto now = CurrentMonotonicTime();
  if (!end_of_stream_written_.load()) {
    auto elapsed = now - last_buffering_state_update_;
    if (elapsed > kDelayBeforeWarning) {
      switch (buffering_state_) {
        case kWaitForBuffer:
          SB_LOG(ERROR) << "Haven't received input buffer for " << elapsed
                        << " microseconds.";
          break;
        case kWaitForConsumption:
          SB_LOG(ERROR) << "Haven't consumed input buffer for " << elapsed
                        << " microseconds.";
          break;
      }
    }
    elapsed = now - last_can_accept_more_data;
    SB_LOG_IF(ERROR, elapsed > kDelayBeforeWarning)
        << "Haven't ready for input for " << elapsed << " microseconds. "
        << "Frame backlog/max frames: " << number_of_frames_.load() << "/"
        << decoder_->GetMaxNumberOfCachedFrames();
  }
  auto elapsed = now - last_output_;
  SB_LOG_IF(ERROR, elapsed > kDelayBeforeWarning)
      << "Haven't received any output for " << elapsed << " microseconds.";
  Schedule(std::bind(&VideoRendererImpl::CheckBufferingState, this),
           kCheckBufferingStateInterval);
}

void VideoRendererImpl::CheckForFrameLag(int64_t last_decoded_frame_timestamp) {
  int64_t now = CurrentMonotonicTime();
  // Limit check frequency to minimize call to GetCurrentMediaTime().
  if (now - time_of_last_lag_warning_ < kMinLagWarningInterval) {
    return;
  }
  time_of_last_lag_warning_ = now;

  bool is_playing;
  bool is_eos_played;
  bool is_underflow;
  double playback_rate;
  int64_t media_time = media_time_provider_->GetCurrentMediaTime(
      &is_playing, &is_eos_played, &is_underflow, &playback_rate);
  if (is_eos_played) {
    return;
  }
  int64_t frame_time = last_decoded_frame_timestamp;
  int64_t diff_media_frame_time = media_time - frame_time;
  if (diff_media_frame_time <= kDelayBeforeWarning) {
    return;
  }
  SB_LOG(WARNING) << "Video renderer wrote sample with frame time"
                  << " lagging " << diff_media_frame_time * 1.0f / 1'000'000LL
                  << " s behind media time";
}

#endif  // SB_PLAYER_FILTER_ENABLE_STATE_CHECK

}  // namespace starboard
