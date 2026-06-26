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

#include "media/starboard/url_player_renderer.h"

#include "base/logging.h"
#include "base/notreached.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/trace_event/trace_event.h"
#include "media/base/pipeline_status.h"
#include "media/starboard/buildflags.h"
#include "starboard/common/player.h"

namespace media {

using base::Time;
using base::TimeDelta;

UrlPlayerRenderer::UrlPlayerRenderer(
    const scoped_refptr<base::SequencedTaskRunner>& task_runner,
    std::unique_ptr<MediaLog> media_log,
    const base::UnguessableToken& overlay_plane_id,
    const gfx::Size& viewport_size)
    : task_runner_(task_runner),
      media_log_(std::move(media_log)),
      viewport_size_(viewport_size) {
  LOG(INFO) << "UrlPlayerRenderer created.";
}

UrlPlayerRenderer::~UrlPlayerRenderer() {
  LOG(INFO) << "UrlPlayerRenderer destroyed.";
}

void UrlPlayerRenderer::Initialize(MediaResource* media_resource,
                                   RendererClient* client,
                                   PipelineStatusCallback init_cb) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(state_, STATE_UNINITIALIZED);

  LOG(INFO) << "Initializing UrlPlayerRenderer.";

  client_ = client;
  init_cb_ = std::move(init_cb);

  if (source_url_.empty()) {
    LOG(ERROR) << "Initialize() called without source URL set. "
               << "URL must be set via SetSourceUrl() before Initialize().";
    state_ = STATE_ERROR;
    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(std::move(init_cb_),
                                  PIPELINE_ERROR_INITIALIZATION_FAILED));
    return;
  }

  state_ = STATE_INITIALIZING;

  if (get_sb_window_handle_cb_) {
    // Get SbWindow from CobaltRenderContentClient.
    get_sb_window_handle_cb_.Run();
    return;
  }

  CreatePlayerBridge();
}

void UrlPlayerRenderer::SetCdm(CdmContext* cdm_context,
                               CdmAttachedCB cdm_attached_cb) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(cdm_context);

  if (cdm_context_ || SbDrmSystemIsValid(drm_system_)) {
    LOG(WARNING) << "Switching CDM not supported.";
    std::move(cdm_attached_cb).Run(false);
    return;
  }

  cdm_context_ = cdm_context;
  drm_system_ = cdm_context_->GetSbDrmSystem();

  if (player_bridge_ && SbDrmSystemIsValid(drm_system_)) {
    LOG(INFO) << "Attaching DRM system to existing player bridge.";
    player_bridge_->SetDrmSystem(drm_system_);
  }

  std::move(cdm_attached_cb).Run(true);
  LOG(INFO) << "CDM set successfully.";
}

void UrlPlayerRenderer::SetLatencyHint(
    std::optional<base::TimeDelta> latency_hint) {
  LOG(INFO) << "SetLatencyHint() not supported for URL player.";
}

void UrlPlayerRenderer::Flush(base::OnceClosure flush_cb) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(flush_cb);

  if (!player_bridge_) {
    LOG(WARNING) << "Flush() called with no player bridge.";
    std::move(flush_cb).Run();
    return;
  }

  player_bridge_->PrepareForSeek();
  playback_rate_ = 0.0;

  if (buffering_state_ != BUFFERING_HAVE_NOTHING) {
    buffering_state_ = BUFFERING_HAVE_NOTHING;
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&UrlPlayerRenderer::OnBufferingStateChange,
                       weak_factory_.GetWeakPtr(), buffering_state_));
  }

  // URL player has no in-flight demuxer reads, complete immediately.
  state_ = STATE_FLUSHED;
  std::move(flush_cb).Run();
}

void UrlPlayerRenderer::StartPlayingFrom(TimeDelta time) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(INFO) << "UrlPlayerRenderer::StartPlayingFrom() called with " << time
            << '.';
  if (state_ != STATE_FLUSHED) {
    DCHECK_EQ(state_, STATE_ERROR);
    return;
  }

  state_ = STATE_PLAYING;
  player_bridge_->Seek(time);
}

