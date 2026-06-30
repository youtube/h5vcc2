// Copyright 2024 The Cobalt Authors. All Rights Reserved.
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

#include "media/starboard/starboard_renderer.h"

#include "base/feature_list.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/trace_event/trace_event.h"
#include "media/base/audio_codecs.h"
#include "media/base/decoder_buffer.h"
#include "media/base/media_switches.h"
#include "media/base/video_codecs.h"
#include "media/starboard/buildflags.h"
#include "media/starboard/decoder_buffer_allocator.h"
#include "starboard/common/media.h"
#include "starboard/common/player.h"
#include "starboard/common/string.h"

#if BUILDFLAG(IS_ANDROID)
#include "media/base/android/android_overlay.h"
#include "media/base/android_overlay_config.h"
#endif  // BUILDFLAG(IS_ANDROID)

namespace media {

namespace {

using ::starboard::GetMediaAudioConnectorName;
using ::starboard::GetPlayerStateName;

// In the OnNeedData(), it attempts to write one more audio access
// unit than the audio write duration. Specifically, the check
// |time_ahead_of_playback_for_preroll| > |adjusted_write_duration_for_preroll|
// is used to skip audio writing, using '>' instead of '>='.
// Since the calculated write duration during preroll may align exactly
// with the audio write duration, the current check can fail, leading to an
// additional call to SbPlayerWriteSamples(). By writing an extra guard audio
// buffer, this extra write during preroll can be eliminated.
const int kPrerollGuardAudioBuffer = 1;

// The value of 12 was chosen after experimentation was done. For more
// details, see b/497891900.
constexpr int kDefaultMaxSamplePerWrite = 12;

bool HasRemoteAudioOutputs(
    const std::vector<SbMediaAudioConfiguration>& configurations) {
  for (auto&& configuration : configurations) {
    const auto connector = configuration.connector;
    switch (connector) {
      case kSbMediaAudioConnectorUnknown:
      case kSbMediaAudioConnectorAnalog:
      case kSbMediaAudioConnectorBuiltIn:
      case kSbMediaAudioConnectorHdmi:
      case kSbMediaAudioConnectorSpdif:
      case kSbMediaAudioConnectorUsb:
        LOG(INFO) << "Encountered local audio connector: "
                  << GetMediaAudioConnectorName(connector);
        break;
      case kSbMediaAudioConnectorBluetooth:
      case kSbMediaAudioConnectorRemoteWired:
      case kSbMediaAudioConnectorRemoteWireless:
      case kSbMediaAudioConnectorRemoteOther:
        LOG(INFO) << "Encountered remote audio connector: "
                  << GetMediaAudioConnectorName(connector);
        return true;
    }
  }

  LOG(INFO) << "No remote audio outputs found.";

  return false;
}

// The function adjusts audio write duration proportionally to the playback
// rate, when the playback rate is greater than 1.0.
//
// Having the right write duration is important:
// 1. Too small of it causes audio underflow.
// 2. Too large of it causes excessive audio switch latency.
// When playback rate is 2x, an 0.5 seconds of write duration effectively only
// lasts for 0.25 seconds and causes audio underflow, and the function will
// adjust it to 1 second in this case.
TimeDelta AdjustWriteDurationForPlaybackRate(TimeDelta write_duration,
                                             float playback_rate) {
  if (playback_rate <= 1.0) {
    return write_duration;
  }

  return write_duration * playback_rate;
}

// The function returns the default frames per DecoderBuffer.
//
// The number of frames is used to estimate the number of samples per write for
// audio preroll according to |audio_write_duration_|.
int GetDefaultAudioFramesPerBuffer(AudioCodec codec) {
  switch (codec) {
    case AudioCodec::kOpus:
      return 960;
    case AudioCodec::kAAC:
      return 1024;
    case AudioCodec::kAC3:
    case AudioCodec::kEAC3:
      return 1536;
    default:
      NOTREACHED();
      return 1;
  }
}
}  // namespace

StarboardRenderer::StarboardRenderer(
    const scoped_refptr<base::SequencedTaskRunner>& task_runner,
    std::unique_ptr<MediaLog> media_log,
    const base::UnguessableToken& overlay_plane_id,
    TimeDelta audio_write_duration_local,
    TimeDelta audio_write_duration_remote,
    const std::string& max_video_capabilities,
    const StarboardRendererConfig::ExperimentalFeatures& experimental_features,
    const gfx::Size& viewport_size
#if BUILDFLAG(IS_ANDROID)
    ,
    const AndroidOverlayMojoFactoryCB android_overlay_factory_cb
#endif  // BUILDFLAG(IS_ANDROID)
    )
    : state_(STATE_UNINITIALIZED),
      task_runner_(task_runner),
      media_log_(std::move(media_log)),
      cdm_context_(nullptr),
      buffering_state_(BUFFERING_HAVE_NOTHING),
      audio_write_duration_local_(audio_write_duration_local),
      audio_write_duration_remote_(audio_write_duration_remote),
      max_video_capabilities_(max_video_capabilities),
      experimental_features_(experimental_features),
      max_samples_per_write_(
          experimental_features.max_samples_per_write.value_or(
              kDefaultMaxSamplePerWrite)),
      viewport_size_(viewport_size)
#if BUILDFLAG(IS_ANDROID)
      ,
      android_overlay_factory_cb_(std::move(android_overlay_factory_cb))
#endif  // BUILDFLAG(IS_ANDROID)
{
  DCHECK(task_runner_);
  DCHECK(media_log_);
  CHECK_GT(max_samples_per_write_, 0);
  LOG(INFO) << "StarboardRenderer constructed: audio_write_duration_local="
            << audio_write_duration_local_
            << ", audio_write_duration_remote=" << audio_write_duration_remote_
            << ", max_video_capabilities="
            << base::GetQuotedJSONString(max_video_capabilities_)
            << ", max_samples_per_write=" << max_samples_per_write_
            << ", view_port_size=" << viewport_size_.ToString()
            << ", experimental_features=" << experimental_features_;
}

StarboardRenderer::~StarboardRenderer() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  LOG(INFO) << "Destructing StarboardRenderer.";

