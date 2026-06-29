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

#include "starboard/android/shared/media_codec_video_decoder.h"

#include <jni.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <list>

#include "build/build_config.h"
#include "starboard/android/shared/media_capabilities_cache.h"
#include "starboard/android/shared/media_codec_video_decoder_helpers.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/android/shared/video_render_algorithm_android.h"
#include "starboard/android/shared/video_surface_texture_bridge.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/player.h"
#include "starboard/common/size.h"
#include "starboard/common/string.h"
#include "starboard/configuration.h"
#include "starboard/decode_target.h"
#include "starboard/drm.h"
#include "starboard/shared/starboard/media/media_tracing.h"
#include "starboard/shared/starboard/media/mime_type.h"
#include "starboard/shared/starboard/player/filter/video_frame_internal.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {
namespace {

using jni_zero::AttachCurrentThread;
using jni_zero::JavaRef;
using std::placeholders::_1;
using std::placeholders::_2;

class VideoFrameImpl : public VideoFrame {
 public:
  typedef std::function<void()> VideoFrameReleaseCallback;

  VideoFrameImpl(const DequeueOutputResult& dequeue_output_result,
                 MediaCodec* media_codec_bridge,
                 const VideoFrameReleaseCallback& release_callback)
      : VideoFrame(dequeue_output_result.flags &
                           MediaCodec::kBufferFlagEndOfStream
                       ? kMediaTimeEndOfStream
                       : dequeue_output_result.presentation_time_microseconds),
        dequeue_output_result_(dequeue_output_result),
        media_codec_bridge_(media_codec_bridge),
        released_(false),
        release_callback_(release_callback) {
    SB_DCHECK(media_codec_bridge_);
    SB_DCHECK(release_callback_);
  }

  ~VideoFrameImpl() {
    if (!released_) {
      media_codec_bridge_->ReleaseOutputBuffer(dequeue_output_result_.index,
                                               false);
      if (!is_end_of_stream()) {
        release_callback_();
      }
    }
  }

  void Draw(int64_t release_time_in_nanoseconds) {
    SB_DCHECK(!released_);
    SB_DCHECK(!is_end_of_stream());
    released_ = true;
    media_codec_bridge_->ReleaseOutputBufferAtTimestamp(
        dequeue_output_result_.index, release_time_in_nanoseconds);
    release_callback_();
  }

 private:
  DequeueOutputResult dequeue_output_result_;
  MediaCodec* media_codec_bridge_;
  volatile bool released_;
  const VideoFrameReleaseCallback release_callback_;
};

const int64_t kInitialPrerollTimeout = 250'000;                  // 250ms
const int64_t kNeedMoreInputCheckIntervalInTunnelMode = 50'000;  // 50ms

const int kInitialPrerollFrameCount = 8;
const int kNonInitialPrerollFrameCount = 1;
// According to b/487397946#comment3, after the first non-DECODE_ONLY frame is
// rendered, the rest of the playback should play without frame drops. So,
// tunnel mode prerolling only needs 1 frame.
const int kTunnelModePrerollFrameCount = 1;
const int kMaxPendingInputsSize = 128;

const int kFpsGuesstimateRequiredInputBufferCount = 3;

void StubDrmSessionUpdateRequestFunc(SbDrmSystem drm_system,
                                     void* context,
                                     int ticket,
                                     SbDrmStatus status,
                                     SbDrmSessionRequestType type,
                                     const char* error_message,
                                     const void* session_id,
                                     int session_id_size,
                                     const void* content,
                                     int content_size,
                                     const char* url) {}

void StubDrmSessionUpdatedFunc(SbDrmSystem drm_system,
                               void* context,
                               int ticket,
                               SbDrmStatus status,
                               const char* error_message,
                               const void* session_id,
                               int session_id_size) {}

void StubDrmSessionKeyStatusesChangedFunc(SbDrmSystem drm_system,
                                          void* context,
                                          const void* session_id,
                                          int session_id_size,
                                          int number_of_keys,
                                          const SbDrmKeyId* key_ids,
                                          const SbDrmKeyStatus* key_statuses) {}

const DrmSystem::Callbacks kStubDrmSystemCallbacks = {
    StubDrmSessionUpdateRequestFunc, StubDrmSessionUpdatedFunc,
    StubDrmSessionKeyStatusesChangedFunc};

}  // namespace

// TODO: Merge this with VideoFrameTracker, maybe?
class VideoRenderAlgorithmTunneled : public VideoRenderAlgorithm {
 public:
  explicit VideoRenderAlgorithmTunneled(VideoFrameTracker* frame_tracker)
      : frame_tracker_(frame_tracker) {
    SB_DCHECK(frame_tracker_);
  }

  void Render(MediaTimeProvider* media_time_provider,
              std::list<scoped_refptr<VideoFrame>>* frames,
              VideoRendererSink::DrawFrameCB draw_frame_cb) override {
    // Clear output frames.
    while (!frames->empty() && !frames->front()->is_end_of_stream()) {
      frames->pop_front();
    }
  }
  void Seek(int64_t seek_to_time) override {
    frame_tracker_->Seek(seek_to_time);
  }
  int GetDroppedFrames() override {
    return frame_tracker_->UpdateAndGetDroppedFrames();
  }

 private:
  VideoFrameTracker* frame_tracker_;
};

class MediaCodecVideoDecoder::Sink : public VideoRendererSink {
 public:
  typedef std::function<void(double playback_rate)> PlaybackRateChangedCB;

  Sink(PlaybackRateChangedCB playback_rate_changed_cb)
      : playback_rate_changed_cb_(playback_rate_changed_cb) {}

