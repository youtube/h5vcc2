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

#include "media/starboard/sbplayer_bridge.h"

#include <algorithm>
#include <iomanip>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "media/base/starboard/experimental_features.h"
#include "media/starboard/buildflags.h"
#include "media/starboard/starboard_utils.h"
#include "starboard/common/media.h"
#include "starboard/common/once.h"
#include "starboard/common/player.h"
#include "starboard/common/string.h"
#include "starboard/configuration.h"
#include "starboard/extension/experimental/experimental_features.h"
#include "starboard/extension/player_set_video_surface_view.h"

#if BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)
#include "cobalt/base/statistics.h"
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)
#if BUILDFLAG(COBALT_MEDIA_ENABLE_FORMAT_SUPPORT_QUERY_METRICS)
#include "cobalt/media/base/format_support_query_metrics.h"
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_FORMAT_SUPPORT_QUERY_METRICS)
#if BUILDFLAG(COBALT_MEDIA_ENABLE_PLAYER_SET_MAX_VIDEO_INPUT_SIZE)
#include "starboard/extension/player_set_max_video_input_size.h"
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_PLAYER_SET_MAX_VIDEO_INPUT_SIZE)

namespace media {

namespace {

using base::Time;
using base::TimeDelta;
using starboard::FormatString;
using starboard::GetPlayerOutputModeName;

constexpr size_t kDeallocationBatchThreshold = 16;

// Reason for existence: Guarantees automatic clearing of an underlying vector
// upon exiting a scoped execution block, preventing stale or dangling pointers.
//
// Expected lifetime and ownership: Instantiated locally on the stack within a
// function body. Destruction occurs automatically at scope exit.
//
// Threading model: Strictly single-threaded execution on the invoking thread.
template <typename T>
class ScopedVectorClearer {
 public:
  explicit ScopedVectorClearer(std::vector<T>& vec) : vec_(vec) {}

  ~ScopedVectorClearer() { vec_.clear(); }

  ScopedVectorClearer(const ScopedVectorClearer&) = delete;
  ScopedVectorClearer& operator=(const ScopedVectorClearer&) = delete;

 private:
  std::vector<T>& vec_;
};

bool TryRemoveBufferAt(SbPlayerBridge::DecodingBufferQueue* buffer_queue,
                       uint64_t* cached_bytes_decoded,
                       DecoderBuffer::Allocator::Handle handle,
                       size_t index) {
  if (buffer_queue->size() <= index ||
      (*buffer_queue)[index].handle != handle) {
    return false;
  }
  *cached_bytes_decoded += (*buffer_queue)[index].buffer->size();
  if (index == 0) {
    buffer_queue->pop_front();
  } else {
    buffer_queue->erase(buffer_queue->begin() + index);
  }
  return true;
}

bool TryRemoveBufferScan(SbPlayerBridge::DecodingBufferQueue* buffer_queue,
                         uint64_t* cached_bytes_decoded,
                         DecoderBuffer::Allocator::Handle handle,
                         size_t start_index) {
  if (buffer_queue->size() <= start_index) {
    return false;
  }
  for (size_t i = start_index; i < buffer_queue->size(); ++i) {
    if ((*buffer_queue)[i].handle == handle) {
      *cached_bytes_decoded += (*buffer_queue)[i].buffer->size();
      buffer_queue->erase(buffer_queue->begin() + i);
      return true;
    }
  }
  return false;
}

#if BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)
class StatisticsWrapper {
 public:
  static StatisticsWrapper* GetInstance();
  base::Statistics<int64_t, int, 1024> startup_latency{
      "Media.PlaybackStartupLatency"};
};
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)

void SetStreamInfo(const SbMediaAudioStreamInfo& stream_info,
                   SbMediaAudioSampleInfo* sample_info) {
  DCHECK(sample_info);

  sample_info->stream_info = stream_info;
}

void SetStreamInfo(const SbMediaVideoStreamInfo& stream_info,
                   SbMediaVideoSampleInfo* sample_info) {
  DCHECK(sample_info);
  sample_info->stream_info = stream_info;
}

void SetDiscardPadding(
    const ::media::DecoderBuffer::DiscardPadding& discard_padding,
    SbMediaAudioSampleInfo* sample_info) {
  DCHECK(sample_info);

  sample_info->discarded_duration_from_front =
      discard_padding.first.InMicroseconds();
  sample_info->discarded_duration_from_back =
      discard_padding.second.InMicroseconds();
}

std::vector<StarboardExperimentalFeatureEntry>
GetStarboardExtensionExperimentalFeatures(
    const SbPlayerBridge::ExperimentalFeatures& experimental_features) {
  std::vector<StarboardExperimentalFeatureEntry> entries;
  entries.reserve(experimental_features.settings().size());
  for (const auto& [key, val] : experimental_features.settings()) {
    if (auto* int_val = std::get_if<int64_t>(&val)) {
      entries.push_back({key.c_str(), *int_val});
    }
  }
  return entries;
}

}  // namespace

#if BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)
SB_ONCE_INITIALIZE_FUNCTION(StatisticsWrapper, StatisticsWrapper::GetInstance);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)

#if SB_HAS(PLAYER_WITH_URL)
SbPlayerBridge::SbPlayerBridge(
    SbPlayerInterface* interface,
    const scoped_refptr<base::SequencedTaskRunner>& task_runner,
    const std::string& url,
    SbWindow window,
    Host* host,
    bool allow_resume_after_suspend,
    SbPlayerOutputMode default_output_mode,
    const OnEncryptedMediaInitDataEncounteredCB&
        encrypted_media_init_data_encountered_cb
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    ,
    std::string pipeline_identifier
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    )
    : on_encrypted_media_init_data_encountered_cb_(
          encrypted_media_init_data_encountered_cb),
      url_(url),
      sbplayer_interface_(interface),
      task_runner_(task_runner),
      window_(window),
      host_(host),
#if COBALT_MEDIA_ENABLE_SUSPEND_RESUME
      allow_resume_after_suspend_(allow_resume_after_suspend),
#endif
      enable_batched_buffer_deallocation_(false),
      is_url_based_(true)
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
      ,
      cval_stats_(&interface->cval_stats_),
      pipeline_identifier_(pipeline_identifier)
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
{
  DCHECK(host_);

  output_mode_ = ComputeSbUrlPlayerOutputMode(default_output_mode);

  CreateUrlPlayer(url_);

#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  task_runner->PostTask(FROM_HERE,
                        base::BindOnce(&SbPlayerBridge::ClearDecoderBufferCache,
                                       weak_factory_.GetWeakPtr()));
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
}
#endif  // SB_HAS(PLAYER_WITH_URL)

SbPlayerBridge::SbPlayerBridge(
    SbPlayerInterface* interface,
    const scoped_refptr<base::SequencedTaskRunner>& task_runner,
    const GetDecodeTargetGraphicsContextProviderFunc&
        get_decode_target_graphics_context_provider_func,
    const AudioDecoderConfig& audio_config,
    const std::string& audio_mime_type,
    const VideoDecoderConfig& video_config,
    const std::string& video_mime_type,
    SbWindow window,
    SbDrmSystem drm_system,
    Host* host,
    bool allow_resume_after_suspend,
    SbPlayerOutputMode default_output_mode,
    const std::string& max_video_capabilities,
    int max_video_input_size,
    const ExperimentalFeatures& experimental_features
#if BUILDFLAG(IS_ANDROID)
    ,
    jobject surface_view
#endif  // BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    ,
    std::string pipeline_identifier
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    )
    : sbplayer_interface_(interface),
      task_runner_(task_runner),
      get_decode_target_graphics_context_provider_func_(
          get_decode_target_graphics_context_provider_func),
      window_(window),
      drm_system_(drm_system),
      host_(host),