void UrlPlayerRenderer::SetPlaybackRate(double playback_rate) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (playback_rate < 0.0) {
    LOG(INFO) << "UrlPlayerRenderer::SetPlaybackRate(): invalid playback rate "
              << playback_rate << '.';
    return;
  }

  LOG(INFO) << "UrlPlayerRenderer changes playback rate from " << playback_rate_
            << " to " << playback_rate << '.';

  if (playback_rate_ == playback_rate) {
    return;
  }

  playback_rate_ = playback_rate;

  if (player_bridge_) {
    player_bridge_->SetPlaybackRate(playback_rate_);
  }
}

void UrlPlayerRenderer::SetVolume(float volume) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (volume < 0.0f || volume > 1.0f) {
    LOG(INFO) << "UrlPlayerRenderer::SetVolume(): invalid volume " << volume
              << '.';
    return;
  }

  LOG(INFO) << "UrlPlayerRenderer changes volume from " << volume_ << " to "
            << volume << '.';

  if (volume_ == volume) {
    return;
  }

  volume_ = volume;

  if (player_bridge_) {
    player_bridge_->SetVolume(volume_);
  }
}

TimeDelta UrlPlayerRenderer::GetMediaTime() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (!player_bridge_) {
    return base::Microseconds(0);
  }

  uint32_t video_frames_decoded = 0;
  uint32_t video_frames_dropped = 0;
  uint64_t audio_bytes_decoded = 0;
  uint64_t video_bytes_decoded = 0;
  TimeDelta media_time;
  SbPlayerBridge::PlayerInfo info{&video_frames_decoded, &video_frames_dropped,
                                  &audio_bytes_decoded, &video_bytes_decoded,
                                  &media_time};

  player_bridge_->GetInfo(&info);

  PipelineStatistics statistics;
  statistics.audio_bytes_decoded = audio_bytes_decoded;
  statistics.video_bytes_decoded = video_bytes_decoded;

  if (video_frames_decoded > last_video_frames_decoded_) {
    statistics.video_frames_decoded =
        video_frames_decoded - last_video_frames_decoded_;
    last_video_frames_decoded_ = video_frames_decoded;
  }

  if (video_frames_dropped > last_video_frames_dropped_) {
    statistics.video_frames_dropped =
        video_frames_dropped - last_video_frames_dropped_;
    last_video_frames_dropped_ = video_frames_dropped;
  }

  if (statistics.video_frames_decoded > 0 ||
      statistics.video_frames_dropped > 0) {
    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&UrlPlayerRenderer::OnStatisticsUpdate,
                                  weak_factory_.GetWeakPtr(), statistics));
  }

  // Retry video dimensions if not yet reported (AVPlayer may expose them
  // after the initial kSbPlayerStatePresenting event).
  if (!has_reported_dimensions_ && state_ == STATE_PLAYING) {
    TryReportVideoDimensions();
  }

  return media_time;
}

void UrlPlayerRenderer::OnTracksChanged(
    DemuxerStream::Type track_type,
    std::vector<DemuxerStream*> enabled_tracks,
    base::OnceClosure change_completed_cb) {
  LOG(INFO) << "OnTracksChanged not supported for type: " << track_type;
  std::move(change_completed_cb).Run();
}

void UrlPlayerRenderer::SetSourceUrl(const std::string& source_url) {
  LOG(INFO) << "SetSourceUrl() called with url length=" << source_url.size();
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(state_, STATE_UNINITIALIZED)
      << "SetSourceUrl() must be called before Initialize().";
  source_url_ = source_url;
}

void UrlPlayerRenderer::SetUrlPlayerRendererCallbacks(
    PaintVideoHoleFrameCallback paint_video_hole_frame_cb,
    UpdateStarboardRenderingModeCallback update_starboard_rendering_mode_cb,
    GetSbWindowHandleCallback get_sb_window_handle_cb) {
  paint_video_hole_frame_cb_ = std::move(paint_video_hole_frame_cb);
  update_starboard_rendering_mode_cb_ =
      std::move(update_starboard_rendering_mode_cb);
  get_sb_window_handle_cb_ = std::move(get_sb_window_handle_cb);
}