  bool Render() {
    SB_DCHECK(render_cb_);

    rendered_ = false;
    render_cb_(std::bind(&Sink::DrawFrame, this, _1, _2));

    return rendered_;
  }

 private:
  void SetPlaybackRate(double playback_rate) override {
    SB_DCHECK(playback_rate_changed_cb_);
    playback_rate_changed_cb_(playback_rate);
  }

  void SetRenderCB(RenderCB render_cb) override {
    SB_DCHECK(!render_cb_);
    SB_DCHECK(render_cb);

    render_cb_ = render_cb;
  }

  void SetBounds(int z_index, const Rect& rect) override {}

  DrawFrameStatus DrawFrame(const scoped_refptr<VideoFrame>& frame,
                            int64_t release_time_in_nanoseconds) {
    rendered_ = true;
    static_cast<VideoFrameImpl*>(frame.get())
        ->Draw(release_time_in_nanoseconds);

    return kReleased;
  }

  PlaybackRateChangedCB playback_rate_changed_cb_;
  RenderCB render_cb_;
  bool rendered_;
};

NonNullResult<std::unique_ptr<MediaCodecVideoDecoder>>
MediaCodecVideoDecoder::Create(JobQueue* job_queue,
                               const StreamConfig& stream_config,
                               const TunnelModeConfig& tunnel_mode_config,
                               const PipelineConfig& pipeline_config,
                               const PlatformOptions& platform_options) {
  auto default_factory = std::make_unique<DefaultMediaCodecFactory>();
  return CreateInternal(std::move(default_factory), job_queue, stream_config,
                        tunnel_mode_config, pipeline_config, platform_options);
}

// static
NonNullResult<std::unique_ptr<MediaCodecVideoDecoder>>
MediaCodecVideoDecoder::CreateForTesting(
    std::unique_ptr<MediaCodec::Factory> media_codec_factory,
    JobQueue* job_queue,
    const StreamConfig& stream_config,
    const TunnelModeConfig& tunnel_mode_config,
    const PipelineConfig& pipeline_config,
    const PlatformOptions& platform_options) {
  SB_CHECK(media_codec_factory);
  return CreateInternal(std::move(media_codec_factory), job_queue,
                        stream_config, tunnel_mode_config, pipeline_config,
                        platform_options);
}

// static
NonNullResult<std::unique_ptr<MediaCodecVideoDecoder>>
MediaCodecVideoDecoder::CreateInternal(
    std::unique_ptr<MediaCodec::Factory> media_codec_factory,
    JobQueue* job_queue,
    const StreamConfig& stream_config,
    const TunnelModeConfig& tunnel_mode_config,
    const PipelineConfig& pipeline_config,
    const PlatformOptions& platform_options) {
  std::string error_message;
  auto video_decoder = std::make_unique<MediaCodecVideoDecoder>(
      PassKey<MediaCodecVideoDecoder>(), std::move(media_codec_factory),
      job_queue, stream_config, tunnel_mode_config, pipeline_config,
      platform_options, &error_message);

  if (!error_message.empty()) {
    return Failure(error_message);
  }
  if (stream_config.video_stream_info.codec != kSbMediaVideoCodecAv1 &&
      !video_decoder->media_decoder_) {
    return Failure(
        "Video decoder was not created, but no error message was provided.");
  }
  return video_decoder;
}