#if COBALT_MEDIA_ENABLE_SUSPEND_RESUME
      allow_resume_after_suspend_(allow_resume_after_suspend),
#endif  // COBALT_MEDIA_ENABLE_SUSPEND_RESUME
      audio_config_(audio_config),
      video_config_(video_config),
      max_video_capabilities_(max_video_capabilities),
      experimental_features_(experimental_features),
      enable_batched_buffer_deallocation_(
          experimental_features_.GetBool(kMediaEnableTrivialOptimizations))
#if BUILDFLAG(COBALT_MEDIA_ENABLE_PLAYER_SET_MAX_VIDEO_INPUT_SIZE)
      ,
      max_video_input_size_(max_video_input_size)
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_PLAYER_SET_MAX_VIDEO_INPUT_SIZE)
#if BUILDFLAG(IS_ANDROID)
      ,
      surface_view_(surface_view)
#endif  // BUILDFLAG(IS_ANDROID)
#if SB_HAS(PLAYER_WITH_URL)
      ,
      is_url_based_(false)
#endif  // SB_HAS(PLAYER_WITH_URL)
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
      ,
      cval_stats_(&interface->cval_stats_),
      pipeline_identifier_(pipeline_identifier)
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
{
  DCHECK(audio_config.IsValidConfig() || video_config.IsValidConfig());
  DCHECK(host_);

  if (enable_batched_buffer_deallocation_) {
    audio_decoding_buffers_.reserve(64);
    video_decoding_buffers_.reserve(256);
    pending_deallocations_.reserve(kDeallocationBatchThreshold);
    processing_deallocations_.reserve(kDeallocationBatchThreshold);
    process_pending_deallocations_cb_ = base::BindPostTask(
        task_runner_,
        base::BindRepeating(&SbPlayerBridge::ProcessPendingBatchedDeallocations,
                            weak_factory_.GetWeakPtr()));
  }

  audio_stream_info_.codec = kSbMediaAudioCodecNone;
  video_stream_info_.codec = kSbMediaVideoCodecNone;

  if (audio_config.IsValidConfig()) {
    UpdateAudioConfig(audio_config, audio_mime_type);
  }
  if (video_config.IsValidConfig()) {
    UpdateVideoConfig(video_config, video_mime_type);
    SendColorSpaceHistogram();
  }

  output_mode_ = ComputeSbPlayerOutputMode(default_output_mode);

  CreatePlayer();

#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  if (SbPlayerIsValid(player_)) {
    task_runner->PostTask(
        FROM_HERE, base::BindOnce(&SbPlayerBridge::ClearDecoderBufferCache,
                                  weak_factory_.GetWeakPtr()));
  }
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
}

SbPlayerBridge::~SbPlayerBridge() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  weak_factory_.InvalidateWeakPtrs();

  if (SbPlayerIsValid(player_)) {
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    cval_stats_->StartTimer(MediaTiming::SbPlayerDestroy, pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    sbplayer_interface_->Destroy(player_);
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    cval_stats_->StopTimer(MediaTiming::SbPlayerDestroy, pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  }
}

void SbPlayerBridge::UpdateAudioConfig(const AudioDecoderConfig& audio_config,
                                       const std::string& mime_type) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(audio_config.IsValidConfig());

  LOG(INFO) << "Updated AudioDecoderConfig -- "
            << audio_config.AsHumanReadableString();

  audio_config_ = audio_config;
  audio_mime_type_ = mime_type;
  audio_stream_info_ = MediaAudioConfigToSbMediaAudioStreamInfo(
      audio_config_, audio_mime_type_.c_str());
  LOG(INFO) << "Converted to SbMediaAudioStreamInfo -- " << audio_stream_info_;
}

void SbPlayerBridge::UpdateVideoConfig(const VideoDecoderConfig& video_config,
                                       const std::string& mime_type) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(video_config.IsValidConfig());

  LOG(INFO) << "Updated VideoDecoderConfig -- "
            << video_config.AsHumanReadableString();

  video_config_ = video_config;
  video_stream_info_.frame_width =
      static_cast<int>(video_config_.natural_size().width());
  video_stream_info_.frame_height =
      static_cast<int>(video_config_.natural_size().height());
  video_stream_info_.codec =
      MediaVideoCodecToSbMediaVideoCodec(video_config_.codec());
  video_stream_info_.color_metadata =
      MediaToSbMediaColorMetadata(video_config_.color_space_info(),
                                  video_config_.hdr_metadata(), mime_type);
  video_mime_type_ = mime_type;
  video_stream_info_.mime = video_mime_type_.c_str();
  video_stream_info_.max_video_capabilities = max_video_capabilities_.c_str();
  LOG(INFO) << "Converted to SbMediaVideoStreamInfo -- " << video_stream_info_;
}

void SbPlayerBridge::WriteBuffers(
    DemuxerStream::Type type,
    const std::vector<scoped_refptr<DecoderBuffer>>& buffers) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
#if SB_HAS(PLAYER_WITH_URL)
  DCHECK(!is_url_based_);
#endif  // SB_HAS(PLAYER_WITH_URL)

#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  if (allow_resume_after_suspend_) {
    if (type == DemuxerStream::Type::AUDIO) {
      decoder_buffer_cache_.AddBuffers(buffers, audio_stream_info_);
    } else {
      DCHECK_EQ(type, DemuxerStream::Type::VIDEO);
      decoder_buffer_cache_.AddBuffers(buffers, video_stream_info_);
    }
    if (state_ != kSuspended) {
      WriteNextBuffersFromCache(type, buffers.size());
    }
    return;
  }
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)

  WriteBuffersInternal(type, buffers, &audio_stream_info_, &video_stream_info_);
}

void SbPlayerBridge::SetBounds(const gfx::Rect& rect) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  // On certain platforms (e.g., Linux), the Cobalt renderer draws layers from
  // back to front. A monotonically increasing sequence is used to generate a
  // unique z-index for each player, ensuring video layers are stacked
  // correctly.
  static base::AtomicSequenceNumber s_z_index;

  set_bounds_z_index_ = s_z_index.GetNext();
  set_bounds_rect_ = rect;

  if (state_ == kSuspended) {
    return;
  }

  UpdateBounds();
}

void SbPlayerBridge::PrepareForSeek() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  seek_pending_ = true;

  if (state_ == kSuspended) {
    return;
  }

  ++ticket_;
  sbplayer_interface_->SetPlaybackRate(player_, 0.f);
}

void SbPlayerBridge::Seek(TimeDelta time) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  decoder_buffer_cache_.ClearAll();
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  seek_pending_ = false;

  pending_audio_eos_buffer_ = false;
  pending_video_eos_buffer_ = false;

  if (state_ == kSuspended) {
    preroll_timestamp_ = time;
    return;
  }

  // If a seek happens during resuming, the pipeline will write samples from the
  // seek target time again so resuming can be aborted.
  if (state_ == kResuming) {
    state_ = kPlaying;
  }

  DCHECK(SbPlayerIsValid(player_));

  ++ticket_;
  sbplayer_interface_->Seek(player_, time, ticket_);
}

