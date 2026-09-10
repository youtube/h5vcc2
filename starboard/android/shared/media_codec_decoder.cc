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

#include "starboard/android/shared/media_codec_decoder.h"

#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include <chrono>
#include <iterator>

#include "starboard/android/shared/media_common.h"
#include "starboard/android/shared/memfd_media_buffer_pool.h"
#include "starboard/audio_sink.h"
#include "starboard/common/experimental/media_buffer_pool.h"
#include "starboard/common/log.h"
#include "starboard/common/string.h"
#include "starboard/common/thread.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {
namespace {

using jni_zero::AttachCurrentThread;
using jni_zero::ScopedJavaLocalRef;

const jint kNoOffset = 0;
const jlong kNoPts = 0;
const jint kNoBufferFlags = 0;

constexpr int64_t kDefaultVideoDecoderPollIntervalUs = 1'000;         // 1 msec
constexpr int64_t kDefaultVideoDecoderTunnelPollIntervalUs = 10'000;  // 10 msec

const char* GetNameForMediaCodecStatus(jint status) {
  switch (status) {
    case MEDIA_CODEC_OK:
      return "MEDIA_CODEC_OK";
    case MEDIA_CODEC_DEQUEUE_INPUT_AGAIN_LATER:
      return "MEDIA_CODEC_DEQUEUE_INPUT_AGAIN_LATER";
    case MEDIA_CODEC_DEQUEUE_OUTPUT_AGAIN_LATER:
      return "MEDIA_CODEC_DEQUEUE_OUTPUT_AGAIN_LATER";
    case MEDIA_CODEC_OUTPUT_BUFFERS_CHANGED:
      return "MEDIA_CODEC_OUTPUT_BUFFERS_CHANGED";
    case MEDIA_CODEC_OUTPUT_FORMAT_CHANGED:
      return "MEDIA_CODEC_OUTPUT_FORMAT_CHANGED";
    case MEDIA_CODEC_INPUT_END_OF_STREAM:
      return "MEDIA_CODEC_INPUT_END_OF_STREAM";
    case MEDIA_CODEC_OUTPUT_END_OF_STREAM:
      return "MEDIA_CODEC_OUTPUT_END_OF_STREAM";
    case MEDIA_CODEC_NO_KEY:
      return "MEDIA_CODEC_NO_KEY";
    case MEDIA_CODEC_INSUFFICIENT_OUTPUT_PROTECTION:
      return "MEDIA_CODEC_INSUFFICIENT_OUTPUT_PROTECTION";
    case MEDIA_CODEC_ABORT:
      return "MEDIA_CODEC_ABORT";
    case MEDIA_CODEC_ERROR:
      return "MEDIA_CODEC_ERROR";
    default:
      SB_NOTREACHED() << "Unknown status value: " << status;
      return "MEDIA_CODEC_ERROR_UNKNOWN";
  }
}

const char* GetDecoderName(SbMediaType media_type) {
  return media_type == kSbMediaTypeAudio ? "audio_decoder" : "video_decoder";
}

}  // namespace

class MediaCodecDecoder::DecoderThread : public Thread {
 public:
  DecoderThread(std::string_view name,
                std::function<void()> runnable,
                std::optional<ThreadPriority> priority = std::nullopt)
      : Thread(name,
               priority ? ThreadOptions().SetPriority(priority.value())
                        : ThreadOptions()),
        runnable_(std::move(runnable)) {
    SB_CHECK(runnable_);
  }

  void Run() override { runnable_(); }

 private:
  const std::function<void()> runnable_;
};

// static
NonNullResult<std::unique_ptr<MediaCodecDecoder>>
MediaCodecDecoder::CreateForAudio(MediaCodec::Factory& media_codec_factory,
                                  JobQueue* job_queue,
                                  Host* host,
                                  const AudioStreamInfo& audio_stream_info,
                                  SbDrmSystem drm_system) {
  std::string error_message;
  auto decoder = std::make_unique<MediaCodecDecoder>(
      PassKey<MediaCodecDecoder>(), media_codec_factory, job_queue, host,
      audio_stream_info, drm_system, &error_message);
  if (!decoder->media_codec_bridge_) {
    return Failure(error_message);
  }
  return decoder;
}

// static
NonNullResult<std::unique_ptr<MediaCodecDecoder>>
MediaCodecDecoder::CreateForVideo(
    MediaCodec::Factory& media_codec_factory,
    JobQueue* job_queue,
    Host* host,
    SbMediaVideoCodec video_codec,
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
    bool enable_reuse_video_codec) {
  std::string error_message;
  auto decoder = std::make_unique<MediaCodecDecoder>(
      PassKey<MediaCodecDecoder>(), media_codec_factory, job_queue, host,
      video_codec, frame_size_hint, max_frame_size, fps, j_output_surface,
      drm_system, color_metadata, require_software_codec, frame_rendered_cb,
      first_tunnel_frame_ready_cb, tunnel_mode_audio_session_id,
      enable_frame_renderer_listener, max_video_input_size, flush_delay_usec,
      use_dual_threads, skip_video_frames_over_60_fps,
      ignore_mediacodec_callbacks_during_flushing, enable_ndk_video,
      enable_trivial_optimizations, enable_reuse_video_codec, &error_message);
  if (!decoder->media_codec_bridge_) {
    return Failure(error_message);
  }
  return decoder;
}