MediaCodecVideoDecoder::MediaCodecVideoDecoder(
    PassKey<MediaCodecVideoDecoder>,
    std::unique_ptr<MediaCodec::Factory> media_codec_factory,
    JobQueue* job_queue,
    const StreamConfig& stream_config,
    const TunnelModeConfig& tunnel_mode_config,
    const PipelineConfig& pipeline_config,
    const PlatformOptions& platform_options,
    std::string* error_message)
    : JobOwner(job_queue),
      video_codec_(stream_config.video_stream_info.codec),
      drm_system_(static_cast<DrmSystem*>(stream_config.drm_system)),
      output_mode_(stream_config.output_mode),
      decode_target_graphics_context_provider_(
          stream_config.decode_target_graphics_context_provider),
      max_video_capabilities_(stream_config.max_video_capabilities),
      require_software_codec_(
          IsSoftwareDecoderRequired(stream_config.max_video_capabilities)),
      force_big_endian_hdr_metadata_(
          platform_options.force_big_endian_hdr_metadata),
      tunnel_mode_audio_session_id_(tunnel_mode_config.audio_session_id),
      max_video_input_size_(pipeline_config.max_input_size),
      use_dual_threads_(pipeline_config.use_dual_threads),
      surface_view_(stream_config.surface_view
                        ? jni_zero::ScopedJavaGlobalRef<jobject>(
                              jni_zero::AttachCurrentThread(),
                              static_cast<jobject>(stream_config.surface_view))
                        : nullptr),
      enable_flush_during_seek_(pipeline_config.enable_flush_during_seek),
      reset_delay_usec_(android_get_device_api_level() < 34
                            ? platform_options.reset_delay_usec
                            : 0),
      flush_delay_usec_(android_get_device_api_level() < 34
                            ? platform_options.flush_delay_usec
                            : 0),
      skip_flush_on_decoder_teardown_(
          pipeline_config.experimental_features.skip_flush_on_decoder_teardown),
      force_clear_surface_(
          pipeline_config.experimental_features.force_clear_surface_view),
      needs_fps_to_initialize_codec_(
          video_codec_ == kSbMediaVideoCodecAv1 &&
          MediaCapabilitiesCache::GetInstance()->IsAv18kCappedAt30()),
      skip_video_frames_over_60_fps_(
          pipeline_config.experimental_features.skip_video_frames_over_60_fps),
      ignore_mediacodec_callbacks_during_flushing_(
          pipeline_config.experimental_features
              .ignore_mediacodec_callbacks_during_flushing),
      enable_low_latency_(
          pipeline_config.experimental_features.enable_low_latency),
      is_video_frame_tracker_enabled_(android_get_device_api_level() >= 34 ||
                                      tunnel_mode_audio_session_id_),
      media_codec_factory_(std::move(media_codec_factory)),
      has_new_texture_available_(false),
      initial_number_of_preroll_frames_(
          pipeline_config.experimental_features
              .video_decoder_initial_preroll_count.value_or(
                  kInitialPrerollFrameCount)),
      number_of_preroll_frames_(initial_number_of_preroll_frames_),
      surface_texture_bridge_(
          output_mode_ == kSbPlayerOutputModeDecodeToTexture
              ? std::make_unique<VideoSurfaceTextureBridge>(this)
              : nullptr) {
  SB_CHECK(error_message);
  SB_CHECK(media_codec_factory_);

  if (tunnel_mode_config.force_secure_pipeline) {
    SB_DCHECK(tunnel_mode_audio_session_id_);
    SB_DCHECK(!drm_system_);
    // To create secure pipeline for tunnel mode, we need use
    // L1("com.widevine.alpha").

    auto tunnel_mode_drm_system = DrmSystem::Create(
        "com.widevine.alpha", /*context=*/nullptr, kStubDrmSystemCallbacks);
    if (!tunnel_mode_drm_system) {
      *error_message =
          "Failed to create DrmSystem for tunnel mode enforcement.";
      return;
    }

    drm_system_to_enforce_tunnel_mode_ = std::move(tunnel_mode_drm_system);
    drm_system_ = drm_system_to_enforce_tunnel_mode_.get();
  }

  if (is_video_frame_tracker_enabled_) {
    video_frame_tracker_ =
        std::make_unique<VideoFrameTracker>(kMaxPendingInputsSize * 2);
  }

  if (require_software_codec_) {
    SB_DCHECK_EQ(output_mode_, kSbPlayerOutputModeDecodeToTexture);
  }

  if (!needs_fps_to_initialize_codec_) {
    auto result = InitializeCodec(stream_config.video_stream_info);
    if (!result) {
      *error_message =
          "Failed to initialize video decoder with error: " + result.error();
      SB_LOG(ERROR) << *error_message;
      TeardownCodec();
    }
  }

  SB_LOG(INFO) << "Created VideoDecoder for codec="
               << GetMediaVideoCodecName(video_codec_)
               << ", with output mode=" << GetPlayerOutputModeName(output_mode_)
               << ", preroll count=" << number_of_preroll_frames_
               << ", max pending input size=" << kMaxPendingInputsSize
               << ", max video capabilities=\"" << max_video_capabilities_
               << "\", tunnel mode audio session id="
               << ToString(tunnel_mode_audio_session_id_)
               << ", is_video_frame_tracker_enabled="
               << ToString(is_video_frame_tracker_enabled_);
}

MediaCodecVideoDecoder::~MediaCodecVideoDecoder() {
  TeardownCodec();
  // The video surface must be reset after tunnel mode playbacks. This prevents
  // video distortion on some platforms. For details, see http://b/182610842.
  bool force_clear =
      !tunnel_mode_audio_session_id_.has_value() && force_clear_surface_;
  CleanUpVideoWindow(force_clear, decode_target_graphics_context_provider_);
}

scoped_refptr<VideoRendererSink> MediaCodecVideoDecoder::GetSink() {
  if (sink_ == nullptr) {
    sink_ = make_scoped_refptr<Sink>(
        std::bind(&MediaCodecVideoDecoder::SetPlaybackRate, this, _1));
  }
  return sink_;
}

std::unique_ptr<VideoRenderAlgorithm>
MediaCodecVideoDecoder::GetRenderAlgorithm() {
  if (!tunnel_mode_audio_session_id_) {
    return std::make_unique<VideoRenderAlgorithmAndroid>(
        this, video_frame_tracker_.get());
  }
  return std::make_unique<VideoRenderAlgorithmTunneled>(
      video_frame_tracker_.get());
}

void MediaCodecVideoDecoder::Initialize(
    const DecoderStatusCB& decoder_status_cb,
    const ErrorCB& error_cb) {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(decoder_status_cb);
  SB_DCHECK(!decoder_status_cb_);
  SB_DCHECK(error_cb);
  SB_DCHECK(!error_cb_);

  decoder_status_cb_ = decoder_status_cb;
  error_cb_ = error_cb;

  // There's a race condition when suspending the app. If surface view is
  // destroyed before this function is called, |media_decoder_| could be null
  // here.
  if (!media_decoder_) {
    SB_LOG(INFO) << "Trying to call Initialize() when media_decoder_ is null.";
    return;
  }
  media_decoder_->Initialize(
      std::bind(&MediaCodecVideoDecoder::ReportError, this, _1, _2));
}

size_t MediaCodecVideoDecoder::GetPrerollFrameCount() const {
  // Tunnel mode uses its own preroll logic.
  if (tunnel_mode_audio_session_id_) {
    return 0;
  }
  if (input_buffer_written_ > 0 && first_buffer_timestamp_ != 0) {
    return kNonInitialPrerollFrameCount;
  }
  return number_of_preroll_frames_;
}

int64_t MediaCodecVideoDecoder::GetPrerollTimeout() const {
  // Tunnel mode uses its own preroll logic.
  if (tunnel_mode_audio_session_id_) {
    return std::numeric_limits<int64_t>::max();
  }
  if (input_buffer_written_ > 0 && first_buffer_timestamp_ != 0) {
    return std::numeric_limits<int64_t>::max();
  }
  return kInitialPrerollTimeout;
}