void SbPlayerBridge::SetVolume(float volume) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  volume_ = volume;

  if (state_ == kSuspended) {
    return;
  }

  DCHECK(SbPlayerIsValid(player_));
  sbplayer_interface_->SetVolume(player_, volume);
}

void SbPlayerBridge::SetPlaybackRate(double playback_rate) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  playback_rate_ = playback_rate;

  if (state_ == kSuspended) {
    return;
  }

  if (seek_pending_) {
    return;
  }

  sbplayer_interface_->SetPlaybackRate(player_, playback_rate);
}

std::vector<SbMediaAudioConfiguration>
SbPlayerBridge::GetAudioConfigurations() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (!SbPlayerIsValid(player_)) {
    return std::vector<SbMediaAudioConfiguration>();
  }

  std::vector<SbMediaAudioConfiguration> configurations;

  // Set a limit to avoid infinite loop.
  constexpr int kMaxAudioConfigurations = 32;

  for (int i = 0; i < kMaxAudioConfigurations; ++i) {
    SbMediaAudioConfiguration configuration;
    if (!sbplayer_interface_->GetAudioConfiguration(player_, i,
                                                    &configuration)) {
      break;
    }

    configurations.push_back(configuration);
  }

  LOG_IF(WARNING, configurations.empty())
      << "Failed to find any audio configurations.";

  return configurations;
}

#if SB_HAS(PLAYER_WITH_URL)
void SbPlayerBridge::GetUrlPlayerBufferedTimeRanges(
    TimeDelta* buffer_start_time,
    TimeDelta* buffer_length_time) {
  DCHECK(buffer_start_time || buffer_length_time);
  DCHECK(is_url_based_);

  if (state_ == kSuspended) {
    *buffer_start_time = TimeDelta();
    *buffer_length_time = TimeDelta();
    return;
  }

  DCHECK(SbPlayerIsValid(player_));

  SbUrlPlayerExtraInfo url_player_info;
  sbplayer_interface_->GetUrlPlayerExtraInfo(player_, &url_player_info);

  if (buffer_start_time) {
    *buffer_start_time =
        base::Microseconds(url_player_info.buffer_start_timestamp);
  }
  if (buffer_length_time) {
    *buffer_length_time = base::Microseconds(url_player_info.buffer_duration);
  }
}

void SbPlayerBridge::GetVideoResolution(int* frame_width, int* frame_height) {
  DCHECK(frame_width);
  DCHECK(frame_height);
  DCHECK(is_url_based_);

  if (state_ == kSuspended) {
    *frame_width = video_stream_info_.frame_width;
    *frame_height = video_stream_info_.frame_height;
    return;
  }

  DCHECK(SbPlayerIsValid(player_));

  SbPlayerInfo out_player_info;
  sbplayer_interface_->GetInfo(player_, &out_player_info);

  video_stream_info_.frame_width = out_player_info.frame_width;
  video_stream_info_.frame_height = out_player_info.frame_height;

  *frame_width = video_stream_info_.frame_width;
  *frame_height = video_stream_info_.frame_height;
}

TimeDelta SbPlayerBridge::GetDuration() {
  DCHECK(is_url_based_);

  if (state_ == kSuspended) {
    return TimeDelta();
  }

  DCHECK(SbPlayerIsValid(player_));

  SbPlayerInfo info;
  sbplayer_interface_->GetInfo(player_, &info);
  if (info.duration == SB_PLAYER_NO_DURATION) {
    // URL-based player may not have loaded asset yet, so map no duration to 0.
    return TimeDelta();
  }
  return base::Microseconds(info.duration);
}

TimeDelta SbPlayerBridge::GetStartDate() {
  DCHECK(is_url_based_);

  if (state_ == kSuspended) {
    return TimeDelta();
  }

  DCHECK(SbPlayerIsValid(player_));

  SbPlayerInfo info;
  sbplayer_interface_->GetInfo(player_, &info);
  return base::Microseconds(info.start_date);
}

void SbPlayerBridge::SetDrmSystem(SbDrmSystem drm_system) {
  DCHECK(is_url_based_);

  drm_system_ = drm_system;
  sbplayer_interface_->SetUrlPlayerDrmSystem(player_, drm_system);
}
#endif  // SB_HAS(PLAYER_WITH_URL)

void SbPlayerBridge::Suspend() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  // Check if the player is already suspended.
  if (state_ == kSuspended) {
    return;
  }

  DCHECK(SbPlayerIsValid(player_));

  sbplayer_interface_->SetPlaybackRate(player_, 0.0);

  PlayerInfo info{&cached_video_frames_decoded_, &cached_video_frames_dropped_,
                  nullptr, nullptr, &preroll_timestamp_};
  GetInfo(&info);

  state_ = kSuspended;

#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  cval_stats_->StartTimer(MediaTiming::SbPlayerDestroy, pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  sbplayer_interface_->Destroy(player_);
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  cval_stats_->StopTimer(MediaTiming::SbPlayerDestroy, pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)

  player_ = kSbPlayerInvalid;
}

void SbPlayerBridge::Resume(SbWindow window) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  window_ = window;

  // Check if the player is already resumed.
  if (state_ != kSuspended) {
    DCHECK(SbPlayerIsValid(player_));
    return;
  }

#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  decoder_buffer_cache_.StartResuming();
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)

#if SB_HAS(PLAYER_WITH_URL)
  if (is_url_based_) {
    CreateUrlPlayer(url_);
    if (SbDrmSystemIsValid(drm_system_)) {
      sbplayer_interface_->SetUrlPlayerDrmSystem(player_, drm_system_);
    }
  } else {
    CreatePlayer();
  }
#else   // SB_HAS(PLAYER_WITH_URL)
  CreatePlayer();
#endif  // SB_HAS(PLAYER_WITH_URL)

  if (SbPlayerIsValid(player_)) {
    state_ = kResuming;
    UpdateBounds();
  }
}

#if SB_HAS(PLAYER_WITH_URL)
// static
void SbPlayerBridge::EncryptedMediaInitDataEncounteredCB(
    SbPlayer player,
    void* context,
    const char* init_data_type,
    const unsigned char* init_data,
    unsigned int init_data_length) {
  SbPlayerBridge* sbplayer_bridge = static_cast<SbPlayerBridge*>(context);
  DCHECK(
      !sbplayer_bridge->on_encrypted_media_init_data_encountered_cb_.is_null());
  sbplayer_bridge->on_encrypted_media_init_data_encountered_cb_.Run(
      init_data_type, init_data, init_data_length);
}

void SbPlayerBridge::CreateUrlPlayer(const std::string& url) {
  TRACE_EVENT0("media", "SbPlayerBridge::CreateUrlPlayer");
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  DCHECK(!on_encrypted_media_init_data_encountered_cb_.is_null());
  LOG(INFO) << "CreateUrlPlayer passed url " << url;

#if BUILDFLAG(COBALT_MEDIA_ENABLE_FORMAT_SUPPORT_QUERY_METRICS)
  if (max_video_capabilities_.empty()) {
    FormatSupportQueryMetrics::PrintAndResetMetrics();
  }
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_FORMAT_SUPPORT_QUERY_METRICS)

  player_creation_time_ = Time::Now();