MediaCodecDecoder::MediaCodecDecoder(PassKey<MediaCodecDecoder>,
                                     MediaCodec::Factory& media_codec_factory,
                                     JobQueue* job_queue,
                                     Host* host,
                                     const AudioStreamInfo& audio_stream_info,
                                     SbDrmSystem drm_system,
                                     std::string* error_message)
    : JobOwner(job_queue),
      media_type_(kSbMediaTypeAudio),
      host_(host),
      drm_system_(static_cast<DrmSystem*>(drm_system)),
      tunnel_mode_enabled_(false),
      flush_delay_usec_(0),
      video_decoder_poll_interval_us_(kDefaultVideoDecoderPollIntervalUs) {
  SB_CHECK(host_);
  SB_CHECK(error_message);

  ScopedJavaLocalRef<jobject> j_media_crypto;
  if (drm_system_) {
    JNIEnv* env = AttachCurrentThread();
    j_media_crypto =
        ScopedJavaLocalRef<jobject>(env, drm_system_->GetMediaCrypto());
  }
  SB_DCHECK(!drm_system_ || j_media_crypto);
  media_codec_bridge_ = media_codec_factory.CreateAudioMediaCodec(
      audio_stream_info, this, j_media_crypto);
  if (!media_codec_bridge_) {
    *error_message = "Failed to create audio media codec.";
    SB_LOG(ERROR) << *error_message;
    return;
  }
  // When |audio_stream_info.codec| == kSbMediaAudioCodecOpus, we instead send
  // the audio specific configuration when we create the MediaCodec object in
  // the call to MediaCodec::CreateAudioMediaCodec() above.
  // TODO: Determine if we should send the audio specific configuration here
  // only when |audio_codec| == kSbMediaAudioCodecAac.
  if (audio_stream_info.codec != kSbMediaAudioCodecOpus &&
      !audio_stream_info.audio_specific_config.empty()) {
    pending_inputs_.emplace_back(audio_stream_info.audio_specific_config);
    ++number_of_pending_inputs_;
  }
}

MediaCodecDecoder::MediaCodecDecoder(
    PassKey<MediaCodecDecoder>,
    MediaCodec::Factory& media_codec_factory,
    JobQueue* job_queue,
    Host* host,
    SbMediaVideoCodec video_codec,
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
    std::string* error_message)
    : JobOwner(job_queue),
      media_type_(kSbMediaTypeVideo),
      host_(host),
      drm_system_(static_cast<DrmSystem*>(drm_system)),
      frame_rendered_cb_(frame_rendered_cb),
      first_tunnel_frame_ready_cb_(first_tunnel_frame_ready_cb),
      tunnel_mode_enabled_(tunnel_mode_audio_session_id.has_value()),
      flush_delay_usec_(flush_delay_usec),
      video_decoder_poll_interval_us_(
          tunnel_mode_enabled_ ? kDefaultVideoDecoderTunnelPollIntervalUs
                               : kDefaultVideoDecoderPollIntervalUs),
      use_dual_threads_(use_dual_threads),
      enable_trivial_optimizations_(enable_trivial_optimizations) {
  SB_DCHECK(frame_rendered_cb_);
  SB_DCHECK(first_tunnel_frame_ready_cb_);

  ScopedJavaLocalRef<jobject> j_media_crypto;
  if (drm_system_) {
    JNIEnv* env = AttachCurrentThread();
    j_media_crypto =
        ScopedJavaLocalRef<jobject>(env, drm_system_->GetMediaCrypto());
  }
  const bool require_secured_decoder =
      drm_system_ && drm_system_->require_secured_decoder();
  SB_DCHECK(!drm_system_ || j_media_crypto);

  auto media_codec_bridge = media_codec_factory.CreateVideoMediaCodec(
      video_codec, frame_size_hint, fps, max_frame_size,
      /*handler=*/this, j_output_surface, j_media_crypto, color_metadata,
      {max_video_input_size, skip_video_frames_over_60_fps,
       ignore_mediacodec_callbacks_during_flushing,
       enable_frame_renderer_listener, require_secured_decoder,
       require_software_codec, tunnel_mode_audio_session_id, enable_ndk_video,
       enable_reuse_video_codec});

  if (media_codec_bridge) {
    media_codec_bridge_ = std::move(media_codec_bridge.value());
  } else {
    *error_message = media_codec_bridge.error();
    SB_LOG(ERROR) << "Failed to create video media codec with error: "
                  << *error_message;
    return;
  }
  SB_LOG(INFO) << "MediaDecoder is created: tunnel_mode_enabled="
               << ToString(tunnel_mode_enabled_)
               << ", video_decoder_poll_interval(msec)="
               << video_decoder_poll_interval_us_ / 1'000
               << ", use_dual_threads=" << ToString(use_dual_threads_)
               << ", enable_trivial_optimizations="
               << ToString(enable_trivial_optimizations_);
}