  player_bridge_.reset();

  LOG(INFO) << "SbPlayerBridge destructed.";
}

void StarboardRenderer::Initialize(MediaResource* media_resource,
                                   RendererClient* client,
                                   PipelineStatusCallback init_cb) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(state_, STATE_UNINITIALIZED);
  DCHECK(!client_);
  DCHECK(!audio_stream_);
  DCHECK(!video_stream_);
  DCHECK(media_resource);
  DCHECK(client);
  DCHECK(init_cb);

  TRACE_EVENT0("media", "StarboardRenderer::Initialize");

  LOG(INFO) << "Initializing StarboardRenderer.";

#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  // Note that once this code block is enabled, we should also ensure that the
  // posted callback executes on the right object (i.e. not destroyed, use
  // WeakPtr?) in the right state (not flushed?).
  if (suspended_) {
    task_runner_->PostDelayedTask(
        FROM_HERE,
        base::Bind(&StarboardRenderer::Initialize, this, media_resource, client,
                   init_cb),
        base::Milliseconds(kRetryDelayAtSuspendInMilliseconds));
    return;
  }
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)

  client_ = client;
  init_cb_ = std::move(init_cb);

  audio_stream_ = media_resource->GetFirstStream(DemuxerStream::AUDIO);
  video_stream_ = media_resource->GetFirstStream(DemuxerStream::VIDEO);

  if (audio_stream_ == nullptr && video_stream_ == nullptr) {
    LOG(INFO)
        << "The video has to contain at least an audio track or a video track.";
    std::move(init_cb_).Run(PipelineStatus(
        DEMUXER_ERROR_NO_SUPPORTED_STREAMS,
        "The video has to contain at least an audio track or a video track."));
    return;
  }

  // Enable bit stream converter for aac and h264 streams, which will convert
  // them into ADTS and Annex B.  This is only required for FFmpegDemuxer, and
  // has no effect for other demuxers (e.g. ChunkDemuxer).
  if (audio_stream_ &&
      audio_stream_->audio_decoder_config().codec() == AudioCodec::kAAC) {
    LOG(INFO) << "Encountered AAC stream, enabling bit stream converter ...";
    audio_stream_->EnableBitstreamConverter();
  }

  if (video_stream_ &&
      video_stream_->video_decoder_config().codec() == VideoCodec::kH264) {
    LOG(INFO) << "Encountered H264 stream, enabling bit stream converter ...";
    video_stream_->EnableBitstreamConverter();
  }

  bool is_encrypted =
      audio_stream_ && audio_stream_->audio_decoder_config().is_encrypted();
  is_encrypted |=
      video_stream_ && video_stream_->video_decoder_config().is_encrypted();
  if (is_encrypted && !cdm_context_) {
    // TODO(b/328305808): Shall we call client_->OnVideoNaturalSizeChange()
    // to provide an initial size before the license exchange finishes?
    LOG(WARNING) << __func__ << ": Has encrypted stream but CDM is not set.";
    state_ = STATE_INIT_PENDING_CDM;
    client_->OnWaiting(WaitingReason::kNoCdm);
    return;
  }

  // |init_cb| will be called inside |CreatePlayerBridge()|.
  state_ = STATE_INITIALIZING;

#if BUILDFLAG(IS_ANDROID)
  if (base::FeatureList::IsEnabled(media::kCobaltUsingAndroidOverlay)) {
    // RequestOverlayInfoCB and create AndroidOverlay if the BASE feature is
    // enabled.
    if (request_overlay_info_cb_ && android_overlay_factory_cb_) {
      LOG(INFO) << "Requesting AndroidOverlay for Video SurfaceView.";
      // Set |restart_for_transitions| to false due to devices are
      // isSetOutputSurfaceSupported() in
      // media/base/android/java/src/org/chromium/media/MediaCodecUtil.java.
      request_overlay_info_cb_.Run(/*restart_for_transitions=*/false);
      return;
    }
    // When CobaltUsingAndroidOverlay is enabled, both request_overlay_info_cb_
    // and android_overlay_factory_cb_ should not be null.
    NOTREACHED();
  }
#endif  // BUILDFLAG(IS_ANDROID)

  if (get_sb_window_handle_cb_) {
    // Get SbWindow from CobaltRenderContentClient.
    get_sb_window_handle_cb_.Run();
    return;
  }

  CreatePlayerBridge();
}

void StarboardRenderer::SetCdm(CdmContext* cdm_context,
                               CdmAttachedCB cdm_attached_cb) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(cdm_context);
  TRACE_EVENT0("media", "StarboardRenderer::SetCdm");

  if (cdm_context_ || SbDrmSystemIsValid(drm_system_)) {
    LOG(WARNING) << "Switching CDM not supported.";
    std::move(cdm_attached_cb).Run(false);
    return;
  }

  cdm_context_ = cdm_context;
  drm_system_ = cdm_context_->GetSbDrmSystem();
  std::move(cdm_attached_cb).Run(true);
  LOG(INFO) << "CDM set successfully.";

  if (state_ != STATE_INIT_PENDING_CDM) {
    return;
  }

  DCHECK(init_cb_);
  state_ = STATE_INITIALIZING;
  CreatePlayerBridge();
}