void UrlPlayerRenderer::OnVideoGeometryChange(const gfx::Rect& output_rect) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  output_rect_ = output_rect;
  ApplyPendingBounds();
}

void UrlPlayerRenderer::OnSbWindowHandleReady(uint64_t sb_window_handle) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());

  if (state_ != STATE_INITIALIZING || player_bridge_) {
    LOG(WARNING) << "OnSbWindowHandleReady called in unexpected state: "
                 << state_;
    return;
  }

  if (sb_window_handle != 0) {
    sb_window_ = reinterpret_cast<SbWindow>(sb_window_handle);
    if (!SbWindowIsValid(sb_window_)) {
      LOG(WARNING) << "SbWindow is not valid.";
    }
  }
  CreatePlayerBridge();
}

SbPlayerInterface* UrlPlayerRenderer::GetSbPlayerInterface() {
  return test_sbplayer_interface_ ? test_sbplayer_interface_
                                  : &sbplayer_interface_;
}

// Private methods

void UrlPlayerRenderer::CreatePlayerBridge() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(init_cb_);
  DCHECK_EQ(state_, STATE_INITIALIZING);
  DCHECK(!source_url_.empty());

  TRACE_EVENT0("media", "UrlPlayerRenderer::CreatePlayerBridge");

  DCHECK(!player_bridge_);
  player_bridge_.reset();

  LOG(INFO) << "Creating SbPlayerBridge.";

  player_bridge_.reset(new SbPlayerBridge(
      GetSbPlayerInterface(), task_runner_, source_url_, sb_window_, this,
      /*allow_resume_after_suspend=*/false, kSbPlayerOutputModePunchOut,
      base::BindRepeating(
          &UrlPlayerRenderer::OnEncryptedMediaInitDataEncountered,
          base::Unretained(this))
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
          ,
      /*pipeline_identifier=*/""
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
      ));

  if (player_bridge_ && player_bridge_->IsValid()) {
    ApplyPendingBounds();

    const auto output_mode = player_bridge_->GetSbPlayerOutputMode();
    if (output_mode != kSbPlayerOutputModePunchOut) {
      LOG(ERROR) << "URL player requires punch-out mode but got output mode "
                 << output_mode;
      player_bridge_.reset();
      state_ = STATE_ERROR;
      std::move(init_cb_).Run(
          PipelineStatus(DECODER_ERROR_NOT_SUPPORTED,
                         "URL player requires punch-out output mode"));
      return;
    }

    update_starboard_rendering_mode_cb_.Run(StarboardRenderingMode::kPunchOut);

    if (SbDrmSystemIsValid(drm_system_)) {
      LOG(INFO) << "Attaching DRM system to player bridge.";
      player_bridge_->SetDrmSystem(drm_system_);
    }

    player_bridge_->SetVolume(volume_);

    state_ = STATE_FLUSHED;

    // Defer init_cb_ until SbPlayer reports kSbPlayerStateInitialized
    // via OnPlayerStatus().
    LOG(INFO) << "SbPlayerBridge created, waiting for initialized.";
    return;
  }

  std::string error_message =
      player_bridge_ ? player_bridge_->GetPlayerCreationErrorMessage()
                     : "Player bridge creation failed";
  player_bridge_.reset();
  state_ = STATE_ERROR;
  PipelineStatus status(DECODER_ERROR_NOT_SUPPORTED,
                        "CreatePlayerBridge failed: " + error_message);
  LOG(INFO) << status;
  std::move(init_cb_).Run(status);
}

void UrlPlayerRenderer::ApplyPendingBounds() {
  if (!player_bridge_ || !output_rect_) {
    return;
  }

  player_bridge_->SetBounds(*output_rect_);
}

void UrlPlayerRenderer::OnStatisticsUpdate(const PipelineStatistics& stats) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (client_) {
    client_->OnStatisticsUpdate(stats);
  }
}

void UrlPlayerRenderer::OnNeedData(DemuxerStream::Type type,
                                   int max_number_of_buffers_to_write) {
  // URL player handles all buffering natively; ignore.
}