MediaCodecDecoder::~MediaCodecDecoder() {
  SB_CHECK(thread_checker_.CalledOnValidThread());

  TerminateDecoderThread();

  // |media_codec_bridge_| can be null if the object is created in an invalid
  // state. Although the factory does not return such instances, the destructor
  // still needs to be able to safely handle them when they are destroyed after
  // a failed initialization.
  if (media_codec_bridge_) {
    host_->OnFlushing();
    // After |decoder_thread_| is ended and before |media_codec_bridge_| is
    // flushed, OnMediaCodecOutputBufferAvailable() would still be called.
    // So that, |dequeue_output_results_| may not be empty. As
    // DequeueOutputResult is consisted of plain data, it's fine to let
    // destructor delete |dequeue_output_results_|.
    jint status = media_codec_bridge_->Flush();
    if (status != MEDIA_CODEC_OK) {
      SB_LOG(ERROR) << "Failed to flush media codec.";
    }
  }
}

void MediaCodecDecoder::Initialize(const ErrorCB& error_cb) {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  SB_DCHECK(error_cb);
  SB_DCHECK(!error_cb_);

  error_cb_ = error_cb;

  if (error_occurred_) {
    Schedule(std::bind(error_cb_, error_, error_message_));
  }
}

void MediaCodecDecoder::WriteInputBuffers(const InputBuffers& input_buffers) {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  if (stream_ended_.load()) {
    SB_LOG(ERROR) << "Decode() is called after WriteEndOfStream() is called.";
    return;
  }
  if (input_buffers.empty()) {
    SB_LOG(ERROR) << "No input buffer to decode.";
    SB_DCHECK(!input_buffers.empty());
    return;
  }

  if (use_dual_threads_) {
    SB_DCHECK_EQ(media_type_, kSbMediaTypeVideo);
    if (!video_input_thread_) {
      video_input_thread_ = std::make_unique<DecoderThread>(
          "VidDecIn", [this] { InputThreadFunc(); });
      video_input_thread_->Start();
      video_output_thread_ = std::make_unique<DecoderThread>(
          "VidDecOut", [this] { OutputThreadFunc(); }, ThreadPriority::kHigh);
      video_output_thread_->Start();
    }
  } else if (!decoder_thread_) {
    decoder_thread_ =
        (media_type_ == kSbMediaTypeAudio)
            ? std::make_unique<DecoderThread>(
                  GetDecoderName(media_type_),
                  [this] { AudioDecoderThreadFunc(); }, ThreadPriority::kNormal)
            : std::make_unique<DecoderThread>(
                  GetDecoderName(media_type_),
                  [this] { VideoDecoderThreadFunc(); }, ThreadPriority::kHigh);
    decoder_thread_->Start();
  }

  std::lock_guard lock(mutex_);
  bool need_signal = pending_inputs_.empty();
  for (const auto& input_buffer : input_buffers) {
    pending_inputs_.emplace_back(input_buffer);
    ++number_of_pending_inputs_;
  }
  if (need_signal) {
    if (use_dual_threads_) {
      video_input_condition_variable_.notify_one();
    } else {
      condition_variable_.notify_one();
    }
  }
}

void MediaCodecDecoder::WriteEndOfStream() {
  SB_CHECK(thread_checker_.CalledOnValidThread());

  stream_ended_.store(true);
  std::lock_guard lock(mutex_);
  pending_inputs_.emplace_back(PendingInput::kWriteEndOfStream);
  ++number_of_pending_inputs_;
  if (pending_inputs_.size() == 1) {
    if (use_dual_threads_) {
      video_input_condition_variable_.notify_one();
    } else {
      condition_variable_.notify_one();
    }
  }
}

void MediaCodecDecoder::SetPlaybackRate(double playback_rate) {
  SB_DCHECK_EQ(media_type_, kSbMediaTypeVideo);
  media_codec_bridge_->SetPlaybackRate(playback_rate);
}