void StarboardRenderer::Flush(base::OnceClosure flush_cb) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!pending_flush_cb_);
  DCHECK(flush_cb);

  if (!player_bridge_) {
    return;
  }

  LOG(INFO) << "Flushing StarboardRenderer.";

  // Prepares the |player_bridge_| for Seek(), the |player_bridge_| won't
  // request more data from us before Seek() is called.
  player_bridge_->PrepareForSeek();
  // Reset |playback_rate_| after PrepareForSeek().
  playback_rate_ = 0.0;
  pending_video_config_.reset();
  pending_audio_config_.reset();

  if (buffering_state_ != BUFFERING_HAVE_NOTHING) {
    buffering_state_ = BUFFERING_HAVE_NOTHING;
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&StarboardRenderer::OnBufferingStateChange,
                       weak_factory_.GetWeakPtr(), buffering_state_));
  }

  // The function can be called when there are in-flight Demuxer::Read() calls
  // posted in OnDemuxerStreamRead(), we will wait until they are completed.
  if (audio_read_in_progress_ || video_read_in_progress_) {
    pending_flush_cb_ = std::move(flush_cb);
  } else {
    state_ = STATE_FLUSHED;
    std::move(flush_cb).Run();
  }
}

void StarboardRenderer::StartPlayingFrom(TimeDelta time) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(buffering_state_, BUFFERING_HAVE_NOTHING);

  LOG(INFO) << "StarboardRenderer::StartPlayingFrom() called with " << time
            << '.';
  LOG_IF(WARNING, time < base::Seconds(0))
      << "Potentially invalid start time " << time << '.';

  if (audio_read_in_progress_ || video_read_in_progress_) {
    constexpr TimeDelta kDelay = base::Milliseconds(50);
    task_runner_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&StarboardRenderer::StartPlayingFrom,
                       weak_factory_.GetWeakPtr(), time),
        kDelay);
    return;
  }

  timestamp_of_last_written_audio_ = TimeDelta();
  is_video_eos_written_ = false;
  StoreMediaTime(time);
  // Ignore pending delayed calls to OnNeedData, and update variables used to
  // decide when to delay.
  audio_read_delayed_ = false;

  seek_time_ = time;

  if (state_ != STATE_FLUSHED) {
    DCHECK_EQ(state_, STATE_ERROR);
    return;
  }

  state_ = STATE_PLAYING;
  player_bridge_->Seek(time);
}

void StarboardRenderer::SetPlaybackRate(double playback_rate) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (playback_rate < 0.0) {
    LOG(INFO) << "StarboardRenderer::SetPlaybackRate(): invalid playback rate "
              << playback_rate << '.';
    return;
  }

  LOG(INFO) << "StarboardRenderer changes playback rate from " << playback_rate_
            << " to " << playback_rate << '.';

  if (playback_rate_ == playback_rate) {
    return;
  }

  playback_rate_ = playback_rate;

  if (player_bridge_) {
    player_bridge_->SetPlaybackRate(playback_rate_);
  }
}

void StarboardRenderer::SetVolume(float volume) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (volume < 0.0 || volume > 1.0) {
    LOG(INFO) << "StarboardRenderer::SetVolume(): invalid volume " << volume
              << '.';
    return;
  }

  LOG(INFO) << "StarboardRenderer changes volume from " << volume_ << " to "
            << volume << '.';

  if (volume_ == volume) {
    return;
  }

  volume_ = volume;

  if (player_bridge_) {
    player_bridge_->SetVolume(volume_);
  }
}

TimeDelta StarboardRenderer::GetMediaTime() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (!player_bridge_) {
    StoreMediaTime(TimeDelta());
    return base::Microseconds(0);
  }

  uint32_t video_frames_decoded, video_frames_dropped;
  uint64_t audio_bytes_decoded, video_bytes_decoded;
  TimeDelta media_time;
  SbPlayerBridge::PlayerInfo info{&video_frames_decoded, &video_frames_dropped,
                                  &audio_bytes_decoded, &video_bytes_decoded,
                                  &media_time};

  player_bridge_->GetInfo(&info);

  // Report dropped frames since we have the info anyway.
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
    // TODO(b/375273093): Refine frame drops reporting, and double check
    //                    whether we should report more statistics, and/or
    //                    reduce the frequency of reporting.
    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&StarboardRenderer::OnStatisticsUpdate,
                                  weak_factory_.GetWeakPtr(), statistics));
  }
  StoreMediaTime(media_time);

  return media_time;
}

void StarboardRenderer::OnTracksChanged(
    DemuxerStream::Type track_type,
    std::vector<DemuxerStream*> enabled_tracks,
    base::OnceClosure change_completed_cb) {
  LOG(INFO) << "OnTracksChanged not supported for type: " << track_type;
  std::move(change_completed_cb).Run();
}

