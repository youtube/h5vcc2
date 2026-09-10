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

#ifndef STARBOARD_ANDROID_SHARED_MEDIA_CODEC_VIDEO_DECODER_H_
#define STARBOARD_ANDROID_SHARED_MEDIA_CODEC_VIDEO_DECODER_H_

#include <jni.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "starboard/android/shared/decode_target.h"
#include "starboard/android/shared/drm_system.h"
#include "starboard/android/shared/max_media_codec_output_buffers_lookup_table.h"
#include "starboard/android/shared/media_codec.h"
#include "starboard/android/shared/media_codec_decoder.h"
#include "starboard/android/shared/video_frame_tracker.h"
#include "starboard/android/shared/video_surface_texture_bridge.h"
#include "starboard/android/shared/video_window.h"
#include "starboard/common/pass_key.h"
#include "starboard/common/ref_counted.h"
#include "starboard/common/result.h"
#include "starboard/common/size.h"
#include "starboard/decode_target.h"
#include "starboard/media.h"
#include "starboard/player.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/experimental_features.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/filter/video_decoder_internal.h"
#include "starboard/shared/starboard/player/filter/video_render_algorithm.h"
#include "starboard/shared/starboard/player/filter/video_renderer_sink.h"
#include "starboard/shared/starboard/player/input_buffer_internal.h"
#include "starboard/shared/starboard/player/job_queue.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {

class MediaCodecVideoDecoder : public VideoDecoder,
                               public MediaCodecDecoder::Host,
                               public VideoSurfaceTextureBridge::Host,
                               private JobQueue::JobOwner,
                               private VideoSurfaceHolder {
 public:
  struct StreamConfig {
    VideoStreamInfo video_stream_info;
    SbDrmSystem drm_system = kSbDrmSystemInvalid;
    SbPlayerOutputMode output_mode = kSbPlayerOutputModeInvalid;
    SbDecodeTargetGraphicsContextProvider*
        decode_target_graphics_context_provider = nullptr;
    void* surface_view = nullptr;
    std::string max_video_capabilities;
  };

  struct TunnelModeConfig {
    std::optional<int> audio_session_id;
    bool force_secure_pipeline = false;
  };

  struct PipelineConfig {
    int max_input_size = 0;
    bool enable_flush_during_seek = false;
    bool use_dual_threads = true;
    ExperimentalFeatures experimental_features;
  };

  struct PlatformOptions {
    int64_t reset_delay_usec = 0;
    int64_t flush_delay_usec = 0;
  };

  class Sink;

  static NonNullResult<std::unique_ptr<MediaCodecVideoDecoder>> Create(
      JobQueue* job_queue,
      const StreamConfig& stream_config,
      const TunnelModeConfig& tunnel_mode_config,
      const PipelineConfig& pipeline_config,
      const PlatformOptions& platform_options);

  static NonNullResult<std::unique_ptr<MediaCodecVideoDecoder>>
  CreateForTesting(std::unique_ptr<MediaCodec::Factory> media_codec_factory,
                   JobQueue* job_queue,
                   const StreamConfig& stream_config,
                   const TunnelModeConfig& tunnel_mode_config,
                   const PipelineConfig& pipeline_config,
                   const PlatformOptions& platform_options);

  MediaCodecVideoDecoder(
      PassKey<MediaCodecVideoDecoder>,
      std::unique_ptr<MediaCodec::Factory> media_codec_factory,
      JobQueue* job_queue,
      const StreamConfig& stream_config,
      const TunnelModeConfig& tunnel_mode_config,
      const PipelineConfig& pipeline_config,
      const PlatformOptions& platform_options,
      std::string* error_message);

  ~MediaCodecVideoDecoder() override;

  scoped_refptr<VideoRendererSink> GetSink();
  std::unique_ptr<VideoRenderAlgorithm> GetRenderAlgorithm();

  void Initialize(const DecoderStatusCB& decoder_status_cb,
                  const ErrorCB& error_cb) override;
  size_t GetPrerollFrameCount() const override;
  int64_t GetPrerollTimeout() const override;
  // As we hold output buffers received from MediaCodec, the max number of
  // cached frames depends on the max number of output buffers in MediaCodec,
  // which is device dependent. The media decoder may stall if we hold all
  // output buffers. But it would continue working once we release output
  // buffer.
  size_t GetMaxNumberOfCachedFrames() const override { return 12; }

  void WriteInputBuffers(const InputBuffers& input_buffers) override;
  void WriteEndOfStream() override;
  void Reset() override;
  void ResetForTeardown() override;
  SbDecodeTarget GetCurrentDecodeTarget() override;

  void UpdateDecodeTargetSizeAndContentRegion_Locked();
  void SetPlaybackRate(double playback_rate);

  void OnFrameAvailable() override;

 private:
  // Attempt to initialize the codec.
  Result<void> InitializeCodec(const VideoStreamInfo& video_stream_info);
  void TeardownCodec();

  void WriteInputBuffersInternal(const InputBuffers& input_buffers);
  void ProcessOutputBuffer(MediaCodec* media_codec_bridge,
                           const DequeueOutputResult& output,
                           int number_of_pending_inputs) override;
  void OnEndOfStreamWritten(MediaCodec* media_codec_bridge) override;
  void RefreshOutputFormat(MediaCodec* media_codec_bridge) override;
  bool Tick(MediaCodec* media_codec_bridge) override;
  void OnFlushing() override;
  bool IsBufferDecodeOnly(
      const scoped_refptr<InputBuffer>& input_buffer) override;

  void TryToSignalPrerollForTunnelMode();
  void OnFrameRendered(int64_t frame_timestamp);
  void OnFirstTunnelFrameReady();
  void OnTunnelModeCheckForNeedMoreInput();

  void OnVideoFrameRelease();

  void OnSurfaceDestroyed() override;
  void ReportError(SbPlayerError error, const std::string& error_message);

  void ResetInternal(bool skip_flush);

  // These variables will be initialized inside ctor or Initialize() and will
  // not be changed during the life time of this class.
  const SbMediaVideoCodec video_codec_;
  DecoderStatusCB decoder_status_cb_;
  ErrorCB error_cb_;
  DrmSystem* drm_system_;
  const SbPlayerOutputMode output_mode_;
  SbDecodeTargetGraphicsContextProvider* const
      decode_target_graphics_context_provider_;
  std::optional<Size> max_video_size_;

  // Android doesn't officially support multi concurrent codecs. But the device
  // usually has at least one hardware decoder and Google's software decoders.
  // Google's software decoders can work concurrently. So, we use HW decoder for
  // the main player and SW decoder for sub players.
  const bool require_software_codec_;

  const std::optional<int> tunnel_mode_audio_session_id_;

  // Set the maximum size in bytes of an input buffer for video.
  const int max_video_input_size_;

  // Enable the use of dual-threading for video decoders. This separates the
  // single threaded decoder thread into separate input and output processing
  // threads when enabled.
  const bool use_dual_threads_;

  // SurfaceView from AndroidOverlay passed from StarboardRenderer to SbPlayer.
  jni_zero::ScopedJavaGlobalRef<jobject> surface_view_;

  const bool enable_flush_during_seek_;
  const int64_t reset_delay_usec_;
  const int64_t flush_delay_usec_;
  const bool skip_flush_on_decoder_teardown_;

  // Codec initialization will be delayed until the decoder receives enough
  // inputs to estimate video fps when |needs_fps_to_initialize_codec_| is true.
  const bool needs_fps_to_initialize_codec_;
  // Workaround for b/506257255, which skips some video frames to make the
  // effective frame rate no more than 60 fps. This is only used for tunnel
  // mode.
  const bool skip_video_frames_over_60_fps_;

  // Enable the workaround to ignore stale/dirty MediaCodec callback messages
  // queued on the main thread during a flush.
  const bool ignore_mediacodec_callbacks_during_flushing_;
  const bool ignore_stale_rendered_frames_after_seek_;
  const bool enable_trivial_optimizations_;
  const bool enable_ndk_video_;
  const bool fix_need_more_input_backpressure_;
  const int max_pending_inputs_size_;

  // On some platforms tunnel mode is only supported in the secure pipeline.  So
  // we create a dummy drm system to force the video playing in secure pipeline
  // to enable tunnel mode.
  std::unique_ptr<DrmSystem> drm_system_to_enforce_tunnel_mode_;

  const bool is_video_frame_tracker_enabled_;
  std::unique_ptr<VideoFrameTracker> video_frame_tracker_;

  // Preroll in tunnel mode is handled in this class instead of in the renderer.
  std::atomic_bool tunnel_mode_prerolling_{true};
  std::atomic_bool tunnel_mode_first_frame_rendered_{false};
  std::atomic_int tunnel_mode_prerolled_frames_{0};

  // Since GetCurrentDecodeTarget() needs to be called from an arbitrary thread
  // to obtain the current decode target (which ultimately ends up being a
  // copy of |decode_target_|), we need to safe-guard access to |decode_target_|
  // and we do so through this mutex.
  std::mutex decode_target_mutex_;

  // If decode-to-texture is enabled, then we store the decode target texture
  // inside of this |decode_target_| member.
  DecodeTarget* decode_target_ = nullptr;

  // The size infos of the frames in use, i.e. the frames being displayed, and
  // the frames recently decoded frames and pending display.
  // They are the same at most of the time, but they can be different when there
  // is a format change. For example, when the newly decoded frames are at
  // 1080p, and the frames being played are still at 480p.
  std::vector<FrameSize> frame_sizes_;

  double playback_rate_ = 1.0;

  // The last enqueued |SbMediaColorMetadata|.
  std::optional<SbMediaColorMetadata> color_metadata_;

  // media_codec_factory_ cannot be null.
  const std::unique_ptr<MediaCodec::Factory> media_codec_factory_;
  std::unique_ptr<MediaCodecDecoder> media_decoder_;

  std::atomic<int32_t> number_of_frames_being_decoded_{0};
  scoped_refptr<Sink> sink_;

  int input_buffer_written_ = 0;
  bool first_texture_received_ = false;
  bool end_of_stream_written_ = false;
  volatile int64_t first_buffer_timestamp_;  // microseconds
  std::atomic_bool has_new_texture_available_{false};

  // Use |owns_video_surface_| only on decoder thread, to avoid unnecessary
  // invocation of ReleaseVideoSurface(), though ReleaseVideoSurface() would
  // do nothing if not own the surface.
  bool owns_video_surface_ = false;
  std::mutex surface_destroy_mutex_;
  std::condition_variable surface_condition_variable_;
  bool surface_destroyed_ = false;  // Guarded by |surface_destroy_mutex_|.

  std::vector<scoped_refptr<InputBuffer>> pending_input_buffers_;
  int video_fps_ = 0;

  // The variables below are used to calculate platform max supported MediaCodec
  // output buffers.
  int decoded_output_frames_ = 0;
  int buffered_output_frames_ = 0;
  int max_buffered_output_frames_ = 0;
  bool first_output_format_changed_ = false;
  std::optional<VideoOutputFormat> output_format_;
  const size_t initial_number_of_preroll_frames_;
  size_t number_of_preroll_frames_;

  static NonNullResult<std::unique_ptr<MediaCodecVideoDecoder>> CreateInternal(
      std::unique_ptr<MediaCodec::Factory> media_codec_factory,
      JobQueue* job_queue,
      const StreamConfig& stream_config,
      const TunnelModeConfig& tunnel_mode_config,
      const PipelineConfig& pipeline_config,
      const PlatformOptions& platform_options);

  const std::unique_ptr<VideoSurfaceTextureBridge> surface_texture_bridge_;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_MEDIA_CODEC_VIDEO_DECODER_H_