#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  cval_stats_->StartTimer(MediaTiming::SbPlayerCreate, pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  player_ = sbplayer_interface_->CreateUrlPlayer(
      url.c_str(), window_, &SbPlayerBridge::PlayerStatusCB,
      &SbPlayerBridge::EncryptedMediaInitDataEncounteredCB,
      &SbPlayerBridge::PlayerErrorCB, this);
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  cval_stats_->StopTimer(MediaTiming::SbPlayerCreate, pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  DCHECK(SbPlayerIsValid(player_));

  if (output_mode_ == kSbPlayerOutputModeDecodeToTexture) {
    LOG(INFO) << "Playing in decode-to-texture mode.";
  } else {
    LOG(INFO) << "Playing in punch-out mode.";
  }

  UpdateBounds();
}
#endif  // SB_HAS(PLAYER_WITH_URL)

void SbPlayerBridge::CreatePlayer() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  TRACE_EVENT0("media", "SbPlayerBridge::CreatePlayer");

#if BUILDFLAG(COBALT_MEDIA_ENABLE_BACKGROUND_MODE)
  bool is_visible = SbWindowIsValid(window_);
#else   // BUILDFLAG(COBALT_MEDIA_ENABLE_BACKGROUND_MODE)
  bool is_visible = true;
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_BACKGROUND_MODE)

  is_creating_player_ = true;

  if (output_mode_ == kSbPlayerOutputModeInvalid) {
    PlayerErrorCB(kSbPlayerInvalid, this, kSbPlayerErrorDecode,
                  "Invalid output mode returned by "
                  "SbPlayerBridge::ComputeSbPlayerOutputMode()");
    is_creating_player_ = false;
    player_ = kSbPlayerInvalid;
    return;
  }

#if BUILDFLAG(COBALT_MEDIA_ENABLE_FORMAT_SUPPORT_QUERY_METRICS)
  if (max_video_capabilities_.empty()) {
    FormatSupportQueryMetrics::PrintAndResetMetrics();
  }
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_FORMAT_SUPPORT_QUERY_METRICS)

  player_creation_time_ = Time::Now();

  SbPlayerCreationParam creation_param = {};
  creation_param.drm_system = drm_system_;
  creation_param.audio_stream_info = audio_stream_info_;
  creation_param.video_stream_info = video_stream_info_;

  // TODO: This is temporary for supporting background media playback.
  //       Need to be removed with media refactor.
  if (!is_visible) {
    creation_param.video_stream_info.codec = kSbMediaVideoCodecNone;
  }
  creation_param.output_mode = output_mode_;
  DCHECK_EQ(sbplayer_interface_->GetPreferredOutputMode(&creation_param),
            output_mode_);
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  cval_stats_->StartTimer(MediaTiming::SbPlayerCreate, pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
#if BUILDFLAG(COBALT_MEDIA_ENABLE_PLAYER_SET_MAX_VIDEO_INPUT_SIZE)
  const StarboardExtensionPlayerSetMaxVideoInputSizeApi*
      player_set_max_video_input_size_extension =
          static_cast<const StarboardExtensionPlayerSetMaxVideoInputSizeApi*>(
              SbSystemGetExtension(
                  kStarboardExtensionPlayerSetMaxVideoInputSizeName));
  if (player_set_max_video_input_size_extension &&
      strcmp(player_set_max_video_input_size_extension->name,
             kStarboardExtensionPlayerSetMaxVideoInputSizeName) == 0 &&
      player_set_max_video_input_size_extension->version >= 1) {
    player_set_max_video_input_size_extension
        ->SetMaxVideoInputSizeForCurrentThread(max_video_input_size_);
  }
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_PLAYER_SET_MAX_VIDEO_INPUT_SIZE)
#if BUILDFLAG(IS_ANDROID)
  const StarboardExtensionPlayerSetVideoSurfaceViewApi*
      player_set_video_surface_view_extension =
          static_cast<const StarboardExtensionPlayerSetVideoSurfaceViewApi*>(
              SbSystemGetExtension(
                  kStarboardExtensionPlayerSetVideoSurfaceViewName));
  if (player_set_video_surface_view_extension &&
      strcmp(player_set_video_surface_view_extension->name,
             kStarboardExtensionPlayerSetVideoSurfaceViewName) == 0 &&
      player_set_video_surface_view_extension->version >= 1) {
    player_set_video_surface_view_extension
        ->SetVideoSurfaceViewForCurrentThread(surface_view_);
  }
#endif  // BUILDFLAG(IS_ANDROID)

  const StarboardExtensionExperimentalFeaturesConfigurationApi*
      experimental_features_extension = static_cast<
          const StarboardExtensionExperimentalFeaturesConfigurationApi*>(
          SbSystemGetExtension(
              kStarboardExtensionExperimentalFeaturesConfigurationName));
  if (experimental_features_extension &&
      strcmp(experimental_features_extension->name,
             kStarboardExtensionExperimentalFeaturesConfigurationName) == 0 &&
      experimental_features_extension->version >= 1) {
    const auto entries =
        GetStarboardExtensionExperimentalFeatures(experimental_features_);
    StarboardExtensionExperimentalFeatures extension_features{entries.data(),
                                                              entries.size()};
    experimental_features_extension->SetExperimentalFeaturesForCurrentThread(
        &extension_features);
  }

  const bool should_get_decode_target_graphics_context_provider =
      output_mode_ == kSbPlayerOutputModeDecodeToTexture ||
      experimental_features_.GetBool(kMediaForceClearSurfaceView);

  player_ = sbplayer_interface_->Create(
      window_, &creation_param, &SbPlayerBridge::DeallocateSampleCB,
      &SbPlayerBridge::DecoderStatusCB, &SbPlayerBridge::PlayerStatusCB,
      &SbPlayerBridge::PlayerErrorCB, this,
      should_get_decode_target_graphics_context_provider
          ? get_decode_target_graphics_context_provider_func_.Run()
          : nullptr);
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  cval_stats_->StopTimer(MediaTiming::SbPlayerCreate, pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)

  is_creating_player_ = false;

  if (!SbPlayerIsValid(player_)) {
    return;
  }

  if (output_mode_ == kSbPlayerOutputModeDecodeToTexture) {
    LOG(INFO) << "Playing in decode-to-texture mode.";
  } else {
    LOG(INFO) << "Playing in punch-out mode.";
  }

  UpdateBounds();
}

#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
void SbPlayerBridge::WriteNextBuffersFromCache(DemuxerStream::Type type,
                                               int max_buffers_per_write) {
  DCHECK(state_ != kSuspended);
#if SB_HAS(PLAYER_WITH_URL)
  DCHECK(!is_url_based_);
#endif  // SB_HAS(PLAYER_WITH_URL)

  DCHECK(SbPlayerIsValid(player_));

  if (type == DemuxerStream::AUDIO) {
    std::vector<scoped_refptr<DecoderBuffer>> buffers;
    SbMediaAudioStreamInfo stream_info;
    decoder_buffer_cache_.ReadBuffers(&buffers, max_buffers_per_write,
                                      &stream_info);
    if (buffers.size() > 0) {
      WriteBuffersInternal(type, buffers, &stream_info, nullptr);
    }
  } else {
    DCHECK_EQ(type, DemuxerStream::VIDEO);
    std::vector<scoped_refptr<DecoderBuffer>> buffers;
    SbMediaVideoStreamInfo stream_info;
    decoder_buffer_cache_.ReadBuffers(&buffers, max_buffers_per_write,
                                      &stream_info);
    if (buffers.size() > 0) {
      WriteBuffersInternal(type, buffers, nullptr, &stream_info);
    }
  }
}
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)

void SbPlayerBridge::WriteBuffersInternal(
    DemuxerStream::Type type,
    const std::vector<scoped_refptr<DecoderBuffer>>& buffers,
    const SbMediaAudioStreamInfo* audio_stream_info,
    const SbMediaVideoStreamInfo* video_stream_info) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