void StarboardRenderer::SetStarboardRendererCallbacks(
    PaintVideoHoleFrameCallback paint_video_hole_frame_cb,
    UpdateStarboardRenderingModeCallback update_starboard_rendering_mode_cb,
    GetSbWindowHandleCallback get_sb_window_handle_cb
#if BUILDFLAG(IS_ANDROID)
    ,
    RequestOverlayInfoCallBack request_overlay_info_cb
#endif  // BUILDFLAG(IS_ANDROID)
) {
  paint_video_hole_frame_cb_ = std::move(paint_video_hole_frame_cb);
  update_starboard_rendering_mode_cb_ =
      std::move(update_starboard_rendering_mode_cb);
  get_sb_window_handle_cb_ = std::move(get_sb_window_handle_cb);
#if BUILDFLAG(IS_ANDROID)
  request_overlay_info_cb_ = std::move(request_overlay_info_cb);
#endif  // BUILDFLAG(IS_ANDROID)
}

void StarboardRenderer::OnVideoGeometryChange(const gfx::Rect& output_rect) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  if (output_rect_ == output_rect) {
    return;
  }

  output_rect_ = output_rect;

#if BUILDFLAG(IS_ANDROID)
  if (overlay_ && output_rect_) {
    overlay_->ScheduleLayout(*output_rect_);
    return;
  }
#endif  // BUILDFLAG(IS_ANDROID)

  ApplyPendingBounds();
}

void StarboardRenderer::OnSbWindowHandleReady(const uint64_t sb_window_handle) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  if (sb_window_handle != 0) {
    sb_window_ = reinterpret_cast<SbWindow>(sb_window_handle);
    if (!SbWindowIsValid(sb_window_)) {
      LOG(WARNING) << "SbWindow is not valid.";
    }
  }
  CreatePlayerBridge();
}

#if BUILDFLAG(IS_ANDROID)
void StarboardRenderer::OnOverlayInfoChanged(const OverlayInfo& overlay_info) {
  bool overlay_changed = !overlay_info_.RefersToSameOverlayAs(overlay_info);
  if (!overlay_changed) {
    return;
  }

  if (android_overlay_factory_cb_.is_null()) {
    LOG(ERROR) << " AndroidOverlayMojoFactoryCB is NULL.";
    state_ = STATE_ERROR;
    if (init_cb_) {
      std::move(init_cb_).Run(PipelineStatus(
          DECODER_ERROR_NOT_SUPPORTED, "AndroidOverlayMojoFactoryCB is null"));
    }
    return;
  }

  overlay_info_ = overlay_info;

  AndroidOverlayConfig config;
  config.ready_cb = base::BindOnce(&StarboardRenderer::OnOverlayReady,
                                   weak_factory_.GetWeakPtr());
  config.failed_cb = base::BindOnce(&StarboardRenderer::OnOverlayFailed,
                                    weak_factory_.GetWeakPtr());
  gfx::Rect initial_rect = gfx::Rect(viewport_size_);
  if (initial_rect.IsEmpty()) {
    // Provide a default non-zero size, this is fine as it will be
    // updated by OnVideoGeometryChange().
    initial_rect = gfx::Rect(0, 0, 1920, 1080);
  }
  config.rect = initial_rect;
  config.secure = cdm_context_ != nullptr;
  config.power_efficient = false;

  overlay_ = android_overlay_factory_cb_.Run(*overlay_info.routing_token,
                                             std::move(config));
  LOG(INFO) << " Overlay info changed, requested AndroidOverlay. Token: "
            << overlay_info.routing_token.value().ToString();
}
#endif  // BUILDFLAG(IS_ANDROID)

SbPlayerInterface* StarboardRenderer::GetSbPlayerInterface() {
  if (test_sbplayer_interface_) {
    return test_sbplayer_interface_;
  }
  return &sbplayer_interface_;
}

void StarboardRenderer::CreatePlayerBridge() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(init_cb_);
  DCHECK_EQ(state_, STATE_INITIALIZING);
  DCHECK(audio_stream_ || video_stream_);

  TRACE_EVENT0("media", "StarboardRenderer::CreatePlayerBridge");