void MediaCodecVideoDecoder::WriteInputBuffers(
    const InputBuffers& input_buffers) {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(!input_buffers.empty());
  SB_DCHECK_EQ(input_buffers.front()->sample_type(), kSbMediaTypeVideo);
  SB_DCHECK(decoder_status_cb_);

  MEDIA_TRACE_EVENT("starboard", "VideoDecoder::WriteInputBuffers", "timestamp",
                    input_buffers.front()->timestamp(), "size",
                    input_buffers.size());

  if (input_buffer_written_ == 0) {
    SB_DCHECK_EQ(video_fps_, 0);
    first_buffer_timestamp_ = input_buffers.front()->timestamp();

    // If color metadata is present and is not an identity mapping, then
    // teardown the codec so it can be reinitalized with the new metadata.
    const auto& color_metadata =
        input_buffers.front()->video_stream_info().color_metadata;
    if (!IsSDR(color_metadata)) {
      SB_DCHECK(!color_metadata_) << "Unexpected residual color metadata.";
      SB_LOG(INFO) << "Reinitializing codec with HDR color metadata.";
      TeardownCodec();
      color_metadata_ = color_metadata;
    }

    // Re-initialize the codec now if it was torn down either in |Reset| or
    // because we need to change the color metadata.
    if (!needs_fps_to_initialize_codec_ && media_decoder_ == NULL) {
      auto result = InitializeCodec(input_buffers.front()->video_stream_info());
      if (!result) {
        std::string error_message =
            "Failed to reinitialize codec with error: " + result.error();
        SB_LOG(ERROR) << error_message;
        TeardownCodec();
        ReportError(kSbPlayerErrorDecode, error_message);
        return;
      }
    }
  }

  input_buffer_written_ += input_buffers.size();

  if (needs_fps_to_initialize_codec_ && video_fps_ == 0) {
    SB_DCHECK(!media_decoder_);

    pending_input_buffers_.insert(pending_input_buffers_.end(),
                                  input_buffers.begin(), input_buffers.end());
    if (pending_input_buffers_.size() <
        kFpsGuesstimateRequiredInputBufferCount) {
      decoder_status_cb_(kNeedMoreInput, NULL);
      return;
    }
    auto result =
        InitializeCodec(pending_input_buffers_.front()->video_stream_info());
    if (!result) {
      std::string error_message =
          "Failed to reinitialize codec with error: " + result.error();
      SB_LOG(ERROR) << error_message;
      TeardownCodec();
      ReportError(kSbPlayerErrorDecode, error_message);
      return;
    }
    return;
  }

  WriteInputBuffersInternal(input_buffers);
}

void MediaCodecVideoDecoder::WriteEndOfStream() {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(decoder_status_cb_);

  if (end_of_stream_written_) {
    SB_LOG(WARNING) << "WriteEndOfStream() is called more than once.";
    return;
  }
  end_of_stream_written_ = true;

  if (input_buffer_written_ == 0) {
    // In this case, |media_decoder_|'s decoder thread is not initialized,
    // return EOS frame directly.
    first_buffer_timestamp_ = 0;
    decoder_status_cb_(kBufferFull, VideoFrame::CreateEOSFrame());
    return;
  }

  if (needs_fps_to_initialize_codec_ && video_fps_ == 0) {
    SB_DCHECK(!media_decoder_);
    SB_DCHECK_EQ(pending_input_buffers_.size(),
                 static_cast<size_t>(input_buffer_written_));

    auto result =
        InitializeCodec(pending_input_buffers_.front()->video_stream_info());
    if (!result) {
      std::string error_message =
          "Failed to reinitialize codec with error: " + result.error();
      SB_LOG(ERROR) << error_message;
      TeardownCodec();
      ReportError(kSbPlayerErrorDecode, error_message);
      return;
    }
  }

  // There's a race condition when suspending the app. If surface view is
  // destroyed before video decoder stopped, |media_decoder_| could be null
  // here. And error_cb_() could be handled asynchronously. It's possible
  // that WriteEndOfStream() is called immediately after the first
  // WriteInputBuffer() fails, in such case |media_decoder_| will be null.
  if (!media_decoder_) {
    SB_LOG(INFO)
        << "Trying to write end of stream when media_decoder_ is null.";
    return;
  }

  media_decoder_->WriteEndOfStream();
}

void MediaCodecVideoDecoder::Reset() {
  ResetInternal(/*skip_flush=*/false);
}

void MediaCodecVideoDecoder::ResetForTeardown() {
  ResetInternal(skip_flush_on_decoder_teardown_);
}

// When in decode-to-texture mode, this returns the current decoded video frame.
SbDecodeTarget MediaCodecVideoDecoder::GetCurrentDecodeTarget() {
  SB_DCHECK_EQ(output_mode_, kSbPlayerOutputModeDecodeToTexture);
  // We must take a lock here since this function can be called from a separate
  // thread.
  std::lock_guard lock(decode_target_mutex_);
  if (decode_target_ != nullptr) {
    bool has_new_texture = has_new_texture_available_.exchange(false);
    if (has_new_texture) {
      VideoSurfaceTextureBridge::UpdateTexImage(
          AttachCurrentThread(), decode_target_->surface_texture());
      UpdateDecodeTargetSizeAndContentRegion_Locked();

      if (!first_texture_received_) {
        first_texture_received_ = true;
      }
    }

    if (first_texture_received_) {
      decode_target_->AddRef();
      return decode_target_;
    }
  }
  return kSbDecodeTargetInvalid;
}