#if SB_HAS(PLAYER_WITH_URL)
  DCHECK(!is_url_based_);
#endif  // SB_HAS(PLAYER_WITH_URL)

  auto sample_type = DemuxerStreamTypeToSbMediaType(type);
  if (buffers.size() == 1 && buffers[0]->end_of_stream()) {
    sbplayer_interface_->WriteEndOfStream(player_,
                                          DemuxerStreamTypeToSbMediaType(type));
    return;
  }

  const bool enable_trivial_optimizations =
      experimental_features_.GetBool(kMediaEnableTrivialOptimizations);

  std::vector<SbPlayerSampleInfo> local_sample_infos;
  std::vector<SbDrmSampleInfo> local_drm_infos;
  std::vector<SbDrmSubSampleMapping> local_subsample_mappings;
  std::vector<SbPlayerSampleSideData> local_side_data;

  std::vector<SbPlayerSampleInfo>& gathered_sbplayer_sample_infos =
      enable_trivial_optimizations ? gathered_sbplayer_sample_infos_
                                   : local_sample_infos;
  std::vector<SbDrmSampleInfo>& gathered_sbplayer_sample_infos_drm_info =
      enable_trivial_optimizations ? gathered_sbplayer_sample_infos_drm_info_
                                   : local_drm_infos;
  std::vector<SbDrmSubSampleMapping>&
      gathered_sbplayer_sample_infos_subsample_mapping =
          enable_trivial_optimizations
              ? gathered_sbplayer_sample_infos_subsample_mapping_
              : local_subsample_mappings;
  std::vector<SbPlayerSampleSideData>&
      gathered_sbplayer_sample_infos_side_data =
          enable_trivial_optimizations
              ? gathered_sbplayer_sample_infos_side_data_
              : local_side_data;

  ScopedVectorClearer<SbPlayerSampleInfo> clearer_info{
      gathered_sbplayer_sample_infos};
  ScopedVectorClearer<SbDrmSampleInfo> clearer_drm{
      gathered_sbplayer_sample_infos_drm_info};
  ScopedVectorClearer<SbDrmSubSampleMapping> clearer_mapping{
      gathered_sbplayer_sample_infos_subsample_mapping};
  ScopedVectorClearer<SbPlayerSampleSideData> clearer_side_data{
      gathered_sbplayer_sample_infos_side_data};

  gathered_sbplayer_sample_infos.reserve(buffers.size());
  gathered_sbplayer_sample_infos_drm_info.reserve(buffers.size());
  gathered_sbplayer_sample_infos_subsample_mapping.reserve(buffers.size());
  gathered_sbplayer_sample_infos_side_data.reserve(buffers.size());

  for (int i = 0; i < static_cast<int>(buffers.size()); i++) {
    const auto& buffer = buffers[i];
    if (buffer->end_of_stream()) {
      DCHECK_EQ(static_cast<size_t>(i), buffers.size() - 1);
      if (type == DemuxerStream::AUDIO) {
        pending_audio_eos_buffer_ = true;
      } else {
        pending_video_eos_buffer_ = true;
      }
      break;
    }

    if (enable_batched_buffer_deallocation_) {
      if (sample_type == kSbMediaTypeAudio) {
        audio_decoding_buffers_.push_back({buffer->handle(), buffer});
      } else {
        video_decoding_buffers_.push_back({buffer->handle(), buffer});
      }
    } else {
      if (auto [iter, inserted] = decoding_buffers_.try_emplace(
              buffer->handle(), buffer, /*usage_count=*/1, sample_type);
          !inserted) {
        ++iter->second.usage_count;
      }
    }

    if (sample_type == kSbMediaTypeAudio &&
        first_audio_sample_time_.is_null()) {
      first_audio_sample_time_ = Time::Now();
    } else if (sample_type == kSbMediaTypeVideo &&
               first_video_sample_time_.is_null()) {
      first_video_sample_time_ = Time::Now();
    }

    gathered_sbplayer_sample_infos_drm_info.push_back(SbDrmSampleInfo());
    SbDrmSampleInfo* drm_info = &gathered_sbplayer_sample_infos_drm_info[i];

    gathered_sbplayer_sample_infos_subsample_mapping.push_back(
        SbDrmSubSampleMapping());
    SbDrmSubSampleMapping* subsample_mapping =
        &gathered_sbplayer_sample_infos_subsample_mapping[i];

    drm_info->subsample_count = 0;
    if (buffer->decrypt_config()) {
      FillDrmSampleInfo(buffer, drm_info, subsample_mapping);
    }

    gathered_sbplayer_sample_infos_side_data.push_back(
        SbPlayerSampleSideData());
    SbPlayerSampleSideData* side_data =
        &gathered_sbplayer_sample_infos_side_data[i];

    SbPlayerSampleInfo sample_info = {};
    sample_info.type = sample_type;
    // Cast the handle to void* to reuse the existing SbPlayerWriteSamples().
    sample_info.buffer = reinterpret_cast<void*>(buffer->handle());
    sample_info.buffer_size = buffer->size();
    sample_info.timestamp = buffer->timestamp().InMicroseconds();

    if (buffer->side_data() && !buffer->side_data()->alpha_data.empty()) {
      // We only support at most one side data currently.
      side_data->type = kMatroskaBlockAdditional;
      side_data->data = buffer->side_data()->alpha_data.data();
      side_data->size = buffer->side_data()->alpha_data.size();
      sample_info.side_data = side_data;
      sample_info.side_data_count = 1;
    }

    if (sample_type == kSbMediaTypeAudio) {
      DCHECK(audio_stream_info);
      SetStreamInfo(*audio_stream_info, &sample_info.audio_sample_info);
      SetDiscardPadding(buffer->discard_padding(),
                        &sample_info.audio_sample_info);
    } else {
      DCHECK_EQ(sample_type, kSbMediaTypeVideo);
      DCHECK(video_stream_info);
      SetStreamInfo(*video_stream_info, &sample_info.video_sample_info);
      sample_info.video_sample_info.is_key_frame = buffer->is_key_frame();
    }
    if (drm_info->subsample_count > 0) {
      sample_info.drm_info = drm_info;
    } else {
      sample_info.drm_info = NULL;
    }
    gathered_sbplayer_sample_infos.push_back(sample_info);
  }

  if (!gathered_sbplayer_sample_infos.empty()) {
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    cval_stats_->StartTimer(MediaTiming::SbPlayerWriteSamples,
                            pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    sbplayer_interface_->WriteSamples(player_, sample_type,
                                      gathered_sbplayer_sample_infos.data(),
                                      gathered_sbplayer_sample_infos.size());
#if BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
    cval_stats_->StopTimer(MediaTiming::SbPlayerWriteSamples,
                           pipeline_identifier_);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_CVAL)
  }
}

SbDecodeTarget SbPlayerBridge::GetCurrentSbDecodeTarget() {
  return sbplayer_interface_->GetCurrentFrame(player_);
}

SbPlayerOutputMode SbPlayerBridge::GetSbPlayerOutputMode() {
  return output_mode_;
}

