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

#ifndef STARBOARD_ANDROID_SHARED_MEDIA_CODEC_DECODER_H_
#define STARBOARD_ANDROID_SHARED_MEDIA_CODEC_DECODER_H_

#include <atomic>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "starboard/android/shared/drm_system.h"
#include "starboard/android/shared/media_codec.h"
#include "starboard/common/pass_key.h"
#include "starboard/common/ref_counted.h"
#include "starboard/common/thread.h"
#include "starboard/media.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/filter/common.h"
#include "starboard/shared/starboard/player/input_buffer_internal.h"
#include "starboard/shared/starboard/player/job_queue.h"
#include "starboard/shared/starboard/thread_checker.h"

namespace starboard {

// TODO: Better encapsulation the MediaCodecBridge so the decoders no longer
//       need to talk directly to the MediaCodecBridge.
class MediaCodecDecoder final : private MediaCodec::Handler,
                                protected JobQueue::JobOwner {
 public:
  using FrameRenderedCB = std::function<void(int64_t)>;
  using FirstTunnelFrameReadyCB = std::function<void(void)>;

  MediaCodec* media_codec() const { return media_codec_bridge_.get(); }

  // This class should be implemented by the users of MediaCodecDecoder to
  // receive various notifications.  Note that all such functions are called on
  // the decoder thread.
  // TODO: Replace this with std::function<> based callbacks.
  class Host {
   public:
    virtual void ProcessOutputBuffer(MediaCodec* media_codec,
                                     const DequeueOutputResult& output,
                                     int number_of_pending_inputs) = 0;
    virtual void OnEndOfStreamWritten(MediaCodec* media_codec) = 0;
    virtual void RefreshOutputFormat(MediaCodec* media_codec) = 0;
    // This function gets called frequently on the decoding thread to give the
    // Host a chance to process when the MediaCodecDecoder is decoding.
    // TODO: Revise the scheduling logic to give the host a chance to process in
    //       a more elegant way.
    virtual bool Tick(MediaCodec* media_codec) = 0;
    // This function gets called before calling Flush() on the contained
    // MediaCodecBridge so the host can have a chance to do necessary cleanups
    // before the MediaCodecBridge is flushed.
    virtual void OnFlushing() = 0;

    virtual bool IsBufferDecodeOnly(
        const scoped_refptr<InputBuffer>& input_buffer) = 0;

   protected:
    ~Host() {}
  };

  static NonNullResult<std::unique_ptr<MediaCodecDecoder>> CreateForAudio(
      MediaCodec::Factory& media_codec_factory,
      JobQueue* job_queue,
      Host* host,
      const AudioStreamInfo& audio_stream_info,
      SbDrmSystem drm_system);
  static NonNullResult<std::unique_ptr<MediaCodecDecoder>> CreateForVideo(
      MediaCodec::Factory& media_codec_factory,
      JobQueue* job_queue,
      Host* host,
      SbMediaVideoCodec video_codec,
      // `frame_size_hint` is used to create the Android video format, which
      // doesn't have to be directly related to the resolution of the video.
      const Size& frame_size_hint,
      const std::optional<Size>& max_frame_size,
      int fps,
      const jni_zero::JavaRef<jobject>& j_output_surface,
      SbDrmSystem drm_system,
      const SbMediaColorMetadata* color_metadata,
      bool require_software_codec,
      const FrameRenderedCB& frame_rendered_cb,
      const FirstTunnelFrameReadyCB& first_tunnel_frame_ready_cb,
      std::optional<int> tunnel_mode_audio_session_id,
      bool enable_frame_renderer_listener,
      int max_video_input_size,
      int64_t flush_delay_usec,
      bool use_dual_threads,
      bool skip_video_frames_over_60_fps,
      bool ignore_mediacodec_callbacks_during_flushing,
      bool enable_ndk_video,
      bool enable_trivial_optimizations,
      bool enable_reuse_video_codec);

  MediaCodecDecoder(PassKey<MediaCodecDecoder>,
                    MediaCodec::Factory& media_codec_factory,
                    JobQueue* job_queue,
                    Host* host,
                    const AudioStreamInfo& audio_stream_info,
                    SbDrmSystem drm_system,
                    std::string* error_message);
  MediaCodecDecoder(
      PassKey<MediaCodecDecoder>,
      MediaCodec::Factory& media_codec_factory,
      JobQueue* job_queue,
      Host* host,
      SbMediaVideoCodec video_codec,
      // `frame_size_hint` is used to create the Android video format, which
      // doesn't have to be directly related to the resolution of the video.
      const Size& frame_size_hint,
      const std::optional<Size>& max_frame_size,
      int fps,
      const jni_zero::JavaRef<jobject>& j_output_surface,
      SbDrmSystem drm_system,
      const SbMediaColorMetadata* color_metadata,
      bool require_software_codec,
      const FrameRenderedCB& frame_rendered_cb,
      const FirstTunnelFrameReadyCB& first_tunnel_frame_ready_cb,
      std::optional<int> tunnel_mode_audio_session_id,
      bool enable_frame_renderer_listener,
      int max_video_input_size,
      int64_t flush_delay_usec,
      bool use_dual_threads,
      bool skip_video_frames_over_60_fps,
      bool ignore_mediacodec_callbacks_during_flushing,
      bool enable_ndk_video,
      bool enable_trivial_optimizations,
      bool enable_reuse_video_codec,
      std::string* error_message);
  ~MediaCodecDecoder();

  void Initialize(const ErrorCB& error_cb);
  void WriteInputBuffers(const InputBuffers& input_buffers);
  void WriteEndOfStream();

  void SetPlaybackRate(double playback_rate);

  size_t GetNumberOfPendingInputs() const {
    return number_of_pending_inputs_.load();
  }

  bool Flush();

 private:
  // Holding inputs to be processed.  They are mostly InputBuffer objects, but
  // can also be codec configs or end of streams.
  struct PendingInput {
    enum Type {
      kInvalid,
      kWriteCodecConfig,
      kWriteInputBuffer,
      kWriteEndOfStream,
    };

    explicit PendingInput(Type type = kInvalid) : type(type) {
      SB_DCHECK_NE(type, kWriteInputBuffer);
      SB_DCHECK_NE(type, kWriteCodecConfig);
    }
    explicit PendingInput(const std::vector<uint8_t>& codec_config)
        : type(kWriteCodecConfig), codec_config(codec_config) {
      SB_DCHECK(!this->codec_config.empty());
    }
    explicit PendingInput(scoped_refptr<InputBuffer> input_buffer)
        : type(kWriteInputBuffer), input_buffer(std::move(input_buffer)) {}

    Type type;
    scoped_refptr<InputBuffer> input_buffer;
    std::vector<uint8_t> codec_config;
  };

  // Holding an |input_buffer_index| and a PendingInput when a call to
  // QueueInputBuffer() or QueueSecureInputBuffer() fails so it can be retried
  // later.
  struct PendingInputToRetry {
    int input_buffer_index = -1;
    PendingInput pending_input;
  };

  class DecoderThread;
  void AudioDecoderThreadFunc();
  void VideoDecoderThreadFunc();

  // TODO(b/329686979): Consider turning MediaDecoder into a class hierarchy to
  // simplify the handling of threading, including the difference of a/v
  // threading in the original implementation in DecoderThreadFunc() above.
  void InputThreadFunc();

  void OutputThreadFunc();

  void TerminateDecoderThread();

  void CollectPendingData_Locked(
      std::deque<PendingInput>* pending_inputs,
      std::vector<int>* input_buffer_indices,
      std::vector<DequeueOutputResult>* dequeue_output_results);
  void CollectPendingInputData_Locked(std::deque<PendingInput>* pending_inputs,
                                      std::vector<int>* input_buffer_indices);
  void CollectPendingOutputData_Locked(
      std::vector<DequeueOutputResult>* dequeue_output_results);
  bool ProcessOneInputBuffer(std::deque<PendingInput>* pending_inputs,
                             std::vector<int>* input_buffer_indices);
  void HandleError(const char* action_name, jint status);
  void ReportError(const SbPlayerError error, const std::string error_message);

  // MediaCodec::Handler methods
  void OnMediaCodecError(bool is_recoverable,
                         bool is_transient,
                         const std::string& error_message) override;
  void OnMediaCodecInputBufferAvailable(int32_t buffer_index) override;
  void OnMediaCodecOutputBufferAvailable(int32_t buffer_index,
                                         int32_t flags,
                                         int32_t offset,
                                         int64_t presentation_time_us,
                                         int32_t size) override;
  void OnMediaCodecOutputFormatChanged() override;
  void OnMediaCodecFrameRendered(int64_t presentation_time_us) override;
  void OnMediaCodecFirstTunnelFrameReady() override;

  ThreadChecker thread_checker_;

  const SbMediaType media_type_;
  Host* const host_;
  DrmSystem* const drm_system_;
  const FrameRenderedCB frame_rendered_cb_;
  const FirstTunnelFrameReadyCB first_tunnel_frame_ready_cb_;
  const bool tunnel_mode_enabled_;
  const int64_t flush_delay_usec_;
  const int64_t video_decoder_poll_interval_us_;
  const bool use_dual_threads_ = false;
  const bool enable_trivial_optimizations_ = false;

  ErrorCB error_cb_;

  bool error_occurred_ = false;
  SbPlayerError error_;
  std::string error_message_;

  std::atomic_bool stream_ended_{false};

  std::atomic_bool destroying_{false};

  std::optional<PendingInputToRetry> pending_input_to_retry_;

  std::atomic<int32_t> number_of_pending_inputs_{0};

  std::mutex mutex_;
  std::condition_variable condition_variable_;
  // TODO(b/329686979): Consider guarding input and output logic using two
  // mutexes.
  // Only used when |use_dual_threads_| is true.
  std::condition_variable video_input_condition_variable_;
  // Only used when |use_dual_threads_| is true.
  std::condition_variable video_output_condition_variable_;
  std::deque<PendingInput> pending_inputs_;
  std::vector<int> input_buffer_indices_;
  std::vector<DequeueOutputResult> dequeue_output_results_;

  bool is_output_restricted_ = false;
  bool first_call_on_handler_thread_ = true;

  // Working threads to avoid lengthy decoding work block the player thread.
  std::unique_ptr<Thread> decoder_thread_;
  // Only used when |use_dual_threads_| is true.
  std::unique_ptr<Thread> video_input_thread_;
  // Only used when |use_dual_threads_| is true.
  std::unique_ptr<Thread> video_output_thread_;

  std::unique_ptr<MediaCodec> media_codec_bridge_;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_MEDIA_CODEC_DECODER_H_