#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  // Note that once this code block is enabled, we should also ensure that the
  // posted callback executes on the right object (i.e. not destroyed, use
  // WeakPtr?) in the right state (not flushed?).
  if (suspended_) {
    task_runner_->PostDelayedTask(
        FROM_HERE,
        base::Bind(&StarboardRenderer::CreatePlayerBridge, this, init_cb),
        base::Milliseconds(kRetryDelayAtSuspendInMilliseconds));
    return;
  }
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)

  AudioDecoderConfig invalid_audio_config;
  const AudioDecoderConfig& audio_config =
      audio_stream_ ? audio_stream_->audio_decoder_config()
                    : invalid_audio_config;
  VideoDecoderConfig invalid_video_config;
  const VideoDecoderConfig& video_config =
      video_stream_ ? video_stream_->video_decoder_config()
                    : invalid_video_config;

  const std::string audio_mime_type =
      audio_stream_ ? audio_stream_->mime_type() : "";
  const std::string video_mime_type =
      video_stream_ ? video_stream_->mime_type() : "";

  std::string error_message;

  DCHECK(!player_bridge_);

  // In the unexpected case that CreatePlayerBridge() is called when a
  // |player_bridge_| is set, reset the existing player first to reduce the
  // number of active players.
  player_bridge_.reset();

  LOG(INFO) << "Creating SbPlayerBridge.";

  player_bridge_.reset(new SbPlayerBridge(
      GetSbPlayerInterface(), task_runner_,
      get_decode_target_graphics_context_provider_func_, audio_config,
      audio_mime_type, video_config, video_mime_type,
      // TODO(b/326497953): Support suspend/resume.
      // TODO(b/326508279): Support background mode.
      sb_window_, drm_system_, this,
      // TODO(b/326497953): Support suspend/resume.
      false,
      // TODO(b/326825450): Revisit 360 videos.
      kSbPlayerOutputModeInvalid, max_video_capabilities_,
      // TODO(b/326654546): Revisit HTMLVideoElement.setMaxVideoInputSize.
      /*max_video_input_size=*/-1, experimental_features_
#if BUILDFLAG(IS_ANDROID)
      ,
      // TODO: b/475294958 - Revisit platform-specific codes above starboard.
      surface_view_
#endif  // BUILDFLAG(IS_ANDROID)
      ));
  if (player_bridge_->IsValid()) {
    // TODO(b/267678497): When `player_bridge_->GetAudioConfigurations()`
    // returns no audio configurations, update the write durations again
    // before the SbPlayer reaches `kSbPlayerStatePresenting`.
    audio_write_duration_for_preroll_ = audio_write_duration_ =
        HasRemoteAudioOutputs(player_bridge_->GetAudioConfigurations())
            ? audio_write_duration_remote_
            : audio_write_duration_local_;
    LOG(INFO) << "SbPlayerBridge created, with audio write duration at "
              << audio_write_duration_for_preroll_
              << " and with max_video_capabilities_ at "
              << max_video_capabilities_;
  } else {
    error_message = player_bridge_->GetPlayerCreationErrorMessage();
    player_bridge_.reset();
    LOG(INFO) << "Failed to create a valid SbPlayerBridge.";
  }

  if (player_bridge_ && player_bridge_->IsValid()) {
    ApplyPendingBounds();

    const auto output_mode = player_bridge_->GetSbPlayerOutputMode();
    switch (output_mode) {
      case kSbPlayerOutputModeDecodeToTexture:
        update_starboard_rendering_mode_cb_.Run(
            StarboardRenderingMode::kDecodeToTexture);
        break;
      case kSbPlayerOutputModePunchOut:
        update_starboard_rendering_mode_cb_.Run(
            StarboardRenderingMode::kPunchOut);
        break;
      case kSbPlayerOutputModeInvalid:
        NOTREACHED() << "Invalid SbPlayer output mode";
        break;
    }

    if (audio_stream_) {
      UpdateDecoderConfig(audio_stream_);
    }
    if (video_stream_) {
      UpdateDecoderConfig(video_stream_);
    }

    player_bridge_->SetVolume(volume_);

    state_ = STATE_FLUSHED;

    // Defer running the initialization callback
    // (|init_cb_|.Run(PipelineStatus(PIPELINE_OK))) until the `SbPlayer`
    // reports it is initialized via
    // `OnPlayerStatus(kSbPlayerStateInitialized)`. This ensures clients don't
    // call `StartPlayingFrom()` until the SbPlayer is actually ready.
    return;
  }

  LOG(INFO) << "StarboardRenderer::CreatePlayerBridge() failed to create a"
               " valid SbPlayerBridge - \""
            << error_message << "\"";

  state_ = STATE_ERROR;
  std::move(init_cb_).Run(PipelineStatus(
      DECODER_ERROR_NOT_SUPPORTED,
      "StarboardRenderer::CreatePlayerBridge() failed to create a valid"
      " SbPlayerBridge - \"" +
          error_message + "\""));
}

void StarboardRenderer::ApplyPendingBounds() {
  if (!player_bridge_ || !output_rect_) {
    return;
  }

  player_bridge_->SetBounds(*output_rect_);
}

void StarboardRenderer::UpdateDecoderConfig(DemuxerStream* stream) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (!player_bridge_) {
    return;
  }

  if (stream->type() == DemuxerStream::AUDIO) {
    const AudioDecoderConfig& decoder_config = stream->audio_decoder_config();
    player_bridge_->UpdateAudioConfig(decoder_config, stream->mime_type());
  } else {
    DCHECK_EQ(stream->type(), DemuxerStream::VIDEO);
    const VideoDecoderConfig& decoder_config = stream->video_decoder_config();

    player_bridge_->UpdateVideoConfig(decoder_config, stream->mime_type());

    // TODO(b/375275033): Refine natural size change handling.
#if 0
    bool natural_size_changed =
        (decoder_config.natural_size().width() != natural_size_.width() ||
         decoder_config.natural_size().height() != natural_size_.height());

    natural_size_ = decoder_config.natural_size();

    if (natural_size_changed) {
      content_size_change_cb_.Run();
    }
#endif  // 0
    color_space_ = decoder_config.color_space_info().ToGfxColorSpace();
    paint_video_hole_frame_cb_.Run(
        stream->video_decoder_config().visible_rect().size());
  }
}

void StarboardRenderer::ApplyPendingVideoConfig() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(pending_video_config_.has_value());
  DCHECK(video_stream_);

  LOG(INFO)
      << "Applying pending Video config change from changeType transition.";

  client_->OnVideoConfigChange(*pending_video_config_);
  client_->OnVideoNaturalSizeChange(
      pending_video_config_->visible_rect().size());
  paint_video_hole_frame_cb_.Run(pending_video_config_->visible_rect().size());

  // Re-use the existing config update logic (keeps color space and hole
  // painting intact)
  UpdateDecoderConfig(video_stream_);

  pending_video_config_.reset();
}

void StarboardRenderer::ApplyPendingAudioConfig() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(pending_audio_config_.has_value());
  DCHECK(audio_stream_);

  LOG(INFO)
      << "Applying pending Audio config change from changeType transition.";

  client_->OnAudioConfigChange(*pending_audio_config_);

  // Re-use the existing config update logic
  UpdateDecoderConfig(audio_stream_);

  pending_audio_config_.reset();
}