void SbPlayerBridge::GetInfo(PlayerInfo* out_info) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(out_info);
  DCHECK(out_info->video_frames_decoded || out_info->video_frames_dropped ||
         out_info->media_time);

  if (state_ == kSuspended) {
    if (out_info->video_frames_decoded) {
      *out_info->video_frames_decoded = cached_video_frames_decoded_;
    }
    if (out_info->video_frames_dropped) {
      *out_info->video_frames_dropped = cached_video_frames_dropped_;
    }
    if (out_info->media_time) {
      *out_info->media_time = preroll_timestamp_;
    }
  } else {
    DCHECK(SbPlayerIsValid(player_));

    SbPlayerInfo info;
    sbplayer_interface_->GetInfo(player_, &info);

    if (out_info->media_time) {
      *out_info->media_time = base::Microseconds(info.current_media_timestamp);
    }
    if (out_info->video_frames_decoded) {
      *out_info->video_frames_decoded = info.total_video_frames;
    }
    if (out_info->video_frames_dropped) {
      *out_info->video_frames_dropped = info.dropped_video_frames;
    }
  }

  if (out_info->audio_bytes_decoded) {
    *out_info->audio_bytes_decoded = cached_audio_bytes_decoded_;
  }
  if (out_info->video_bytes_decoded) {
    *out_info->video_bytes_decoded = cached_video_bytes_decoded_;
  }
}

void SbPlayerBridge::UpdateBounds() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(SbPlayerIsValid(player_));

  if (!set_bounds_z_index_ || !set_bounds_rect_) {
    return;
  }

  auto& rect = *set_bounds_rect_;
  sbplayer_interface_->SetBounds(player_, *set_bounds_z_index_, rect.x(),
                                 rect.y(), rect.width(), rect.height());
}

void SbPlayerBridge::ClearDecoderBufferCache() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  if (state_ != kResuming) {
    TimeDelta media_time;
    PlayerInfo info{NULL, NULL, NULL, NULL, &media_time};
    GetInfo(&info);
    decoder_buffer_cache_.ClearSegmentsBeforeMediaTime(media_time);
  }

  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SbPlayerBridge::ClearDecoderBufferCache,
                     weak_factory_.GetWeakPtr()),
      TimeDelta::FromMilliseconds(kClearDecoderCacheIntervalInMilliseconds));
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
}

void SbPlayerBridge::OnDecoderStatus(SbPlayer player,
                                     SbMediaType type,
                                     SbPlayerDecoderState state,
                                     int ticket) {
#if SB_HAS(PLAYER_WITH_URL)
  DCHECK(!is_url_based_);
#endif  // SB_HAS(PLAYER_WITH_URL)
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (player_ != player || ticket != ticket_) {
    return;
  }

  DCHECK_NE(state_, kSuspended);

  switch (state) {
    case kSbPlayerDecoderStateNeedsData:
      break;
  }

  DemuxerStream::Type stream_type =
      ::media::SbMediaTypeToDemuxerStreamType(type);

  if (stream_type == DemuxerStream::AUDIO && pending_audio_eos_buffer_) {
    SbPlayerWriteEndOfStream(player_, type);
    pending_audio_eos_buffer_ = false;
    return;
  } else if (stream_type == DemuxerStream::VIDEO && pending_video_eos_buffer_) {
    SbPlayerWriteEndOfStream(player_, type);
    pending_video_eos_buffer_ = false;
    return;
  }

  auto max_number_of_samples_to_write =
      SbPlayerGetMaximumNumberOfSamplesPerWrite(player_, type);
  if (state_ == kResuming) {
#if BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
    if (decoder_buffer_cache_.HasMoreBuffers(stream_type)) {
      WriteNextBuffersFromCache(stream_type, max_number_of_samples_to_write);
      return;
    }
    if (!decoder_buffer_cache_.HasMoreBuffers(DemuxerStream::AUDIO) &&
        !decoder_buffer_cache_.HasMoreBuffers(DemuxerStream::VIDEO)) {
      state_ = kPlaying;
    }
#else   // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
    state_ = kPlaying;
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_SUSPEND_RESUME)
  }
  host_->OnNeedData(stream_type, max_number_of_samples_to_write);
}

void SbPlayerBridge::OnPlayerStatus(SbPlayer player,
                                    SbPlayerState state,
                                    int ticket) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  TRACE_EVENT1("media", "SbPlayerBridge::OnPlayerStatus", "state", state);

  if (player_ != player) {
    return;
  }

  DCHECK_NE(state_, kSuspended);

  if (ticket != SB_PLAYER_INITIAL_TICKET && ticket != ticket_) {
    return;
  }

  if (state == kSbPlayerStateInitialized) {
    if (ticket_ == SB_PLAYER_INITIAL_TICKET) {
      ++ticket_;
    }
    if (sb_player_state_initialized_time_.is_null()) {
      sb_player_state_initialized_time_ = Time::Now();
    }
    // TODO(b/326497953): revisit if we need to call Seek() here once we support
    // suspend and resume.
    PrepareForSeek();
  } else if (state == kSbPlayerStatePrerolling &&
             sb_player_state_prerolling_time_.is_null()) {
    sb_player_state_prerolling_time_ = Time::Now();
  } else if (state == kSbPlayerStatePresenting &&
             sb_player_state_presenting_time_.is_null()) {
    sb_player_state_presenting_time_ = Time::Now();
#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
    LogStartupLatency();
#endif  // !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  }

  if (enable_batched_buffer_deallocation_ &&
      (state == kSbPlayerStatePrerolling ||
       state == kSbPlayerStateEndOfStream)) {
    {
      base::AutoLock auto_lock(pending_deallocations_lock_);
      if (state == kSbPlayerStatePrerolling) {
        // Re-enable batch deallocations for subsequent playback after previous
        // completion (e.g., reaching EOS).
        batch_deallocations_enabled_ = true;
      } else {
        // Immediate flush is necessary upon reaching EndOfStream to ensure the
        // accuracy of decoded byte statistics.
        batch_deallocations_enabled_ = false;
      }
    }
    ProcessPendingBatchedDeallocations();
  }

  host_->OnPlayerStatus(state);
}

void SbPlayerBridge::OnPlayerError(SbPlayer player,
                                   SbPlayerError error,
                                   const std::string& message) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (player_ != player) {
    return;
  }
  host_->OnPlayerError(error, message);
}

void SbPlayerBridge::OnDeallocateSample(const void* sample_buffer) {
#if SB_HAS(PLAYER_WITH_URL)
  DCHECK(!is_url_based_);
#endif  // SB_HAS(PLAYER_WITH_URL)
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (enable_batched_buffer_deallocation_) {
    auto handle =
        reinterpret_cast<DecoderBuffer::Allocator::Handle>(sample_buffer);

    // Check index 0 and 1 of audio and video queues first (empirical profile
    // confirms index 0 covers ~96% and index 1 covers ~4%, so both cover ~100%
    // of the cases).
    if (TryRemoveBufferAt(&audio_decoding_buffers_,
                          &cached_audio_bytes_decoded_, handle, 0) ||
        TryRemoveBufferAt(&video_decoding_buffers_,
                          &cached_video_bytes_decoded_, handle, 0) ||
        TryRemoveBufferAt(&audio_decoding_buffers_,
                          &cached_audio_bytes_decoded_, handle, 1) ||
        TryRemoveBufferAt(&video_decoding_buffers_,
                          &cached_video_bytes_decoded_, handle, 1)) {
      return;
    }

    // Fallback scan for extreme exceptions (index >= 2)
    if (TryRemoveBufferScan(&audio_decoding_buffers_,
                            &cached_audio_bytes_decoded_, handle, 2) ||
        TryRemoveBufferScan(&video_decoding_buffers_,
                            &cached_video_bytes_decoded_, handle, 2)) {
      return;
    }

    LOG(ERROR) << "SbPlayerBridge::OnDeallocateSample encounters unknown "
               << "sample_buffer " << sample_buffer;
    NOTREACHED();
    return;
  }

  DecodingBuffers::iterator iter = decoding_buffers_.find(
      reinterpret_cast<DecoderBuffer::Allocator::Handle>(sample_buffer));
  DCHECK(iter != decoding_buffers_.end());
  if (iter == decoding_buffers_.end()) {
    LOG(ERROR) << "SbPlayerBridge::OnDeallocateSample encounters unknown "
               << "sample_buffer " << sample_buffer;
    return;
  }

  DecodingBuffer& decoding_buffer = iter->second;
  if (decoding_buffer.type == kSbMediaTypeAudio) {
    cached_audio_bytes_decoded_ += decoding_buffer.buffer->size();
  } else {
    cached_video_bytes_decoded_ += decoding_buffer.buffer->size();
  }
  --decoding_buffer.usage_count;
  DCHECK_GE(decoding_buffer.usage_count, 0);
  if (decoding_buffer.usage_count == 0) {
    decoding_buffers_.erase(iter);
  }
}