bool MediaCodecDecoder::Flush() {
  SB_CHECK(thread_checker_.CalledOnValidThread());

  // Try to flush if we can, otherwise return |false| to recreate the codec
  // completely. Flush() is called by `player_worker` thread,
  // but MediaCodecDecoder is on `audio_decoder` and `video_decoder`
  // threads, let `player_worker` destroy `audio_decoder` and
  // `video_decoder` threads to clean up all pending tasks,
  // and Flush()/Start() |media_codec_bridge_|.

  // 1. Terminate `audio_decoder` or `video_decoder` thread.
  TerminateDecoderThread();

  // 2. Flush()/Start() |media_codec_bridge_| and clean up pending tasks.
  // 2.1. Flush() |media_codec_bridge_|.
  host_->OnFlushing();
  jint status = media_codec_bridge_->Flush();
  if (status != MEDIA_CODEC_OK) {
    SB_LOG(ERROR) << "Failed to flush media codec.";
    return false;
  }

  // 2.2. Clean up pending_inputs and input_buffer/output_buffer indices.
  number_of_pending_inputs_.store(0);
  pending_inputs_.clear();
  input_buffer_indices_.clear();
  dequeue_output_results_.clear();
  pending_input_to_retry_ = std::nullopt;

  // 2.3. Add OutputFormatChanged to get current output format after Flush().
  DequeueOutputResult dequeue_output_result = {};
  dequeue_output_result.index = -1;
  dequeue_output_results_.push_back(dequeue_output_result);

  // 2.4. Wait for |flush_delay_usec_| on pre Android 13 devices.
  if (flush_delay_usec_ > 0) {
    usleep(flush_delay_usec_);
  }

  // 2.5. Restart() |media_codec_bridge_|. As the codec is configured in
  // asynchronous mode, call Start() after Flush() has returned to
  // resume codec operations. After Restart(), input_buffer_index should
  // start with 0.
  if (!media_codec_bridge_->Restart()) {
    SB_LOG(ERROR) << "Failed to start media codec.";
    return false;
  }

  // 3. Recreate `audio_decoder` and `video_decoder` threads in
  // WriteInputBuffers().
  stream_ended_.store(false);
  destroying_.store(false);
  return true;
}

void MediaCodecDecoder::AudioDecoderThreadFunc() {
  // Initialize() should be called before creating the thread, where `error_cb_`
  // is set.  Check `error_cb_` here to ensure Initialize() has been called.
  SB_DCHECK(error_cb_);
  SB_DCHECK_EQ(media_type_, kSbMediaTypeAudio);

  std::deque<PendingInput> pending_inputs;
  std::vector<int> input_buffer_indices;
  std::vector<DequeueOutputResult> dequeue_output_results;

  while (!destroying_.load()) {
    dequeue_output_results.clear();
    {
      std::unique_lock lock(mutex_);
      bool has_pending_input =
          !pending_inputs.empty() || !pending_inputs_.empty();
      bool has_input_buffer_indices =
          !input_buffer_indices.empty() || !input_buffer_indices_.empty();
      bool can_process_input = pending_input_to_retry_ ||
                               (has_pending_input && has_input_buffer_indices);
      if (dequeue_output_results_.empty() && !can_process_input) {
        // Wait for a signal or a timeout. We don't use a predicate here
        // because the complex conditions are already checked by the
        // surrounding loop, which will re-evaluate the state when this wait
        // returns.
        if (condition_variable_.wait_for(lock, std::chrono::seconds(5)) ==
            std::cv_status::timeout) {
          SB_LOG_IF(ERROR, !stream_ended_.load())
              << GetDecoderName(media_type_) << ": Wait() hits timeout.";
        }
      }
      SB_DCHECK(dequeue_output_results.empty());
      if (destroying_.load()) {
        break;
      }
      CollectPendingData_Locked(&pending_inputs, &input_buffer_indices,
                                &dequeue_output_results);
    }

    for (const auto& dequeue_output_result : dequeue_output_results) {
      if (dequeue_output_result.index < 0) {
        host_->RefreshOutputFormat(media_codec_bridge_.get());
      } else {
        host_->ProcessOutputBuffer(media_codec_bridge_.get(),
                                   dequeue_output_result,
                                   GetNumberOfPendingInputs());
      }
    }

    for (;;) {
      bool can_process_input =
          pending_input_to_retry_ ||
          (!pending_inputs.empty() && !input_buffer_indices.empty());
      if (!can_process_input) {
        break;
      }
      if (!ProcessOneInputBuffer(&pending_inputs, &input_buffer_indices)) {
        break;
      }
    }
  }

  SB_LOG(INFO) << "Destroying audio decoder thread.";
}