void StarboardRenderer::OnDemuxerStreamRead(
    DemuxerStream* stream,
    int max_buffers,
    DemuxerStream::Status status,
    DemuxerStream::DecoderBufferVector buffers) {
  if (!task_runner_->RunsTasksInCurrentSequence()) {
    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&StarboardRenderer::OnDemuxerStreamRead,
                                  weak_factory_.GetWeakPtr(), stream,
                                  max_buffers, status, buffers));
    return;
  }

  if (pending_flush_cb_) {
    if (stream == audio_stream_) {
      DCHECK(audio_read_in_progress_);
      audio_read_in_progress_ = false;
    }
    if (stream == video_stream_) {
      DCHECK(video_read_in_progress_);
      video_read_in_progress_ = false;
    }
    if (!audio_read_in_progress_ && !video_read_in_progress_) {
      auto flush_cb = std::move(pending_flush_cb_);
      state_ = STATE_FLUSHED;
      std::move(flush_cb).Run();
    }
    return;
  }

  DCHECK(player_bridge_);

  if (status == DemuxerStream::kOk) {
    if (stream == audio_stream_) {
      DCHECK(audio_read_in_progress_);
      audio_read_in_progress_ = false;
      if (pending_audio_config_.has_value()) {
        ApplyPendingAudioConfig();
      }
      for (const auto& buffer : buffers) {
        if (!buffer->end_of_stream()) {
          last_audio_sample_interval_ =
              buffer->timestamp() - timestamp_of_last_written_audio_;
          timestamp_of_last_written_audio_ = buffer->timestamp();
        }
      }
      player_bridge_->WriteBuffers(DemuxerStream::AUDIO, buffers);
    } else {
      DCHECK(video_read_in_progress_);
      video_read_in_progress_ = false;
      if (pending_video_config_.has_value()) {
        ApplyPendingVideoConfig();
      }
      for (const auto& buffer : buffers) {
        if (buffer->end_of_stream()) {
          is_video_eos_written_ = true;
        }
      }
      player_bridge_->WriteBuffers(DemuxerStream::VIDEO, buffers);
    }
  } else if (status == DemuxerStream::kAborted) {
    if (stream == audio_stream_) {
      DCHECK(audio_read_in_progress_);
      audio_read_in_progress_ = false;
    }
    if (stream == video_stream_) {
      DCHECK(video_read_in_progress_);
      video_read_in_progress_ = false;
    }
    if (pending_flush_cb_ && !audio_read_in_progress_ &&
        !video_read_in_progress_) {
      state_ = STATE_FLUSHED;
      std::move(pending_flush_cb_).Run();
    }
  } else if (status == DemuxerStream::kConfigChanged) {
    if (stream == audio_stream_) {
      const AudioDecoderConfig& config = stream->audio_decoder_config();
      if (config.is_change_type_transition()) {
        pending_audio_config_ = config;
        LOG(INFO) << "Pending Audio config change recorded. "
                     "is_change_type_transition=true";
      } else {
        pending_audio_config_.reset();
        client_->OnAudioConfigChange(config);
        UpdateDecoderConfig(stream);
      }
    } else {
      DCHECK_EQ(stream, video_stream_);
      const VideoDecoderConfig& config = stream->video_decoder_config();
      if (config.is_change_type_transition()) {
        pending_video_config_ = config;
        LOG(INFO) << "Pending Video config change recorded. "
                     "is_change_type_transition=true";
      } else {
        pending_video_config_.reset();
        client_->OnVideoConfigChange(config);
        // TODO(b/375275033): Refine calling to OnVideoNaturalSizeChange().
        client_->OnVideoNaturalSizeChange(config.visible_rect().size());
        paint_video_hole_frame_cb_.Run(config.visible_rect().size());
        UpdateDecoderConfig(stream);
      }
    }
    stream->Read(
        max_buffers,
        base::BindOnce(&StarboardRenderer::OnDemuxerStreamRead,
                       weak_factory_.GetWeakPtr(), stream, max_buffers));
  } else if (status == DemuxerStream::kError) {
    state_ = STATE_ERROR;
    NotifyError(PIPELINE_ERROR_READ);
  }
}

void StarboardRenderer::OnStatisticsUpdate(const PipelineStatistics& stats) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  client_->OnStatisticsUpdate(stats);
}