void MediaCodecVideoDecoder::UpdateDecodeTargetSizeAndContentRegion_Locked() {
  SB_DCHECK(!frame_sizes_.empty());

  JNIEnv* env = AttachCurrentThread();

  while (!frame_sizes_.empty()) {
    const auto& frame_size = frame_sizes_.front();
    if (frame_size.has_crop_values) {
      auto matrix4x4 = VideoSurfaceTextureBridge::GetTransformMatrix(
          env, decode_target_->surface_texture());
      auto [content_region, coded_size] =
          GetDecodeTargetGeometryFromMatrix(matrix4x4, frame_size.display_size);

      decode_target_->set_dimension(coded_size);
      decode_target_->set_content_region(content_region);

      // Now we have two crop rectangles, one from the MediaFormat, one from the
      // transform of the surface texture.  Their sizes should match.
      // Note that we cannot compare individual corners directly, as the values
      // retrieving from the surface texture can be flipped.
      int content_region_width =
          std::abs(content_region.left - content_region.right);
      int content_region_height =
          std::abs(content_region.bottom - content_region.top);
      // Using 2 as epsilon, as the texture may get clipped by one pixel from
      // each side.
      const auto display_size = frame_size.display_size;
      bool are_crop_values_matching =
          std::abs(content_region_width - display_size.width) <= 2 &&
          std::abs(content_region_height - display_size.height) <= 2;
      if (are_crop_values_matching) {
        return;
      }

#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
      // If we failed to find any matching clip regions, the crop values
      // returned from the platform may be inconsistent.
      // Crash in non-gold mode, and fallback to the old logic in gold mode to
      // avoid terminating the app in production.
      SB_LOG_IF(WARNING, frame_sizes_.size() <= 1)
          << frame_size << " - (" << content_region.left << ", "
          << content_region.top << ", " << content_region.right << ", "
          << content_region.bottom << ")";
#endif  // !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
    } else {
      SB_LOG(WARNING) << "Crop values not set.";
    }

    if (frame_sizes_.size() == 1) {
      SB_LOG(WARNING) << "Setting content region frame width/height failed,"
                      << " fallback to the legacy logic.";
      break;
    }

    frame_sizes_.erase(frame_sizes_.begin());
  }

  SB_DCHECK(!frame_sizes_.empty());
  if (frame_sizes_.empty()) {
    // This should never happen.  Appending a default value so it aligns to the
    // legacy behavior, where a single value (instead of an std::vector<>) is
    // used.
    frame_sizes_.resize(1);
  }

  // The legacy logic works when the crop rectangle has the same aspect ratio as
  // the video texture, which is true for most of the playbacks.
  // Leaving the legacy logic in place in case the new logic above doesn't work
  // on some devices, so at least the majority of playbacks still work.
  auto matrix4x4 = VideoSurfaceTextureBridge::GetTransformMatrix(
      env, decode_target_->surface_texture());
  auto [content_region, coded_size] = GetDecodeTargetGeometryFromMatrix(
      matrix4x4, frame_sizes_.back().display_size);
  decode_target_->set_dimension(coded_size);
  decode_target_->set_content_region(content_region);
}

void MediaCodecVideoDecoder::SetPlaybackRate(double playback_rate) {
  if (playback_rate_ == playback_rate) {
    return;
  }

  playback_rate_ = playback_rate;
  if (media_decoder_) {
    media_decoder_->SetPlaybackRate(playback_rate);
  }
}

void MediaCodecVideoDecoder::OnFrameAvailable() {
  has_new_texture_available_.store(true);
}