void UrlPlayerRenderer::OnPlayerStatus(SbPlayerState state) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  LOG(INFO) << "UrlPlayerRenderer::OnPlayerStatus() called with state="
            << ::starboard::GetPlayerStateName(state);

  if (!player_bridge_) {
    LOG(WARNING)
        << "UrlPlayerRenderer::OnPlayerStatus() called without player bridge.";
    return;
  }

  switch (state) {
    case kSbPlayerStateInitialized:
      if (init_cb_) {
        std::move(init_cb_).Run(PipelineStatus(PIPELINE_OK));
      }
      break;
    case kSbPlayerStatePrerolling:
      break;
    case kSbPlayerStatePresenting: {
      buffering_state_ = BUFFERING_HAVE_ENOUGH;
      task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(&UrlPlayerRenderer::OnBufferingStateChange,
                         weak_factory_.GetWeakPtr(), buffering_state_));

      // Query video resolution and report for video hole punch-out.
      TryReportVideoDimensions();
      if (!has_reported_dimensions_) {
        LOG(WARNING) << "Dimensions not yet available at presenting; "
                        "will retry during GetMediaTime() polling.";
      }

      // Re-apply playback rate. AVPlayer silently ignores rate changes
      // before ReadyToPlay.
      player_bridge_->SetPlaybackRate(playback_rate_);
      break;
    }
    case kSbPlayerStateEndOfStream:
      if (client_) {
        client_->OnEnded();
      }
      break;
    case kSbPlayerStateDestroyed:
      break;
  }
}

void UrlPlayerRenderer::OnPlayerError(SbPlayerError error,
                                      const std::string& message) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(ERROR) << "UrlPlayerRenderer::OnPlayerError() code=" << error
             << " message=\"" << message << "\"";

  if (state_ == STATE_ERROR) {
    return;
  }

  state_ = STATE_ERROR;

  switch (error) {
    case kSbPlayerErrorDecode:
      MEDIA_LOG(ERROR, media_log_) << message;
      NotifyError(PIPELINE_ERROR_DECODE);
      break;
    case kSbPlayerErrorCapabilityChanged:
      MEDIA_LOG(ERROR, media_log_)
          << (message.empty()
                  ? kSbPlayerCapabilityChangedErrorMessage
                  : base::StringPrintf("%s: %s",
                                       kSbPlayerCapabilityChangedErrorMessage,
                                       message.c_str()));
      NotifyError(PIPELINE_ERROR_DECODE);
      break;
    case kSbPlayerErrorMax:
      NOTREACHED();
      break;
  }
}

void UrlPlayerRenderer::NotifyError(PipelineStatus status) {
  if (init_cb_) {
    std::move(init_cb_).Run(status);
  } else {
    client_->OnError(status);
  }
}
void UrlPlayerRenderer::TryReportVideoDimensions() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(player_bridge_);

  int width = 0, height = 0;
  player_bridge_->GetVideoResolution(&width, &height);
  if (width > 0 && height > 0) {
    gfx::Size size(width, height);
    if (client_) {
      client_->OnVideoNaturalSizeChange(size);
    }
    paint_video_hole_frame_cb_.Run(size);
    has_reported_dimensions_ = true;
    LOG(INFO) << "Video size: " << width << "x" << height;
  }
}

void UrlPlayerRenderer::OnBufferingStateChange(BufferingState state) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (client_) {
    client_->OnBufferingStateChange(state, BUFFERING_CHANGE_REASON_UNKNOWN);
  }
}

SbDecodeTarget UrlPlayerRenderer::GetSbDecodeTarget() {
  LOG(INFO) << "GetSbDecodeTarget() not supported for URL player.";
  return kSbDecodeTargetInvalid;
}

void UrlPlayerRenderer::OnEncryptedMediaInitDataEncountered(
    const char* init_data_type,
    const unsigned char* init_data,
    unsigned int init_data_length) {
  LOG(INFO) << "OnEncryptedMediaInitDataEncountered() (no-op).";
  // TODO: Forward encrypted media init data to the EME/DRM layer.
}

}  // namespace media