bool SbPlayerBridge::TryToSetPlayerCreationErrorMessage(
    const std::string& message) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (is_creating_player_) {
    player_creation_error_message_ = message;
    return true;
  }
  LOG(INFO) << "TryToSetPlayerCreationErrorMessage() "
               "is called when |is_creating_player_| "
               "is false. Error message is ignored.";
  return false;
}

void SbPlayerBridge::OnDecoderStatusTask(void* player,
                                         SbMediaType type,
                                         SbPlayerDecoderState state,
                                         int ticket) {
  OnDecoderStatus(static_cast<SbPlayer>(player), type, state, ticket);
}

// static
void SbPlayerBridge::DecoderStatusCB(SbPlayer player,
                                     void* context,
                                     SbMediaType type,
                                     SbPlayerDecoderState state,
                                     int ticket) {
  SbPlayerBridge* sbplayer_bridge = static_cast<SbPlayerBridge*>(context);
  sbplayer_bridge->task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SbPlayerBridge::OnDecoderStatusTask,
                     sbplayer_bridge->weak_factory_.GetWeakPtr(),
                     // SAFETY: The `player` handle refers to `player_`, a
                     // member of SbPlayerBridge. Its validity is guaranteed by
                     // the WeakPtr check for SbPlayerBridge's lifetime.
                     static_cast<void*>(player), type, state, ticket));
}

void SbPlayerBridge::OnPlayerStatusTask(void* player,
                                        SbPlayerState state,
                                        int ticket) {
  OnPlayerStatus(static_cast<SbPlayer>(player), state, ticket);
}

// static
void SbPlayerBridge::PlayerStatusCB(SbPlayer player,
                                    void* context,
                                    SbPlayerState state,
                                    int ticket) {
  SbPlayerBridge* sbplayer_bridge = static_cast<SbPlayerBridge*>(context);
  sbplayer_bridge->task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SbPlayerBridge::OnPlayerStatusTask,
                     sbplayer_bridge->weak_factory_.GetWeakPtr(),
                     // SAFETY: The `player` handle refers to `player_`, a
                     // member of SbPlayerBridge. Its validity is guaranteed by
                     // the WeakPtr check for SbPlayerBridge's lifetime.
                     static_cast<void*>(player), state, ticket));
}

void SbPlayerBridge::OnPlayerErrorTask(void* player,
                                       SbPlayerError error,
                                       const std::string& message) {
  OnPlayerError(static_cast<SbPlayer>(player), error, message);
}

// static
void SbPlayerBridge::PlayerErrorCB(SbPlayer player,
                                   void* context,
                                   SbPlayerError error,
                                   const char* message) {
  SbPlayerBridge* sbplayer_bridge = static_cast<SbPlayerBridge*>(context);
  if (player == kSbPlayerInvalid) {
    // TODO: Simplify by combining the functionality of
    // TryToSetPlayerCreationErrorMessage() with OnPlayerError().
    if (sbplayer_bridge->TryToSetPlayerCreationErrorMessage(
            std::string(message ? message : ""))) {
      return;
    }
  }
  sbplayer_bridge->task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SbPlayerBridge::OnPlayerErrorTask,
                     sbplayer_bridge->weak_factory_.GetWeakPtr(),
                     // SAFETY: The `player` handle refers to `player_`, a
                     // member of SbPlayerBridge. Its validity is guaranteed by
                     // the WeakPtr check for SbPlayerBridge's lifetime.
                     static_cast<void*>(player), error,
                     message ? std::string(message) : ""));
}

void SbPlayerBridge::OnDeallocateSampleTask(const void* sample_buffer) {
  OnDeallocateSample(sample_buffer);
}

void SbPlayerBridge::ProcessPendingBatchedDeallocations() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(enable_batched_buffer_deallocation_);
  DCHECK(processing_deallocations_.empty());

  {
    base::AutoLock auto_lock(pending_deallocations_lock_);
    if (pending_deallocations_.empty()) {
      deallocation_task_pending_ = false;
      return;
    }
    processing_deallocations_.swap(pending_deallocations_);
    deallocation_task_pending_ = false;
  }

  for (const void* sample_buffer : processing_deallocations_) {
    OnDeallocateSample(sample_buffer);
  }
  processing_deallocations_.clear();
}

void SbPlayerBridge::EnqueueSampleForBatchedDeallocation(
    const void* sample_buffer) {
  DCHECK(enable_batched_buffer_deallocation_);

  // TODO(b/514758473): Experiment whether we should free the buffer
  // immediately in this function instead of posting a task.
  {
    base::AutoLock auto_lock(pending_deallocations_lock_);
    pending_deallocations_.push_back(sample_buffer);
    if (deallocation_task_pending_) {
      return;
    }
    if (batch_deallocations_enabled_ &&
        pending_deallocations_.size() < kDeallocationBatchThreshold) {
      return;
    }
    deallocation_task_pending_ = true;
  }
  process_pending_deallocations_cb_.Run();
}

// static
void SbPlayerBridge::DeallocateSampleCB(SbPlayer player,
                                        void* context,
                                        const void* sample_buffer) {
  SbPlayerBridge* sbplayer_bridge = static_cast<SbPlayerBridge*>(context);

  if (sbplayer_bridge->enable_batched_buffer_deallocation_) {
    sbplayer_bridge->EnqueueSampleForBatchedDeallocation(sample_buffer);
    return;
  }

  sbplayer_bridge->task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &SbPlayerBridge::OnDeallocateSampleTask,
          sbplayer_bridge->weak_factory_.GetWeakPtr(),
          // SAFETY: `sample_buffer` points to memory owned by
          // `decoding_buffers_`, a member of SbPlayerBridge. Its validity is
          // guaranteed by the WeakPtr check for SbPlayerBridge's lifetime.
          sample_buffer));
}