Result<void> MediaCodecVideoDecoder::InitializeCodec(
    const VideoStreamInfo& video_stream_info) {
  SB_CHECK(BelongsToCurrentThread());

  if (needs_fps_to_initialize_codec_) {
    SB_DCHECK_GT(pending_input_buffers_.size(), 0u);

    // Guesstimate the video fps.
    if (pending_input_buffers_.size() == 1) {
      video_fps_ = 30;
    } else {
      int64_t first_timestamp = pending_input_buffers_[0]->timestamp();
      int64_t second_timestamp = pending_input_buffers_[1]->timestamp();
      if (pending_input_buffers_.size() > 2) {
        second_timestamp =
            std::min(second_timestamp, pending_input_buffers_[2]->timestamp());
      }
      int64_t frame_duration = second_timestamp - first_timestamp;
      if (frame_duration > 0) {
        // To avoid problems caused by deviation of fps calculation, we use the
        // nearest multiple of 5 to check codec capability. So, the fps like 61,
        // 62 will be capped to 60, and 24 will be increased to 25.
        const double kFpsMinDifference = 5;
        video_fps_ =
            std::round(1'000'000LL / (second_timestamp - first_timestamp) /
                       kFpsMinDifference) *
            kFpsMinDifference;
      } else {
        video_fps_ = 30;
      }
    }
    SB_DCHECK_GT(video_fps_, 0);
  }

  // Setup the output surface object.  If we are in punch-out mode, target
  // the passed in Android video surface.  If we are in decode-to-texture
  // mode, create a surface from a new texture target and use that as the
  // output surface.
  JNIEnv* env = AttachCurrentThread();
  jni_zero::ScopedJavaLocalRef<jobject> j_output_surface;
  switch (output_mode_) {
    case kSbPlayerOutputModePunchOut: {
      if (surface_view_) {
        j_output_surface = surface_view_.AsLocalRef(env);
      } else {
        AcquiredSurface acquired_surface = AcquireVideoSurface(job_queue());
        surface_destroy_notifier_ = acquired_surface.destroy_notifier;
        j_output_surface = acquired_surface.surface;
      }
      if (j_output_surface) {
        owns_video_surface_ = true;
      }
    } break;
    case kSbPlayerOutputModeDecodeToTexture: {
      // A width and height of (0, 0) is provided here because Android doesn't
      // actually allocate any memory into the texture at this time.  That is
      // done behind the scenes, the acquired texture is not actually backed
      // by texture data until UpdateTexImage() is called on it.
      if (!decode_target_graphics_context_provider_) {
        return Failure("Invalid decode target graphics context provider.");
      }
      DecodeTarget* decode_target =
          new DecodeTarget(decode_target_graphics_context_provider_);
      if (!SbDecodeTargetIsValid(decode_target)) {
        return Failure("Could not acquire a decode target from provider.");
      }
      j_output_surface =
          jni_zero::ScopedJavaLocalRef<jobject>(env, decode_target->surface());

      surface_texture_bridge_->SetOnFrameAvailableListener(
          env, decode_target->surface_texture());

      std::lock_guard lock(decode_target_mutex_);
      decode_target_ = decode_target;
      // We manually call AddRef() here because `decode_target_` is stored as a
      // raw pointer. This ensures Starboard claims its initial ownership of the
      // target, preventing it from stealing Chromium's reference and deleting
      // the texture prematurely during TeardownCodec().
      decode_target_->AddRef();
    } break;
    case kSbPlayerOutputModeInvalid: {
      SB_NOTREACHED();
    } break;
  }
  if (!j_output_surface) {
    return Failure("Video surface does not exist.");
  }

  if (needs_fps_to_initialize_codec_) {
    SB_DCHECK_GT(video_fps_, 0);
  } else {
    SB_DCHECK_EQ(video_fps_, 0);
  }

  // TODO(b/281431214): Evaluate if we should also parse the fps from
  //                    `max_video_capabilities_` and pass to MediaCodecDecoder
  //                    ctor.
  std::optional<Size> max_frame_size =
      ParseMaxResolution(max_video_capabilities_, video_stream_info.frame_size);

  auto result = MediaCodecDecoder::CreateForVideo(
      *media_codec_factory_, job_queue(), /*host=*/this,
      video_stream_info.codec, video_stream_info.frame_size, max_frame_size,
      video_fps_, j_output_surface, drm_system_,
      color_metadata_ ? &*color_metadata_ : nullptr, require_software_codec_,
      std::bind(&MediaCodecVideoDecoder::OnFrameRendered, this, _1),
      std::bind(&MediaCodecVideoDecoder::OnFirstTunnelFrameReady, this),
      tunnel_mode_audio_session_id_, is_video_frame_tracker_enabled_,
      enable_low_latency_, force_big_endian_hdr_metadata_,
      max_video_input_size_, flush_delay_usec_, use_dual_threads_,
      skip_video_frames_over_60_fps_,
      ignore_mediacodec_callbacks_during_flushing_);
  if (result) {
    media_decoder_ = std::move(result.value());
    if (error_cb_) {
      media_decoder_->Initialize(
          std::bind(&MediaCodecVideoDecoder::ReportError, this, _1, _2));
    }
    media_decoder_->SetPlaybackRate(playback_rate_);

    if (needs_fps_to_initialize_codec_) {
      SB_DCHECK(!pending_input_buffers_.empty());
    } else {
      SB_DCHECK(pending_input_buffers_.empty());
    }
    if (!pending_input_buffers_.empty()) {
      WriteInputBuffersInternal(pending_input_buffers_);
      pending_input_buffers_.clear();
    }
    return Success();
  }
  media_decoder_.reset();
  return Failure("Media Decoder is not valid: " + result.error());
}

void MediaCodecVideoDecoder::TeardownCodec() {
  SB_CHECK(BelongsToCurrentThread());
  if (owns_video_surface_) {
    ReleaseVideoSurface();
    owns_video_surface_ = false;
  }
  if (surface_destroy_notifier_) {
    surface_destroy_notifier_->Disconnect();
    surface_destroy_notifier_ = nullptr;
  }
  media_decoder_.reset();
  color_metadata_ = std::nullopt;

  SbDecodeTarget decode_target_to_release = kSbDecodeTargetInvalid;
  {
    std::lock_guard lock(decode_target_mutex_);
    if (decode_target_ != nullptr) {
      // Remove OnFrameAvailableListener to make sure the callback
      // would not be called.
      JNIEnv* env = AttachCurrentThread();
      surface_texture_bridge_->RemoveOnFrameAvailableListener(
          env, decode_target_->surface_texture());

      decode_target_to_release = decode_target_;
      decode_target_ = nullptr;
      first_texture_received_ = false;
      has_new_texture_available_.store(false);
    } else {
      // If |decode_target_| is not created, |first_texture_received_| and
      // |has_new_texture_available_| should always be false.
      SB_DCHECK(!first_texture_received_);
      SB_DCHECK(!has_new_texture_available_.load());
    }
  }
  // Release SbDecodeTarget on renderer thread. As |decode_target_mutex_| may
  // be required in renderer thread, SbDecodeTargetReleaseInGlesContext() must
  // be called when |decode_target_mutex_| is not locked, or we may get
  // deadlock.
  if (SbDecodeTargetIsValid(decode_target_to_release)) {
    SbDecodeTargetReleaseInGlesContext(decode_target_graphics_context_provider_,
                                       decode_target_to_release);
  }
}