void StarboardRenderer::OnNeedData(DemuxerStream::Type type,
                                   int max_number_of_buffers_to_write) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  // In case if the callback is fired when creation of the `player_bridge_`
  // fails.
  // We may also need this for suspend/resume support.
  if (!player_bridge_) {
    LOG(INFO) << "StarboardRenderer::OnNeedData() called with " << type;
    return;
  }

  int max_buffers =
      std::min(max_number_of_buffers_to_write, max_samples_per_write_);

  if (type == DemuxerStream::AUDIO) {
    if (!audio_stream_) {
      LOG(WARNING)
          << "Calling OnNeedData() for audio data during audioless playback.";
      return;
    }

    if (audio_read_in_progress_) {
      LOG(WARNING)
          << "Calling OnNeedData() when there is an audio read in progress.";
      return;
    }

    // Delay reading audio more than |audio_write_duration_| ahead of playback
    // after the player has received enough audio for preroll, taking into
    // account that our estimate of playback time might be behind by
    // |kMediaTimeCheckInterval|.
    TimeDelta time_ahead_of_playback_for_preroll =
        timestamp_of_last_written_audio_ - seek_time_;
    auto adjusted_write_duration_for_preroll =
        AdjustWriteDurationForPlaybackRate(audio_write_duration_for_preroll_,
                                           playback_rate_);
    // Note when Cobalt uses multiple samples per write, GetDefaultMaxBuffers()
    // returns the exact number of samples computed by
    // |time_ahead_of_playback_for_preroll| and
    // |adjusted_write_duration_for_preroll|. The guard number
    // kPrerollGuardAudioBuffer is used to ensure Cobalt can do one initial
    // write for audio preroll, as preroll condition requires that
    // |time_ahead_of_playback_for_preroll| >
    // |adjusted_write_duration_for_preroll|.
    int estimated_max_buffers = max_buffers;
    if (!is_video_eos_written_ && time_ahead_of_playback_for_preroll >
                                      adjusted_write_duration_for_preroll) {
      TimeDelta time_ahead_of_playback;
      TimeDelta time_since_last_update =
          Time::Now() - last_time_media_time_retrieved_;
      // The estimated time ahead of playback may be negative if no audio has
      // been written.
      time_ahead_of_playback = timestamp_of_last_written_audio_ -
                               (last_media_time_ + time_since_last_update);

      auto adjusted_write_duration = AdjustWriteDurationForPlaybackRate(
          audio_write_duration_, playback_rate_);
      if (time_ahead_of_playback >
          (adjusted_write_duration + kMediaTimeCheckInterval)) {
        task_runner_->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(&StarboardRenderer::DelayedNeedData,
                           weak_factory_.GetWeakPtr(), max_buffers),
            kMediaTimeCheckInterval);
        audio_read_delayed_ = true;
        return;
      }
      if (max_samples_per_write_ > 1 && !time_ahead_of_playback.is_negative()) {
        estimated_max_buffers = GetEstimatedMaxBuffers(adjusted_write_duration,
                                                       time_ahead_of_playback,
                                                       false /* is_preroll */);
      }
    } else if (max_samples_per_write_ > 1) {
      if (!time_ahead_of_playback_for_preroll.is_negative()) {
        estimated_max_buffers = GetEstimatedMaxBuffers(
            adjusted_write_duration_for_preroll,
            time_ahead_of_playback_for_preroll, true /* is_preroll */);
        last_estimated_max_buffers_for_preroll_ = std::max(
            estimated_max_buffers, last_estimated_max_buffers_for_preroll_);
      } else {
        estimated_max_buffers = last_estimated_max_buffers_for_preroll_;
      }
    }
    // When Cobalt uses multiple samples per write, this ensures that
    // |max_buffers| is at most |max_number_of_buffers_to_write|.
    // |max_buffers| is in the range of [1, |max_number_of_buffers_to_write|],
    // where the lower bound 1 is guarded by GetEstimatedMaxBuffers().
    max_buffers = std::min(max_buffers, estimated_max_buffers);

    audio_read_delayed_ = false;
    audio_read_in_progress_ = true;
  } else {
    DCHECK_EQ(type, DemuxerStream::VIDEO);

    if (video_read_in_progress_) {
      LOG(WARNING)
          << "Calling OnNeedData() when there is a video read in progress.";
      return;
    }

    video_read_in_progress_ = true;
  }
  auto stream = (type == DemuxerStream::AUDIO ? audio_stream_ : video_stream_);
  DCHECK(stream);

  stream->Read(max_buffers,
               base::BindOnce(&StarboardRenderer::OnDemuxerStreamRead,
                              weak_factory_.GetWeakPtr(), stream, max_buffers));
}

void StarboardRenderer::OnPlayerStatus(SbPlayerState state) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  LOG(INFO) << "StarboardRenderer::OnPlayerStatus() called with "
            << GetPlayerStateName(state);

  // In case if state is changed when creation of the `player_bridge_` fails.
  // We may also need this for suspend/resume support.
  if (!player_bridge_) {
    LOG(WARNING) << "StarboardRenderer::OnPlayerStatus() called with invalid "
                    "SbPlayerBridge";
    return;
  }

  switch (state) {
    case kSbPlayerStateInitialized:
      CHECK(init_cb_);
      std::move(init_cb_).Run(PipelineStatus(PIPELINE_OK));
      break;
    case kSbPlayerStatePrerolling:
      break;
    case kSbPlayerStatePresenting:
      buffering_state_ = BUFFERING_HAVE_ENOUGH;
      task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(&StarboardRenderer::OnBufferingStateChange,
                         weak_factory_.GetWeakPtr(), buffering_state_));
      audio_write_duration_for_preroll_ = audio_write_duration_ =
          HasRemoteAudioOutputs(player_bridge_->GetAudioConfigurations())
              ? audio_write_duration_remote_
              : audio_write_duration_local_;
      LOG(INFO) << "Audio write duration is " << audio_write_duration_;
      break;
    case kSbPlayerStateEndOfStream:
      client_->OnEnded();
      break;
    case kSbPlayerStateDestroyed:
      break;
  }
}

void StarboardRenderer::OnPlayerError(SbPlayerError error,
                                      const std::string& message) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(ERROR) << "StarboardRenderer::OnPlayerError() called with code " << error
             << " and message \"" << message << "\"";

  // An error has already been delivered.
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