#if SB_HAS(PLAYER_WITH_URL)
SbPlayerOutputMode SbPlayerBridge::ComputeSbUrlPlayerOutputMode(
    SbPlayerOutputMode default_output_mode) {
  // Try to choose the output mode according to the passed in value of
  // |prefer_decode_to_texture|.  If the preferred output mode is unavailable
  // though, fallback to an output mode that is available.
  SbPlayerOutputMode output_mode = kSbPlayerOutputModeInvalid;
  if (sbplayer_interface_->GetUrlPlayerOutputModeSupported(
          kSbPlayerOutputModePunchOut)) {
    output_mode = kSbPlayerOutputModePunchOut;
  }
  if ((default_output_mode == kSbPlayerOutputModeDecodeToTexture ||
       output_mode == kSbPlayerOutputModeInvalid) &&
      sbplayer_interface_->GetUrlPlayerOutputModeSupported(
          kSbPlayerOutputModeDecodeToTexture)) {
    output_mode = kSbPlayerOutputModeDecodeToTexture;
  }
  CHECK_NE(kSbPlayerOutputModeInvalid, output_mode);

  return output_mode;
}
#endif  // SB_HAS(PLAYER_WITH_URL)

SbPlayerOutputMode SbPlayerBridge::ComputeSbPlayerOutputMode(
    SbPlayerOutputMode default_output_mode) const {
  SbPlayerCreationParam creation_param = {};
  creation_param.drm_system = drm_system_;
  creation_param.audio_stream_info = audio_stream_info_;
  creation_param.video_stream_info = video_stream_info_;

  if (default_output_mode != kSbPlayerOutputModeDecodeToTexture &&
      video_stream_info_.codec != kSbMediaVideoCodecNone) {
    DCHECK(video_stream_info_.mime);
    DCHECK(video_stream_info_.max_video_capabilities);

    // Set the `default_output_mode` to `kSbPlayerOutputModeDecodeToTexture` if
    // any of the mime associated with it has `decode-to-texture=true` set.
    // TODO(b/232559177): Make the check below more flexible, to work with
    //                    other well formed inputs (e.g. with extra space).
    const bool force_decode_to_texture =
        experimental_features_.GetBool(kMediaForceDecodeToTexture);
    bool is_decode_to_texture_preferred =
        force_decode_to_texture ||
        strstr(video_stream_info_.mime, "decode-to-texture=true") ||
        strstr(video_stream_info_.max_video_capabilities,
               "decode-to-texture=true");

    if (is_decode_to_texture_preferred) {
      LOG(INFO) << "Setting `default_output_mode` from \""
                << GetPlayerOutputModeName(default_output_mode) << "\" to \""
                << GetPlayerOutputModeName(kSbPlayerOutputModeDecodeToTexture)
                << "\" because force_decode_to_texture is "
                << starboard::ToString(force_decode_to_texture)
                << ", mime is set to \"" << video_stream_info_.mime
                << "\", and max_video_capabilities is set to \""
                << video_stream_info_.max_video_capabilities << "\"";
      default_output_mode = kSbPlayerOutputModeDecodeToTexture;
    }
  }

  creation_param.output_mode = default_output_mode;

  auto output_mode =
      sbplayer_interface_->GetPreferredOutputMode(&creation_param);

  LOG(INFO) << "Output mode is set to " << GetPlayerOutputModeName(output_mode);

  return output_mode;
}

void SbPlayerBridge::LogStartupLatency() const {
  std::string first_events_str;
  if (set_drm_system_ready_cb_time_.is_null()) {
    first_events_str = FormatString("%-40s0 us", "SbPlayerCreate() called");
  } else if (set_drm_system_ready_cb_time_ < player_creation_time_) {
    first_events_str = FormatString(
        "%-40s0 us\n%-40s%" PRId64 " us", "set_drm_system_ready_cb called",
        "SbPlayerCreate() called",
        (player_creation_time_ - set_drm_system_ready_cb_time_)
            .InMicroseconds());
  } else {
    first_events_str = FormatString(
        "%-40s0 us\n%-40s%" PRId64 " us", "SbPlayerCreate() called",
        "set_drm_system_ready_cb called",
        (set_drm_system_ready_cb_time_ - player_creation_time_)
            .InMicroseconds());
  }

  Time first_event_time =
      std::max(player_creation_time_, set_drm_system_ready_cb_time_);
  TimeDelta player_initialization_time_delta =
      sb_player_state_initialized_time_ - first_event_time;
  TimeDelta player_preroll_time_delta =
      sb_player_state_prerolling_time_ - sb_player_state_initialized_time_;
  TimeDelta first_audio_sample_time_delta = std::max(
      first_audio_sample_time_ - sb_player_state_prerolling_time_, TimeDelta());
  TimeDelta first_video_sample_time_delta = std::max(
      first_video_sample_time_ - sb_player_state_prerolling_time_, TimeDelta());
  TimeDelta player_presenting_time_delta =
      sb_player_state_presenting_time_ -
      std::max(first_audio_sample_time_, first_video_sample_time_);
  TimeDelta startup_latency =
      sb_player_state_presenting_time_ - first_event_time;

#if BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)
  StatisticsWrapper::GetInstance()->startup_latency.AddSample(
      startup_latency.InMicroseconds(), 1);
#endif  // BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)

  // clang-format off
  LOG(INFO) << FormatString(
      "\nSbPlayer startup latencies: %" PRId64 " us\n"
      "  Event name                              time since last event\n"
      "  %s\n"  // |first_events_str| populated above
      "  kSbPlayerStateInitialized received      %" PRId64 " us\n"
      "  kSbPlayerStatePrerolling received       %" PRId64 " us\n"
      "  First media sample(s) written [a/v]     %" PRId64 "/%" PRId64 " us\n"
      "  kSbPlayerStatePresenting received       %" PRId64 " us\n"
      "  Startup latency statistics (us):"
      "        min: %" PRId64 ", median: %" PRId64 ", average: %" PRId64
      ", max: %" PRId64,
      startup_latency.InMicroseconds(),
      first_events_str.c_str(), player_initialization_time_delta.InMicroseconds(),
      player_preroll_time_delta.InMicroseconds(), first_audio_sample_time_delta.InMicroseconds(),
      first_video_sample_time_delta.InMicroseconds(), player_presenting_time_delta.InMicroseconds(),
#if BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)
      StatisticsWrapper::GetInstance()->startup_latency.min(),
      StatisticsWrapper::GetInstance()->startup_latency.GetMedian(),
      StatisticsWrapper::GetInstance()->startup_latency.average(),
      StatisticsWrapper::GetInstance()->startup_latency.max());
#else // BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)
      static_cast<int64_t>(0),
      static_cast<int64_t>(0),
      static_cast<int64_t>(0),
      static_cast<int64_t>(0));
#endif // BUILDFLAG(COBALT_MEDIA_ENABLE_STARTUP_LATENCY_TRACKING)
  // clang-format on
}

void SbPlayerBridge::SendColorSpaceHistogram() const {
  using base::UmaHistogramEnumeration;

  const auto& cs_info = video_config_.color_space_info();

  if (video_stream_info_.color_metadata.bits_per_channel > 8) {
    UmaHistogramEnumeration("Cobalt.Media.HDR.Primaries", cs_info.primaries);
    UmaHistogramEnumeration("Cobalt.Media.HDR.Transfer", cs_info.transfer);
    UmaHistogramEnumeration("Cobalt.Media.HDR.Matrix", cs_info.matrix);
    UmaHistogramEnumeration("Cobalt.Media.HDR.Range", cs_info.range);
  } else {
    UmaHistogramEnumeration("Cobalt.Media.SDR.Primaries", cs_info.primaries);
    UmaHistogramEnumeration("Cobalt.Media.SDR.Transfer", cs_info.transfer);
    UmaHistogramEnumeration("Cobalt.Media.SDR.Matrix", cs_info.matrix);
    UmaHistogramEnumeration("Cobalt.Media.SDR.Range", cs_info.range);
  }
}

}  // namespace media