void MediaCodecVideoDecoder::WriteInputBuffersInternal(
    const InputBuffers& input_buffers) {
  SB_DCHECK(!input_buffers.empty());

  // There's a race condition when suspending the app. If surface view is
  // destroyed before video decoder stopped, |media_decoder_| could be null
  // here. And error_cb_() could be handled asynchronously. It's possible
  // that WriteInputBuffer() is called again when the first WriteInputBuffer()
  // fails, in such case |media_decoder_| will be null.
  if (!media_decoder_) {
    SB_LOG(INFO) << "Trying to write input buffer when media_decoder_ is null.";
    return;
  }

  if (is_video_frame_tracker_enabled_) {
    SB_DCHECK(video_frame_tracker_);
    for (const auto& input_buffer : input_buffers) {
      video_frame_tracker_->OnInputBuffer(input_buffer->timestamp());
    }
  }

  media_decoder_->WriteInputBuffers(input_buffers);
  if (media_decoder_->GetNumberOfPendingInputs() < kMaxPendingInputsSize) {
    decoder_status_cb_(kNeedMoreInput, NULL);
  } else if (tunnel_mode_audio_session_id_) {
    // In tunnel mode playback when need data is not signaled above, it is
    // possible that the VideoDecoder won't get a chance to send kNeedMoreInput
    // to the renderer again.  Schedule a task to check back.
    Schedule(
        std::bind(&MediaCodecVideoDecoder::OnTunnelModeCheckForNeedMoreInput,
                  this),
        kNeedMoreInputCheckIntervalInTunnelMode);
  }

  if (tunnel_mode_audio_session_id_ && tunnel_mode_prerolling_.load()) {
    for (const auto& input_buffer : input_buffers) {
      if (input_buffer->timestamp() >= video_frame_tracker_->seek_to_time()) {
        tunnel_mode_prerolled_frames_++;
      }
    }
    if (tunnel_mode_prerolled_frames_ >= kTunnelModePrerollFrameCount) {
      TryToSignalPrerollForTunnelMode();
    }
  }
}

void MediaCodecVideoDecoder::ProcessOutputBuffer(
    MediaCodec* media_codec_bridge,
    const DequeueOutputResult& dequeue_output_result) {
  SB_DCHECK(decoder_status_cb_);
  SB_DCHECK_GE(dequeue_output_result.index, 0);

  bool is_end_of_stream =
      dequeue_output_result.flags & MediaCodec::kBufferFlagEndOfStream;
  if (!is_end_of_stream) {
    ++decoded_output_frames_;
    if (output_format_) {
      ++buffered_output_frames_;
      // We have to wait until we feed enough inputs to the decoder and receive
      // enough outputs before update |max_buffered_output_frames_|. Otherwise,
      // |max_buffered_output_frames_| may be updated to a very small number
      // when we receive the first few outputs.
      if (decoded_output_frames_ > kInitialPrerollFrameCount &&
          buffered_output_frames_ > max_buffered_output_frames_) {
        max_buffered_output_frames_ = buffered_output_frames_;
        MaxMediaCodecOutputBuffersLookupTable::GetInstance()
            ->UpdateMaxOutputBuffers(output_format_.value(),
                                     max_buffered_output_frames_);
      }
    }
  }
  decoder_status_cb_(
      is_end_of_stream ? kBufferFull : kNeedMoreInput,
      new VideoFrameImpl(
          dequeue_output_result, media_codec_bridge,
          std::bind(&MediaCodecVideoDecoder::OnVideoFrameRelease, this)));
}

void MediaCodecVideoDecoder::OnEndOfStreamWritten(
    MediaCodec* media_codec_bridge) {
  if (!tunnel_mode_audio_session_id_) {
    return;
  }

  SB_DCHECK(decoder_status_cb_);

  // TODO: Refactor the VideoDecoder and the VideoRendererImpl to improve the
  //       handling of preroll and EOS for pure punchout decoders.
  decoder_status_cb_(kBufferFull, VideoFrame::CreateEOSFrame());
  sink_->Render();
}

void MediaCodecVideoDecoder::RefreshOutputFormat(
    MediaCodec* media_codec_bridge) {
  SB_DCHECK(media_codec_bridge);
  SB_DLOG(INFO) << "Output format changed, trying to dequeue again.";

  std::lock_guard lock(decode_target_mutex_);
  std::optional<FrameSize> output_size = media_codec_bridge->GetOutputSize();
  if (!output_size) {
    SB_LOG(WARNING)
        << "GetOutputSize() returned null. Defaulting to an empty FrameSize.";
    // We default to an empty FrameSize instead of propagating a failure to
    // ensure system robustness. The calling code historically does not expect
    // this call to fail and is not equipped to handle a null optional.
    output_size = FrameSize();
  }

  // Record the latest dimensions of the decoded input.
  frame_sizes_.push_back(*output_size);

  if (tunnel_mode_audio_session_id_) {
    return;
  }
  if (first_output_format_changed_) {
    // After resolution changes, the output buffers may have frames of different
    // resolutions. In that case, it's hard to determine the max supported
    // output buffers. So, we reset |output_format_| to null here to skip max
    // output buffers check.
    output_format_ = std::nullopt;
    return;
  }
  output_format_ =
      VideoOutputFormat(video_codec_, frame_sizes_.back().display_size,
                        color_metadata_.has_value());
  first_output_format_changed_ = true;
  auto max_output_buffers =
      MaxMediaCodecOutputBuffersLookupTable::GetInstance()
          ->GetMaxOutputVideoBuffers(output_format_.value());
  if (max_output_buffers > 0 &&
      max_output_buffers <
          static_cast<int>(initial_number_of_preroll_frames_)) {
    number_of_preroll_frames_ = max_output_buffers;
  }
}