void MediaCodecDecoder::VideoDecoderThreadFunc() {
  // Initialize() should be called before creating the thread, where `error_cb_`
  // is set.  Check `error_cb_` here to ensure Initialize() has been called.
  SB_DCHECK(error_cb_);
  SB_DCHECK_EQ(media_type_, kSbMediaTypeVideo);

  // While it is possible to consolidate the logic for audio and video
  // decoders, it is easy to fine tune the behavior of video decoder if they
  // are separated.
  std::deque<PendingInput> pending_inputs;
  std::vector<int> input_buffer_indices;
  std::vector<DequeueOutputResult> dequeue_output_results;

  while (!destroying_.load()) {
    // TODO(b/329686979): access to `pending_input_to_retry_` should be
    //                    synchronized.
    bool has_input = pending_input_to_retry_ ||
                     (!pending_inputs.empty() && !input_buffer_indices.empty());
    bool has_output = !dequeue_output_results.empty();
    bool collect_pending_data = false;

    if (tunnel_mode_enabled_) {
      // We don't explicitly process output in tunnel mode.
      collect_pending_data = !has_input;
    } else {
      collect_pending_data = !has_input || !has_output;
    }

    if (destroying_.load()) {
      break;
    }
    if (collect_pending_data) {
      std::lock_guard lock(mutex_);
      CollectPendingData_Locked(&pending_inputs, &input_buffer_indices,
                                &dequeue_output_results);
    }

    if (!dequeue_output_results.empty()) {
      auto& dequeue_output_result = dequeue_output_results.front();
      if (dequeue_output_result.index < 0) {
        host_->RefreshOutputFormat(media_codec_bridge_.get());
      } else {
        SB_DCHECK(!tunnel_mode_enabled_);
        host_->ProcessOutputBuffer(media_codec_bridge_.get(),
                                   dequeue_output_result,
                                   GetNumberOfPendingInputs());
      }
      dequeue_output_results.erase(dequeue_output_results.begin());
    }
    if (!tunnel_mode_enabled_) {
      host_->Tick(media_codec_bridge_.get());
    }

    bool can_process_input =
        pending_input_to_retry_ ||
        (!pending_inputs.empty() && !input_buffer_indices.empty());
    if (can_process_input) {
      ProcessOneInputBuffer(&pending_inputs, &input_buffer_indices);
    }

    bool ticked = false;
    if (!tunnel_mode_enabled_) {
      // Output is only processed when tunnel mode is disabled.
      ticked = host_->Tick(media_codec_bridge_.get());
    }

    can_process_input =
        pending_input_to_retry_ ||
        (!pending_inputs.empty() && !input_buffer_indices.empty());
    if (!ticked && !can_process_input && dequeue_output_results.empty()) {
      std::unique_lock lock(mutex_);
      CollectPendingData_Locked(&pending_inputs, &input_buffer_indices,
                                &dequeue_output_results);
      can_process_input =
          !pending_inputs.empty() && !input_buffer_indices.empty();
      if (!can_process_input && dequeue_output_results.empty()) {
        condition_variable_.wait_for(
            lock, std::chrono::microseconds(video_decoder_poll_interval_us_));
      }
    }
  }

  SB_LOG(INFO) << "Destroying video decoder thread.";
}

