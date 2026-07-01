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

#include "media/starboard/url_player_demuxer.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/channel_layout.h"
#include "media/base/decoder_buffer.h"
#include "media/base/media_track.h"
#include "media/base/sample_format.h"
#include "media/base/video_decoder_config.h"

namespace media {

bool IsHlsUrl(const GURL& url) {
  // See
  // https://github.com/youtube/cobalt/blob/25.lts.stable/cobalt/dom/html_media_element.cc#L103
  auto path = url.path_piece();
  return path.ends_with(".m3u8") ||
         path.find("hls_variant") != std::string_view::npos;
}

// --- UrlPlayerDemuxerStream ---

UrlPlayerDemuxerStream::UrlPlayerDemuxerStream(Type type) : type_(type) {}

UrlPlayerDemuxerStream::~UrlPlayerDemuxerStream() = default;

void UrlPlayerDemuxerStream::Read(uint32_t count, ReadCB read_cb) {
  // The platform URL player handles all data loading and Read() should never
  // be called on placeholder streams.
  NOTREACHED();
}

AudioDecoderConfig UrlPlayerDemuxerStream::audio_decoder_config() {
  return AudioDecoderConfig(AudioCodec::kAAC, kSampleFormatS16,
                            CHANNEL_LAYOUT_STEREO,
                            /*samples_per_second=*/44100, /*extra_data=*/{},
                            EncryptionScheme::kUnencrypted);
}

VideoDecoderConfig UrlPlayerDemuxerStream::video_decoder_config() {
  static const gfx::Size kPlaceholderSize(1, 1);
  return VideoDecoderConfig(
      VideoCodec::kH264, VideoCodecProfile::H264PROFILE_BASELINE,
      VideoDecoderConfig::AlphaMode::kIsOpaque, VideoColorSpace(),
      kNoTransformation, kPlaceholderSize, gfx::Rect(kPlaceholderSize),
      kPlaceholderSize, /*extra_data=*/{}, EncryptionScheme::kUnencrypted);
}

DemuxerStream::Type UrlPlayerDemuxerStream::type() const {
  return type_;
}

bool UrlPlayerDemuxerStream::SupportsConfigChanges() {
  return false;
}

// --- UrlPlayerDemuxer ---

UrlPlayerDemuxer::UrlPlayerDemuxer(
    scoped_refptr<base::SequencedTaskRunner> media_task_runner,
    GURL url)
    : media_task_runner_(std::move(media_task_runner)), url_(std::move(url)) {
  DCHECK(media_task_runner_);
}

UrlPlayerDemuxer::~UrlPlayerDemuxer() = default;

std::vector<DemuxerStream*> UrlPlayerDemuxer::GetAllStreams() {
  return {&audio_stream_, &video_stream_};
}

GURL UrlPlayerDemuxer::GetMediaUrl() const {
  return url_;
}

std::string UrlPlayerDemuxer::GetDisplayName() const {
  return "UrlPlayerDemuxer";
}

DemuxerType UrlPlayerDemuxer::GetDemuxerType() const {
  return DemuxerType::kUrlPlayerDemuxer;
}

void UrlPlayerDemuxer::Initialize(DemuxerHost* host,
                                  PipelineStatusCallback status_cb) {
  DVLOG(1) << __func__;
  host_ = host;
  media_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(std::move(status_cb), PIPELINE_OK));
}

void UrlPlayerDemuxer::AbortPendingReads() {}
void UrlPlayerDemuxer::StartWaitingForSeek(base::TimeDelta seek_time) {}
void UrlPlayerDemuxer::CancelPendingSeek(base::TimeDelta seek_time) {}

void UrlPlayerDemuxer::Seek(base::TimeDelta time,
                            PipelineStatusCallback status_cb) {
  DVLOG(1) << __func__ << "(" << time << ")";
  media_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(std::move(status_cb), PIPELINE_OK));
}

// While the demuxer itself is not seekable, the underlying URL player is.
bool UrlPlayerDemuxer::IsSeekable() const {
  return true;
}

void UrlPlayerDemuxer::Stop() {}

base::TimeDelta UrlPlayerDemuxer::GetStartTime() const {
  return base::TimeDelta();
}

base::Time UrlPlayerDemuxer::GetTimelineOffset() const {
  return base::Time();
}

int64_t UrlPlayerDemuxer::GetMemoryUsage() const {
  return 0;
}

std::optional<container_names::MediaContainerName>
UrlPlayerDemuxer::GetContainerForMetrics() const {
  return std::nullopt;
}

void UrlPlayerDemuxer::OnTracksChanged(
    DemuxerStream::Type track_type,
    const std::vector<MediaTrack::Id>& track_ids,
    base::TimeDelta curr_time,
    TrackChangeCB change_completed_cb) {
  std::vector<DemuxerStream*> streams;
  std::move(change_completed_cb).Run(streams);
  DLOG(WARNING) << "Track changes are not supported by UrlPlayerDemuxer.";
}

void UrlPlayerDemuxer::SetPlaybackRate(double rate) {}

}  // namespace media