bool MediaCodecVideoDecoder::Tick(MediaCodec* media_codec_bridge) {
  // Tunnel mode renders frames in MediaCodec automatically and shouldn't reach
  // here.
  SB_DCHECK(!tunnel_mode_audio_session_id_);
  return sink_->Render();
}

void MediaCodecVideoDecoder::OnFlushing() {
  SB_CHECK(BelongsToCurrentThread());

  if (decoder_status_cb_) {
    decoder_status_cb_(kReleaseAllFrames, NULL);
  }
}

bool MediaCodecVideoDecoder::IsBufferDecodeOnly(
    const scoped_refptr<InputBuffer>& input_buffer) {
  if (!is_video_frame_tracker_enabled_) {
    return false;
  }

  SB_CHECK(video_frame_tracker_);
  return input_buffer->timestamp() < video_frame_tracker_->seek_to_time();
}

void MediaCodecVideoDecoder::TryToSignalPrerollForTunnelMode() {
  if (!tunnel_mode_first_frame_rendered_ ||
      tunnel_mode_prerolled_frames_ < kTunnelModePrerollFrameCount) {
    return;
  }

  if (tunnel_mode_prerolling_.exchange(false)) {
    SB_LOG(ERROR) << "Tunnel mode preroll finished.";
    // TODO: Currently the decoder sends a dummy frame to the renderer to signal
    //       preroll finish.  We should investigate a better way for prerolling
    //       when the video is rendered directly by the decoder, maybe by always
    //       sending placeholder frames.
    decoder_status_cb_(kNeedMoreInput,
                       new VideoFrame(video_frame_tracker_->seek_to_time()));
  }
}

void MediaCodecVideoDecoder::OnFrameRendered(int64_t frame_timestamp) {
  SB_DCHECK(is_video_frame_tracker_enabled_);
  SB_DCHECK(video_frame_tracker_);

  video_frame_tracker_->OnFrameRendered(frame_timestamp);
}

void MediaCodecVideoDecoder::OnFirstTunnelFrameReady() {
  SB_DCHECK(tunnel_mode_audio_session_id_);

  tunnel_mode_first_frame_rendered_.store(true);
  TryToSignalPrerollForTunnelMode();
}

void MediaCodecVideoDecoder::OnTunnelModeCheckForNeedMoreInput() {
  SB_CHECK(BelongsToCurrentThread());
  SB_DCHECK(tunnel_mode_audio_session_id_);

  // There's a race condition when suspending the app. If surface view is
  // destroyed before this function is called, |media_decoder_| could be null
  // here, in such case |media_decoder_| will be null.
  if (!media_decoder_) {
    SB_LOG(INFO) << "Trying to call OnTunnelModeCheckForNeedMoreInput() when"
                 << " media_decoder_ is null.";
    return;
  }

  if (media_decoder_->GetNumberOfPendingInputs() < kMaxPendingInputsSize) {
    decoder_status_cb_(kNeedMoreInput, NULL);
    return;
  }

  Schedule(std::bind(&MediaCodecVideoDecoder::OnTunnelModeCheckForNeedMoreInput,
                     this),
           kNeedMoreInputCheckIntervalInTunnelMode);
}

void MediaCodecVideoDecoder::OnVideoFrameRelease() {
  if (output_format_) {
    --buffered_output_frames_;
    SB_DCHECK_GE(buffered_output_frames_, 0);
  }
}

void MediaCodecVideoDecoder::OnSurfaceDestroyed() {
  // When this function is called, the decoder no longer owns the surface.
  TeardownCodec();
}

void MediaCodecVideoDecoder::ReportError(SbPlayerError error,
                                         const std::string& error_message) {
  SB_DCHECK(error_cb_);

  if (!error_cb_) {
    return;
  }

  error_cb_(kSbPlayerErrorDecode, error_message);
}

void MediaCodecVideoDecoder::ResetInternal(bool skip_flush) {
  SB_CHECK(BelongsToCurrentThread());

  // If fail to flush |media_decoder_| or |media_decoder_| is null, then
  // re-create |media_decoder_|. If |needs_fps_to_initialize_codec_| is true,
  // set video_fps_ to 0 will call InitializeCodec(),
  // which we do not need if flush the codec.
  if (!enable_flush_during_seek_ || skip_flush || !media_decoder_ ||
      !media_decoder_->Flush()) {
    TeardownCodec();
    if (reset_delay_usec_ > 0) {
      usleep(reset_delay_usec_);
    }

    // Note that |input_buffer_written_| may not be strictly accurate after
    // Flush() since it counts all buffers written since codec initialization.
    // This is acceptable because it is used to estimate pre-roll frames, and
    // retaining its accumulated value correctly signals that we are past the
    // initial pre-roll phase after a Flush().
    input_buffer_written_ = 0;
    video_fps_ = 0;
  }
  CancelPendingJobs();

  // TODO(b/291959069): After flush |media_decoder_|, the output buffers
  // may have invalid frames. Reset |output_format_| to null here to skip max
  // output buffers check.
  decoded_output_frames_ = 0;
  output_format_ = std::nullopt;

  tunnel_mode_prerolling_.store(true);
  tunnel_mode_first_frame_rendered_.store(false);
  tunnel_mode_prerolled_frames_.store(0);
  end_of_stream_written_ = false;
  pending_input_buffers_.clear();

  // TODO: We rely on VideoRenderAlgorithmTunneled::Seek() to be called inside
  //       VideoRenderer::Seek() after calling MediaCodecVideoDecoder::Reset()
  //       to update the seek status of |video_frame_tracker_|.  This is
  //       slightly flaky as it depends on the behavior of the video renderer.
}

}  // namespace starboard