void MediaCodecDecoder::InputThreadFunc() {
  // Initialize() should be called before creating the thread, where `error_cb_`
  // is set.  Check `error_cb_` here to ensure Initialize() has been called.
  SB_DCHECK(error_cb_);
  SB_DCHECK_EQ(media_type_, kSbMediaTypeVideo);

  std::deque<PendingInput> pending_inputs;
  std::vector<int> input_buffer_indices;

  auto can_process_input = [this, &pending_inputs, &input_buffer_indices] {
    return pending_input_to_retry_ ||
           (!pending_inputs.empty() && !input_buffer_indices.empty());
  };

  while (!destroying_.load()) {
    if (can_process_input()) {
      if (!ProcessOneInputBuffer(&pending_inputs, &input_buffer_indices)) {
        // Sleep for 1 ms to avoid busy looping.
        usleep(1'000);
      }
    } else {
      std::unique_lock lock(mutex_);
      if (pending_inputs_.empty() && input_buffer_indices_.empty()) {
        // Wait for up to one second.  Technically we can wait longer, picking
        // a reasonably small duration to avoid potential deadlock.
        video_input_condition_variable_.wait_for(lock, std::chrono::seconds(1));
      }
      CollectPendingInputData_Locked(&pending_inputs, &input_buffer_indices);
    }
  }
}

void MediaCodecDecoder::OutputThreadFunc() {
  // Initialize() should be called before creating the thread, where `error_cb_`
  // is set.  Check `error_cb_` here to ensure Initialize() has been called.
  SB_DCHECK(error_cb_);
  SB_DCHECK_EQ(media_type_, kSbMediaTypeVideo);

  std::vector<DequeueOutputResult> dequeue_output_results;

  while (!destroying_.load()) {
    bool can_process_output = !dequeue_output_results.empty();

    if (can_process_output) {
      auto& dequeue_output_result = dequeue_output_results.front();
      if (dequeue_output_result.index < 0) {
        host_->RefreshOutputFormat(media_codec_bridge_.get());
      } else if (!tunnel_mode_enabled_) {
        host_->ProcessOutputBuffer(media_codec_bridge_.get(),
                                   dequeue_output_result,
                                   GetNumberOfPendingInputs());
      }
      dequeue_output_results.erase(dequeue_output_results.begin());
    } else {
      std::lock_guard lock(mutex_);
      CollectPendingOutputData_Locked(&dequeue_output_results);
    }

    bool ticked = false;
    if (!tunnel_mode_enabled_) {
      ticked = host_->Tick(media_codec_bridge_.get());
    }

    if (!ticked && dequeue_output_results.empty()) {
      std::unique_lock lock(mutex_);
      CollectPendingOutputData_Locked(&dequeue_output_results);
      if (dequeue_output_results.empty()) {
        if (tunnel_mode_enabled_) {
          video_output_condition_variable_.wait(lock, [this] {
            return destroying_.load() || !dequeue_output_results_.empty();
          });
          CollectPendingOutputData_Locked(&dequeue_output_results);
        } else {
          // TODO(b/329686979): Allow Tick() to return the time to next frame
          // release to dynamically adjust the wait duration.
          video_output_condition_variable_.wait_for(
              lock, std::chrono::microseconds(8'000));
        }
      }
    }
  }
}

void MediaCodecDecoder::TerminateDecoderThread() {
  SB_CHECK(thread_checker_.CalledOnValidThread());

  destroying_.store(true);

  {
    std::lock_guard lock(mutex_);
    if (use_dual_threads_) {
      video_input_condition_variable_.notify_one();
      video_output_condition_variable_.notify_one();
    } else {
      condition_variable_.notify_one();
    }
  }

  if (video_input_thread_) {
    SB_DCHECK(use_dual_threads_);
    video_input_thread_->Join();
    video_input_thread_.reset();
  }

  if (video_output_thread_) {
    SB_DCHECK(use_dual_threads_);
    video_output_thread_->Join();
    video_output_thread_.reset();
  }

  if (decoder_thread_) {
    decoder_thread_->Join();
    decoder_thread_.reset();
  }
}

void MediaCodecDecoder::CollectPendingData_Locked(
    std::deque<PendingInput>* pending_inputs,
    std::vector<int>* input_buffer_indices,
    std::vector<DequeueOutputResult>* dequeue_output_results) {
  CollectPendingInputData_Locked(pending_inputs, input_buffer_indices);
  CollectPendingOutputData_Locked(dequeue_output_results);
}

void MediaCodecDecoder::CollectPendingInputData_Locked(
    std::deque<PendingInput>* pending_inputs,
    std::vector<int>* input_buffer_indices) {
  SB_DCHECK(pending_inputs);
  SB_DCHECK(input_buffer_indices);

  if (enable_trivial_optimizations_) {
    pending_inputs->insert(pending_inputs->end(),
                           std::make_move_iterator(pending_inputs_.begin()),
                           std::make_move_iterator(pending_inputs_.end()));
  } else {
    pending_inputs->insert(pending_inputs->end(), pending_inputs_.begin(),
                           pending_inputs_.end());
  }
  pending_inputs_.clear();

  input_buffer_indices->insert(input_buffer_indices->end(),
                               input_buffer_indices_.begin(),
                               input_buffer_indices_.end());
  input_buffer_indices_.clear();
}

void MediaCodecDecoder::CollectPendingOutputData_Locked(
    std::vector<DequeueOutputResult>* dequeue_output_results) {
  SB_DCHECK(dequeue_output_results);

  dequeue_output_results->insert(dequeue_output_results->end(),
                                 dequeue_output_results_.begin(),
                                 dequeue_output_results_.end());
  dequeue_output_results_.clear();
}

bool MediaCodecDecoder::ProcessOneInputBuffer(
    std::deque<PendingInput>* pending_inputs,
    std::vector<int>* input_buffer_indices) {
  // During secure playback, and only secure playback, it is possible that our
  // attempt to enqueue an input buffer will be rejected by MediaCodec because
  // we do not have a key yet.  In this case, we hold on to the input buffer
  // that we have already set up, and repeatedly attempt to enqueue it until
  // it works.  Ideally, we would just wait until MediaDrm was ready, however
  // the shared starboard player framework assumes that it is possible to
  // perform decryption and decoding as separate steps, so from its
  // perspective, having made it to this point implies that we ready to
  // decode.  It is not possible to do them as separate steps on Android. From
  // the perspective of user application, decryption and decoding are one
  // atomic step.
  int input_buffer_index = -1;
  PendingInput pending_input;
  bool input_buffer_already_written = false;
  if (pending_input_to_retry_) {
    input_buffer_index = pending_input_to_retry_->input_buffer_index;
    if (enable_trivial_optimizations_) {
      pending_input = std::move(pending_input_to_retry_->pending_input);
    } else {
      pending_input = pending_input_to_retry_->pending_input;
    }
    pending_input_to_retry_ = std::nullopt;
    input_buffer_already_written = true;
  } else {
    input_buffer_index = input_buffer_indices->front();
    // TODO(b/514758473): Experiment popping the index from the back.
    input_buffer_indices->erase(input_buffer_indices->begin());
    if (enable_trivial_optimizations_) {
      pending_input = std::move(pending_inputs->front());
    } else {
      pending_input = pending_inputs->front();
    }
    pending_inputs->pop_front();
    --number_of_pending_inputs_;
  }
  SB_DCHECK_GE(input_buffer_index, 0);

  SB_DCHECK(pending_input.type == PendingInput::kWriteCodecConfig ||
            pending_input.type == PendingInput::kWriteInputBuffer ||
            pending_input.type == PendingInput::kWriteEndOfStream);
  const scoped_refptr<InputBuffer>& input_buffer = pending_input.input_buffer;
  if (pending_input.type == PendingInput::kWriteEndOfStream) {
    SB_DCHECK(pending_inputs->empty());
  }
  const void* data = NULL;
  int size = 0;
  if (pending_input.type == PendingInput::kWriteCodecConfig) {
    SB_DCHECK_EQ(media_type_, kSbMediaTypeAudio);
    data = pending_input.codec_config.data();
    size = pending_input.codec_config.size();
  } else if (pending_input.type == PendingInput::kWriteInputBuffer) {
    data = input_buffer->data();
    size = input_buffer->size();
  } else if (pending_input.type == PendingInput::kWriteEndOfStream) {
    data = NULL;
    size = 0;
  }

  // Don't bother rewriting the same data if we already did it last time we
  // were called and had it stored in |pending_input_to_retry_|.
  if (!input_buffer_already_written &&
      pending_input.type != PendingInput::kWriteEndOfStream) {
    const auto codec_input_buffer =
        media_codec_bridge_->GetInputBufferAddress(input_buffer_index);
    auto* address = codec_input_buffer.data();
    if (!address) {
      SB_LOG(ERROR) << "Unable to get MediaCodec input buffer address.";
      // There could be dirty callbacks right after flush, thus the
      // MediaCodec.InputBuffer could be unavailable. In that case, we should
      // re-write the input buffer with a different MediaCodec.InputBuffer.
      if (enable_trivial_optimizations_) {
        pending_inputs->emplace(pending_inputs->begin(),
                                std::move(pending_input));
      } else {
        pending_inputs->emplace(pending_inputs->begin(), pending_input);
      }
      number_of_pending_inputs_++;
      return false;
    }

    const auto capacity = codec_input_buffer.size();
    if (capacity < static_cast<size_t>(size)) {
      auto error_message = FormatString(
          "Unable to write to MediaCodec buffer, input buffer size (%d) is"
          " greater than buffer capacity (%zu).",
          size, capacity);
      SB_LOG(ERROR) << error_message;
      ReportError(kSbPlayerErrorDecode, error_message);
      return false;
    }

    SB_DCHECK_GE(size, 0);
    SB_DCHECK_LE(static_cast<size_t>(size), capacity);

    using ::starboard::experimental::IsPointerAnnotated;
    using ::starboard::experimental::UnannotatePointer;

    if (IsPointerAnnotated(data)) {
      auto* pool = MemFdMediaBufferPool::Get();
      SB_DCHECK(pool);

      // Unannotate the pointer here to convert it into the position the
      // extension understands.
      intptr_t position = UnannotatePointer(reinterpret_cast<intptr_t>(data));
      pool->Read(position, address, size);
    } else {
      memcpy(address, data, size);
    }
  }

  jint status;
  if (drm_system_ && !drm_system_->IsReady()) {
    // Drm system initialization is asynchronous. If there's a drm system, we
    // should wait until it's initialized to avoid errors.
    status = MEDIA_CODEC_NO_KEY;
  } else if (pending_input.type == PendingInput::kWriteCodecConfig) {
    status = media_codec_bridge_->QueueInputBuffer(
        input_buffer_index, kNoOffset, size, kNoPts,
        MediaCodec::kBufferFlagCodecConfig, /*is_decode_only=*/false);
  } else if (pending_input.type == PendingInput::kWriteInputBuffer) {
    jlong pts_us = input_buffer->timestamp();
    if (drm_system_ && input_buffer->drm_info()) {
      status = media_codec_bridge_->QueueSecureInputBuffer(
          input_buffer_index, kNoOffset, *input_buffer->drm_info(), pts_us,
          host_->IsBufferDecodeOnly(input_buffer));
    } else {
      status = media_codec_bridge_->QueueInputBuffer(
          input_buffer_index, kNoOffset, size, pts_us, kNoBufferFlags,
          host_->IsBufferDecodeOnly(input_buffer));
    }
  } else {
    status = media_codec_bridge_->QueueInputBuffer(
        input_buffer_index, kNoOffset, size, kNoPts,
        MediaCodec::kBufferFlagEndOfStream, /*is_decode_only=*/false);
    host_->OnEndOfStreamWritten(media_codec_bridge_.get());
  }

  if (status != MEDIA_CODEC_OK) {
    HandleError("queue(Secure)?InputBuffer", status);
    // TODO: Stop the decoding loop and call error_cb_ on fatal error.
    SB_DCHECK(!pending_input_to_retry_);
    if (enable_trivial_optimizations_) {
      pending_input_to_retry_ = {input_buffer_index, std::move(pending_input)};
    } else {
      pending_input_to_retry_ = {input_buffer_index, pending_input};
    }
    return false;
  }

  is_output_restricted_ = false;
  return true;
}

void MediaCodecDecoder::HandleError(const char* action_name, jint status) {
  SB_DCHECK_NE(status, MEDIA_CODEC_OK);

  bool retry = false;

  if (status != MEDIA_CODEC_INSUFFICIENT_OUTPUT_PROTECTION) {
    is_output_restricted_ = false;
  }

  if (status == MEDIA_CODEC_DEQUEUE_INPUT_AGAIN_LATER) {
    // Don't bother logging a try again later status, it happens a lot.
    return;
  } else if (status == MEDIA_CODEC_DEQUEUE_OUTPUT_AGAIN_LATER) {
    // Don't bother logging a try again later status, it will happen a lot.
    return;
  } else if (status == MEDIA_CODEC_NO_KEY) {
    retry = true;
  } else if (status == MEDIA_CODEC_INSUFFICIENT_OUTPUT_PROTECTION) {
    // TODO: Reduce the retry frequency when output is restricted, or when
    // queueSecureInputBuffer() is failed in general.
    if (is_output_restricted_) {
      return;
    }
    is_output_restricted_ = true;
    drm_system_->OnInsufficientOutputProtection();
  } else {
    if (media_type_ == kSbMediaTypeAudio) {
      ReportError(kSbPlayerErrorDecode,
                  FormatString("%s failed with status %d (audio).", action_name,
                               status));
    } else {
      ReportError(kSbPlayerErrorDecode,
                  FormatString("%s failed with status %d (video).", action_name,
                               status));
    }
  }

  if (retry) {
    SB_LOG(INFO) << "|" << action_name << "| failed with status: "
                 << GetNameForMediaCodecStatus(status)
                 << ", will try again after a delay.";
    sched_yield();
  } else {
    SB_LOG(ERROR) << "|" << action_name << "| failed with status: "
                  << GetNameForMediaCodecStatus(status) << ".";
  }
}

void MediaCodecDecoder::ReportError(const SbPlayerError error,
                                    const std::string error_message) {
  if (!BelongsToCurrentThread()) {
    Schedule(
        std::bind(&MediaCodecDecoder::ReportError, this, error, error_message));
    return;
  }
  if (error_occurred_) {
    // Avoid to report error multiple times.
    return;
  }
  error_occurred_ = true;
  error_ = error;
  error_message_ = error_message;
  if (error_cb_) {
    error_cb_(error_, error_message_);
  }
}

void MediaCodecDecoder::OnMediaCodecError(bool is_recoverable,
                                          bool is_transient,
                                          const std::string& error_message) {
  SB_LOG(WARNING) << "MediaCodecDecoder encountered "
                  << (is_recoverable ? "recoverable, " : "unrecoverable, ")
                  << (is_transient ? "transient " : "intransient ")
                  << " error with message: " << error_message;
  // The callback may be called on a different thread and before |error_cb_| is
  // initialized.
  if (!is_transient) {
    if (media_type_ == kSbMediaTypeAudio) {
      ReportError(kSbPlayerErrorDecode,
                  "OnMediaCodecError (audio): " + error_message +
                      (is_recoverable ? ", recoverable " : ", unrecoverable "));
    } else {
      ReportError(kSbPlayerErrorDecode,
                  "OnMediaCodecError (video): " + error_message +
                      (is_recoverable ? ", recoverable " : ", unrecoverable "));
    }
  }
}

void MediaCodecDecoder::OnMediaCodecInputBufferAvailable(int32_t buffer_index) {
  SB_CHECK_GE(buffer_index, 0);
  if (media_type_ == kSbMediaTypeVideo && first_call_on_handler_thread_) {
    // Set the thread priority of the Handler thread to dispatch the async
    // decoder callbacks to high.
    setpriority(PRIO_PROCESS, 0,
                ThreadPriorityToNiceValue(ThreadPriority::kHigh));
    first_call_on_handler_thread_ = false;
  }
  std::lock_guard lock(mutex_);
  input_buffer_indices_.push_back(buffer_index);
  if (input_buffer_indices_.size() == 1) {
    if (use_dual_threads_) {
      video_input_condition_variable_.notify_one();
    } else {
      condition_variable_.notify_one();
    }
  }
}

void MediaCodecDecoder::OnMediaCodecOutputBufferAvailable(
    int32_t buffer_index,
    int32_t flags,
    int32_t offset,
    int64_t presentation_time_us,
    int32_t size) {
  SB_CHECK_GE(buffer_index, 0);

  // TODO(b/291959069): After the output thread is destroyed, it may still
  // receive output buffer, discard this invalid output buffer.
  if (destroying_.load() || (!decoder_thread_ && !video_output_thread_)) {
    return;
  }

  DequeueOutputResult dequeue_output_result;
  dequeue_output_result.status = 0;
  dequeue_output_result.index = buffer_index;
  dequeue_output_result.flags = flags;
  dequeue_output_result.offset = offset;
  dequeue_output_result.presentation_time_microseconds = presentation_time_us;
  dequeue_output_result.num_bytes = size;

  std::lock_guard lock(mutex_);
  dequeue_output_results_.push_back(dequeue_output_result);
  if (use_dual_threads_) {
    video_output_condition_variable_.notify_one();
  } else {
    condition_variable_.notify_one();
  }
}

void MediaCodecDecoder::OnMediaCodecOutputFormatChanged() {
  DequeueOutputResult dequeue_output_result = {};
  dequeue_output_result.index = -1;

  std::lock_guard lock(mutex_);
  dequeue_output_results_.push_back(dequeue_output_result);
  if (use_dual_threads_) {
    video_output_condition_variable_.notify_one();
  } else {
    condition_variable_.notify_one();
  }
}

void MediaCodecDecoder::OnMediaCodecFrameRendered(int64_t frame_timestamp) {
  frame_rendered_cb_(frame_timestamp);
}

void MediaCodecDecoder::OnMediaCodecFirstTunnelFrameReady() {
  SB_DCHECK(tunnel_mode_enabled_);

  first_tunnel_frame_ready_cb_();
}

}  // namespace starboard
