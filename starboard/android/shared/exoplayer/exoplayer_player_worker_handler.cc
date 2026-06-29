// Copyright 2026 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/android/shared/exoplayer/exoplayer_player_worker_handler.h"

#include <memory>
#include <string>

#include "starboard/android/shared/exoplayer/exoplayer_bridge.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/common/player.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/player/input_buffer_internal.h"
#include "starboard/shared/starboard/player/job_queue.h"
#include "starboard/shared/starboard/player/player_worker.h"

namespace starboard {
namespace {

const int64_t kUpdateIntervalUsec = 200'000;  // 200ms
}  // namespace

ExoPlayerPlayerWorkerHandler::ExoPlayerPlayerWorkerHandler(
    const SbPlayerCreationParam* creation_param)
    : JobOwner(kDetached),
      update_job_([this]() { Update(); }),
      creation_param_(*creation_param) {
  SB_CHECK_EQ(creation_param->output_mode, kSbPlayerOutputModePunchOut)
      << "ExoPlayer only supports punch-out playback.";
}

Result<void> ExoPlayerPlayerWorkerHandler::Init(
    JobQueue* job_queue,
    SbPlayer player,
    UpdateMediaInfoCB update_media_info_cb,
    GetPlayerStateCB get_player_state_cb,
    UpdatePlayerStateCB update_player_state_cb,
    UpdatePlayerErrorCB update_player_error_cb) {
  SB_CHECK(!update_media_info_cb_);

  // Ensure all parameters have to be valid.
  SB_CHECK(SbPlayerIsValid(player));
  SB_CHECK(update_media_info_cb);
  SB_CHECK(get_player_state_cb);
  SB_CHECK(update_player_state_cb);

  update_media_info_cb_ = update_media_info_cb;
  get_player_state_cb_ = get_player_state_cb;
  update_player_state_cb_ = update_player_state_cb;
  update_player_error_cb_ = update_player_error_cb;

  Attach(job_queue);

  bridge_ = std::make_unique<ExoPlayerBridge>(creation_param_.audio_stream_info,
                                              creation_param_.video_stream_info,
                                              job_queue);

  if (!bridge_->is_valid() ||
      !bridge_->Init(
          [this](SbPlayerError error, const std::string& error_message) {
            OnError(error, error_message);
          },
          [this]() { OnPrerolled(); }, [this]() { OnEnded(); })) {
    return Failure("Failed to initialize the ExoPlayer: " +
                   bridge_->GetInitErrorMessage());
  }

  update_job_token_ = Schedule(update_job_, kUpdateIntervalUsec);

  return Success();
}

Result<void> ExoPlayerPlayerWorkerHandler::Seek(int64_t seek_to_time,
                                                int ticket) {
  SB_CHECK(BelongsToCurrentThread());
  SB_CHECK(bridge_->is_valid());

  audio_eos_written_ = false;
  video_eos_written_ = false;

  bridge_->Seek(seek_to_time);

  return Success();
}

Result<void> ExoPlayerPlayerWorkerHandler::WriteSamples(
    const InputBuffers& input_buffers,
    int* samples_written) {
  SB_CHECK(BelongsToCurrentThread());
  SB_CHECK(!input_buffers.empty());
  SB_CHECK(bridge_->is_valid());
  SB_CHECK(samples_written);

  for (const auto& input_buffer : input_buffers) {
    SB_DCHECK(input_buffer);
  }

  SbMediaType sample_type = input_buffers.front()->sample_type();
  *samples_written = 0;
  if (IsEOSWritten(sample_type)) {
    SB_LOG(WARNING) << "Tried to write "
                    << (sample_type == kSbMediaTypeAudio ? "audio" : "video")
                    << " sample after EOS is written.";
  } else {
    if (bridge_->CanAcceptMoreData(sample_type)) {
      bridge_->WriteSamples(input_buffers, sample_type);
      *samples_written = static_cast<int>(input_buffers.size());
    }
  }

  return Success();
}

Result<void> ExoPlayerPlayerWorkerHandler::WriteEndOfStream(
    SbMediaType sample_type) {
  SB_CHECK(BelongsToCurrentThread());
  SB_CHECK(bridge_->is_valid());

  if (IsEOSWritten(sample_type)) {
    SB_LOG(WARNING) << "Tried to write "
                    << (sample_type == kSbMediaTypeAudio ? "audio" : "video")
                    << " EOS sample after EOS is written.";
    return Success();
  }

  bridge_->WriteEOS(sample_type);

  if (sample_type == kSbMediaTypeAudio) {
    audio_eos_written_ = true;
  } else {
    video_eos_written_ = true;
  }

  return Success();
}

Result<void> ExoPlayerPlayerWorkerHandler::SetPause(bool pause) {
  SB_CHECK(BelongsToCurrentThread());
  SB_CHECK(bridge_->is_valid());

  bridge_->SetPause(pause);

  return Success();
}

Result<void> ExoPlayerPlayerWorkerHandler::SetPlaybackRate(
    double playback_rate) {
  SB_CHECK(BelongsToCurrentThread());
  SB_CHECK(bridge_->is_valid());

  bridge_->SetPlaybackRate(playback_rate);

  return Success();
}

void ExoPlayerPlayerWorkerHandler::SetVolume(double volume) {
  SB_CHECK(BelongsToCurrentThread());
  SB_CHECK(bridge_->is_valid());

  bridge_->SetVolume(volume);
}

void ExoPlayerPlayerWorkerHandler::Stop() {
  SB_CHECK(BelongsToCurrentThread());
  SB_CHECK(bridge_->is_valid());

  RemoveJobByToken(&update_job_token_);

  bridge_->Stop();

  update_media_info_cb_ = nullptr;
  get_player_state_cb_ = nullptr;
  update_player_state_cb_ = nullptr;
  update_player_error_cb_ = nullptr;
}

void ExoPlayerPlayerWorkerHandler::Update() {
  SB_DCHECK(BelongsToCurrentThread());
  SB_DCHECK(bridge_->is_valid());
  SB_DCHECK(get_player_state_cb_);

  if (get_player_state_cb_() == kSbPlayerStatePresenting) {
    ExoPlayerBridge::MediaInfo info = bridge_->GetMediaInfo();
    SB_DCHECK(update_media_info_cb_);
    update_media_info_cb_(info.media_time_usec, info.dropped_frames,
                          info.is_playing);
  }

  RemoveJobByToken(&update_job_token_);
  update_job_token_ = Schedule(update_job_, kUpdateIntervalUsec);
}

void ExoPlayerPlayerWorkerHandler::OnError(SbPlayerError error,
                                           const std::string& error_message) {
  SB_CHECK(update_player_error_cb_);
  if (!reported_error_.exchange(true)) {
    RunOnWorker([this, error, error_message]() {
      update_player_error_cb_(error, error_message.empty()
                                         ? "ExoPlayerPlayerWorkerHandler error"
                                         : error_message);
    });
  }
}

void ExoPlayerPlayerWorkerHandler::OnPrerolled() {
  SB_CHECK(get_player_state_cb_);
  SB_CHECK(update_player_state_cb_);
  RunOnWorker([this]() {
    SB_CHECK_EQ(get_player_state_cb_(), kSbPlayerStatePrerolling)
        << "Invalid player state "
        << GetPlayerStateName(get_player_state_cb_());

    update_player_state_cb_(kSbPlayerStatePresenting);
  });
}

void ExoPlayerPlayerWorkerHandler::OnEnded() {
  SB_CHECK(update_player_state_cb_);
  RunOnWorker([this]() { update_player_state_cb_(kSbPlayerStateEndOfStream); });
}

bool ExoPlayerPlayerWorkerHandler::IsEOSWritten(SbMediaType type) const {
  return type == kSbMediaTypeAudio ? audio_eos_written_ : video_eos_written_;
}

void ExoPlayerPlayerWorkerHandler::RunOnWorker(std::function<void()> task) {
  if (BelongsToCurrentThread()) {
    task();
  } else {
    Schedule(task);
  }
}

}  // namespace starboard