void StarboardRenderer::NotifyError(PipelineStatus status) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  // MojoRenderer in the renderer process delays assigning its `client_` pointer
  // until the initialization callback resolves. If we encounter an error during
  // initialization, we must resolve `init_cb_` with the error status instead of
  // firing an asynchronous `client_->OnError()` event. Firing
  // `client_->OnError()` before `init_cb_` is resolved will cause a null
  // pointer dereference in `MojoRenderer::OnError()`.
  if (init_cb_) {
    std::move(init_cb_).Run(status);
  } else {
    client_->OnError(status);
  }
}

void StarboardRenderer::DelayedNeedData(int max_number_of_buffers_to_write) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (audio_read_delayed_) {
    OnNeedData(DemuxerStream::AUDIO, max_number_of_buffers_to_write);
  }
}

void StarboardRenderer::StoreMediaTime(TimeDelta media_time) {
  last_media_time_ = media_time;
  last_time_media_time_retrieved_ = Time::Now();
}

#if BUILDFLAG(IS_ANDROID)
void StarboardRenderer::OnOverlayReady(AndroidOverlay* overlay) {
  // Check that the passed overlay and overlay_ point to the same object.
  DCHECK_EQ(overlay, overlay_.get());

  surface_view_ = overlay_->GetJavaSurface().obj();
  CreatePlayerBridge();
}

void StarboardRenderer::OnOverlayFailed(AndroidOverlay* overlay) {
  DCHECK_EQ(overlay, overlay_.get());
  overlay_ = nullptr;
  state_ = STATE_ERROR;
  if (init_cb_) {
    std::move(init_cb_).Run(PipelineStatus(
        DECODER_ERROR_NOT_SUPPORTED,
        "StarboardRenderer::OnOverlayFailed() failed to create a "
        "valid AndroidOverlay"));
  }
}
#endif  // BUILDFLAG(IS_ANDROID)

int StarboardRenderer::GetDefaultMaxBuffers(AudioCodec codec,
                                            TimeDelta duration_to_write,
                                            bool is_preroll) {
  // Return default maximum samples per write to speed up the initial sample
  // write, including guard number of samples per write for audio preroll.
  // The guard number kPrerollGuardAudioBuffer is used to ensure Cobalt
  // can do one initial write for audio preroll.
  int default_max_buffers = static_cast<int>(
      std::ceil(duration_to_write.InSecondsF() *
                audio_stream_->audio_decoder_config().samples_per_second() /
                GetDefaultAudioFramesPerBuffer(codec)));
  if (is_preroll) {
    default_max_buffers += kPrerollGuardAudioBuffer;
  }
  DCHECK_GT(default_max_buffers, 0);
  return default_max_buffers;
}

int StarboardRenderer::GetEstimatedMaxBuffers(TimeDelta write_duration,
                                              TimeDelta time_ahead_of_playback,
                                              bool is_preroll) {
  DCHECK_GE(time_ahead_of_playback.InMicroseconds(), 0);

  int estimated_max_buffers = 1;
  if (max_samples_per_write_ <= 1 || write_duration <= time_ahead_of_playback) {
    return estimated_max_buffers;
  }

  TimeDelta duration_to_write = write_duration - time_ahead_of_playback;
  DCHECK_GT(duration_to_write.InMicroseconds(), 0);
  switch (audio_stream_->audio_decoder_config().codec()) {
    case AudioCodec::kOpus:
    case AudioCodec::kAAC:
    case AudioCodec::kAC3:
    case AudioCodec::kEAC3:
      if (last_audio_sample_interval_.is_zero()) {
        estimated_max_buffers =
            GetDefaultMaxBuffers(audio_stream_->audio_decoder_config().codec(),
                                 duration_to_write, is_preroll);
        break;
      }
      [[fallthrough]];
    // TODO(b/41486346): Support multiple samples per write on the format IAMF.
    // case AudioCodec::kIAMF:
    default:
      if (!last_audio_sample_interval_.is_zero()) {
        DCHECK_GT(last_audio_sample_interval_.InMicroseconds(), 0);
        estimated_max_buffers =
            duration_to_write.InMillisecondsRoundedUp() /
                last_audio_sample_interval_.InMilliseconds() +
            1;
      }
  }
  DCHECK_GT(estimated_max_buffers, 0);
  // Return 1 if |estimated_max_buffers| is non-positive. This ensures
  // in corner cases, |estimated_max_buffers| falls back to 1.
  // The maximum number samples of write should be guarded by
  // SbPlayerGetMaximumNumberOfSamplesPerWrite() in OnNeedData().
  return estimated_max_buffers > 0 ? estimated_max_buffers : 1;
}

void StarboardRenderer::OnBufferingStateChange(BufferingState buffering_state) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  client_->OnBufferingStateChange(buffering_state,
                                  BUFFERING_CHANGE_REASON_UNKNOWN);
}

SbDecodeTarget StarboardRenderer::GetSbDecodeTarget() {
  // This function might be called from a different thread (e.g. GPU thread)
  // to get the decode target for rendering.
  if (player_bridge_ && player_bridge_->IsValid()) {
    CHECK(player_bridge_->GetSbPlayerOutputMode() ==
          kSbPlayerOutputModeDecodeToTexture);
    return player_bridge_->GetCurrentSbDecodeTarget();
  }
  return kSbDecodeTargetInvalid;
}

void StarboardRenderer::set_decode_target_graphics_context_provider(
    const GetDecodeTargetGraphicsContextProviderFunc&
        get_decode_target_graphics_context_provider_func) {
  get_decode_target_graphics_context_provider_func_ =
      get_decode_target_graphics_context_provider_func;
}

}  // namespace media
