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

#include "starboard/android/shared/exoplayer/exoplayer_bridge.h"

#include <jni.h>

#include <atomic>
#include <mutex>
#include <string>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "cobalt/android/jni_headers/ExoPlayerBridge_jni.h"
#include "starboard/android/shared/exoplayer/exoplayer_util.h"
#include "starboard/android/shared/starboard_bridge.h"
#include "starboard/common/check_op.h"
#include "starboard/common/instance_counter.h"
#include "starboard/common/log.h"
#include "starboard/media.h"
#include "starboard/player.h"
#include "starboard/shared/starboard/player/filter/common.h"
#include "starboard/shared/starboard/player/input_buffer_internal.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "cobalt/android/jni_headers/ExoPlayerManager_jni.h"
#pragma GCC diagnostic pop

namespace starboard {
namespace {

using base::android::ConvertJavaStringToUTF8;
using jni_zero::AttachCurrentThread;
using jni_zero::ScopedJavaGlobalRef;
using jni_zero::ScopedJavaLocalRef;

DECLARE_INSTANCE_COUNTER(ExoPlayerBridge)

constexpr int kADTSHeaderSize = 7;

constexpr bool kForceTunneledPlayback = false;

// Values from androidx.media3.common.C
constexpr int kResultNothingRead = -3;
constexpr int kResultBufferRead = -4;
constexpr int kBufferFlagKeyFrame = 1;
constexpr int kBufferFlagEndOfStream = 4;
constexpr int64_t kTimeEndOfSource = 0x8000000000000000LL;

// Custom signal for Java to allocate buffer space before reading
constexpr int kResultNeedsAllocation = -6;

int GetSampleOffset(SbMediaType type, scoped_refptr<InputBuffer> input_buffer) {
  if (type == kSbMediaTypeAudio &&
      input_buffer->audio_stream_info().codec == kSbMediaAudioCodecAac) {
    return kADTSHeaderSize;
  }
  return 0;
}

}  // namespace

ExoPlayerBridge::ExoPlayerBridge(
    const SbMediaAudioStreamInfo& audio_stream_info,
    const SbMediaVideoStreamInfo& video_stream_info,
    JobQueue* job_queue) {
  ON_INSTANCE_CREATED(ExoPlayerBridge);

  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_audio_format;
  if (audio_stream_info.codec != kSbMediaAudioCodecNone) {
    j_audio_format = CreateAudioFormat(audio_stream_info);
    if (!j_audio_format) {
      init_error_msg_ = "Could not create ExoPlayer audio MediaSource";
      SB_LOG(ERROR) << init_error_msg_;
      return;
    }
  }

  ScopedJavaLocalRef<jobject> j_video_format;
  ScopedJavaGlobalRef<jobject> j_output_surface;
  if (video_stream_info.codec != kSbMediaVideoCodecNone) {
    j_video_format = CreateVideoFormat(video_stream_info);
    if (!j_video_format) {
      init_error_msg_ = "Could not create ExoPlayer video MediaSource";
      SB_LOG(ERROR) << init_error_msg_;
      return;
    }

    AcquiredSurface acquired_surface = AcquireVideoSurface(job_queue);
    surface_destroy_notifier_ = acquired_surface.destroy_notifier;
    j_output_surface = acquired_surface.surface;
    if (!j_output_surface) {
      init_error_msg_ = "Could not acquire video surface for ExoPlayer";
      SB_LOG(ERROR) << init_error_msg_;
      return;
    }
    owns_surface_ = true;
  }

  ScopedJavaLocalRef<jobject> j_exoplayer_manager(
      StarboardBridge::GetInstance()->GetExoPlayerManager(env));
  if (!j_exoplayer_manager) {
    init_error_msg_ = "Failed to fetch ExoPlayerManager";
    SB_LOG(ERROR) << init_error_msg_;
    return;
  }

  ScopedJavaLocalRef<jobject> j_exoplayer_bridge =
      Java_ExoPlayerManager_createExoPlayerBridge(
          env, j_exoplayer_manager, reinterpret_cast<jlong>(this),
          j_audio_format, j_video_format, j_output_surface,
          (video_stream_info.codec != kSbMediaVideoCodecNone &&
           (audio_stream_info.codec == kSbMediaAudioCodecAc3 ||
            audio_stream_info.codec == kSbMediaAudioCodecEac3) &&
           ShouldEnableTunneledPlayback(video_stream_info)) ||
              kForceTunneledPlayback);
  if (!j_exoplayer_bridge) {
    init_error_msg_ = "Could not create Java ExoPlayerBridge";
    SB_LOG(ERROR) << init_error_msg_;
    return;
  }

  j_exoplayer_bridge_.Reset(j_exoplayer_bridge);
}

ExoPlayerBridge::~ExoPlayerBridge() {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  ON_INSTANCE_RELEASED(ExoPlayerBridge);
  player_is_releasing_.store(true);

  if (is_valid()) {
    Java_ExoPlayerBridge_release(AttachCurrentThread(), j_exoplayer_bridge_);
  }

  if (owns_surface_) {
    CleanUpVideoWindow(false);
    ReleaseVideoSurface();
    if (surface_destroy_notifier_) {
      surface_destroy_notifier_->Disconnect();
      surface_destroy_notifier_ = nullptr;
    }
  }
}

void ExoPlayerBridge::OnSurfaceDestroyed() {
  if (!player_is_releasing_.load()) {
    std::string msg = "ExoPlayer surface is destroyed before playback ended";
    SB_LOG(ERROR) << msg;
    playback_error_occurred_.store(true);
    ReportError(msg);
  }
}

bool ExoPlayerBridge::Init(ErrorCB error_cb,
                           PrerolledCB prerolled_cb,
                           EndedCB ended_cb) {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  SB_CHECK(error_cb);
  SB_CHECK(prerolled_cb);
  SB_CHECK(ended_cb);
  SB_CHECK(!error_cb_);
  SB_CHECK(!prerolled_cb_);
  SB_CHECK(!ended_cb_);

  error_cb_ = std::move(error_cb);
  prerolled_cb_ = std::move(prerolled_cb);
  ended_cb_ = std::move(ended_cb);

  return true;
}

void ExoPlayerBridge::Seek(int64_t timestamp) {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  if (ShouldAbortOperation()) {
    return;
  }

  {
    std::lock_guard lock(mutex_);
    pending_audio_samples_.clear();
    audio_eos_pending_ = false;

    pending_video_samples_.clear();
    video_eos_pending_ = false;
  }

  seeking_.store(true);
  Java_ExoPlayerBridge_seek(AttachCurrentThread(), j_exoplayer_bridge_,
                            timestamp);
}

void ExoPlayerBridge::WriteSamples(const InputBuffers& input_buffers,
                                   SbMediaType type) {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  // TODO: It's possible that a video sample may contain valid
  // SbMediaColorMetadata after codec creation. When that happens,
  // we should recreate the video decoder to use the new metadata.
  if (ShouldAbortOperation()) {
    return;
  }

  std::lock_guard lock(mutex_);
  auto& pending_samples = type == kSbMediaTypeAudio ? pending_audio_samples_
                                                    : pending_video_samples_;
  for (auto& input_buffer : input_buffers) {
    pending_samples.push_back(input_buffer);
  }
}

void ExoPlayerBridge::WriteEOS(SbMediaType type) {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  if (ShouldAbortOperation()) {
    return;
  }

  std::lock_guard lock(mutex_);
  if (type == kSbMediaTypeAudio) {
    audio_eos_pending_ = true;
  } else {
    video_eos_pending_ = true;
  }
}

void ExoPlayerBridge::SetPause(bool pause) const {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  if (ShouldAbortOperation()) {
    return;
  }

  JNIEnv* env = AttachCurrentThread();

  if (pause) {
    Java_ExoPlayerBridge_pause(env, j_exoplayer_bridge_);
  } else {
    Java_ExoPlayerBridge_play(env, j_exoplayer_bridge_);
  }
}

void ExoPlayerBridge::SetPlaybackRate(const double playback_rate) const {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  if (ShouldAbortOperation()) {
    return;
  }

  Java_ExoPlayerBridge_setPlaybackRate(AttachCurrentThread(),
                                       j_exoplayer_bridge_,
                                       static_cast<float>(playback_rate));
}

void ExoPlayerBridge::SetVolume(const double volume) const {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  if (ShouldAbortOperation()) {
    return;
  }

  Java_ExoPlayerBridge_setVolume(AttachCurrentThread(), j_exoplayer_bridge_,
                                 static_cast<float>(volume));
}

void ExoPlayerBridge::Stop() const {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  if (ShouldAbortOperation()) {
    return;
  }

  Java_ExoPlayerBridge_stop(AttachCurrentThread(), j_exoplayer_bridge_);
}

ExoPlayerBridge::MediaInfo ExoPlayerBridge::GetMediaInfo() const {
  if (ShouldAbortOperation()) {
    return MediaInfo();
  }

  int64_t media_time_usec = Java_ExoPlayerBridge_getCurrentPositionUsec(
      AttachCurrentThread(), j_exoplayer_bridge_);

  return MediaInfo{media_time_usec, dropped_frames_.load(), is_playing_.load()};
}

bool ExoPlayerBridge::CanAcceptMoreData(SbMediaType type) {
  SB_CHECK(thread_checker_.CalledOnValidThread());
  if (ShouldAbortOperation()) {
    return false;
  }

  std::lock_guard lock(mutex_);
  const auto& pending_samples = type == kSbMediaTypeAudio
                                    ? pending_audio_samples_
                                    : pending_video_samples_;

  if (pending_samples.empty()) {
    return true;
  }

  int64_t buffered_duration_us = pending_samples.back()->timestamp() -
                                 pending_samples.front()->timestamp();

  if (type == kSbMediaTypeAudio) {
    // Max buffer duration 5 seconds, memory pressure 250ms
    int64_t max_duration = 5 * 1000 * 1000 - 250 * 1000;
    if (buffered_duration_us >= max_duration) {
      return false;
    }
    return true;
  } else {
    int64_t max_duration = 3 * 1000 * 1000 - 250 * 1000;
    // Adjust max duration based on HDR or >1080p if available
    auto& info = pending_samples.front()->video_sample_info();
    bool is_hdr = info.stream_info.color_metadata.transfer ==
                  kSbMediaTransferIdSmpteSt2084;
    bool is_over_1080p = info.stream_info.frame_size.width > 1920 ||
                         info.stream_info.frame_size.height > 1080;
    if (is_hdr || is_over_1080p) {
      max_duration = 2 * 1000 * 1000 - 250 * 1000;
    }

    if (buffered_duration_us >= max_duration) {
      return false;
    }
    return true;
  }
}

int ExoPlayerBridge::ReadSample(JNIEnv* env,
                                jint j_type,
                                jobject j_buffer,
                                jlongArray j_metadata) {
  SbMediaType type = static_cast<SbMediaType>(j_type);
  std::lock_guard lock(mutex_);
  auto& pending_samples = type == kSbMediaTypeAudio ? pending_audio_samples_
                                                    : pending_video_samples_;
  bool eos_pending =
      type == kSbMediaTypeAudio ? audio_eos_pending_ : video_eos_pending_;

  if (pending_samples.empty()) {
    if (eos_pending) {
      jlong metadata[3] = {0, 0, kBufferFlagEndOfStream};
      env->SetLongArrayRegion(j_metadata, 0, std::size(metadata), metadata);
      return kResultBufferRead;
    }
    return kResultNothingRead;
  }

  auto input_buffer = pending_samples.front();
  int offset = GetSampleOffset(type, input_buffer);
  int size = input_buffer->size() - offset;

  bool is_key_frame = type == kSbMediaTypeAudio
                          ? true
                          : input_buffer->video_sample_info().is_key_frame;
  int flags = is_key_frame ? kBufferFlagKeyFrame : 0;
  if (eos_pending && pending_samples.size() == 1) {
    flags |= kBufferFlagEndOfStream;
  }
  jlong metadata[3] = {input_buffer->timestamp(), size, flags};

  if (!j_buffer) {
    env->SetLongArrayRegion(j_metadata, 0, std::size(metadata), metadata);
    return kResultNeedsAllocation;
  }

  void* direct_buffer = env->GetDirectBufferAddress(j_buffer);
  if (!direct_buffer) {
    ReportError("Failed to get direct buffer address");
    return kResultNothingRead;
  }

  jlong capacity = env->GetDirectBufferCapacity(j_buffer);
  if (capacity < size) {
    ReportError("Decoder buffer is too small");
    return kResultNothingRead;
  }

  memcpy(direct_buffer, input_buffer->data() + offset, size);
  env->SetLongArrayRegion(j_metadata, 0, std::size(metadata), metadata);

  pending_samples.pop_front();
  return kResultBufferRead;
}

bool ExoPlayerBridge::IsReady(JNIEnv* env, jint j_type) const {
  SbMediaType type = static_cast<SbMediaType>(j_type);
  std::lock_guard lock(const_cast<std::mutex&>(mutex_));
  const auto& pending_samples = type == kSbMediaTypeAudio
                                    ? pending_audio_samples_
                                    : pending_video_samples_;
  bool eos_pending =
      type == kSbMediaTypeAudio ? audio_eos_pending_ : video_eos_pending_;

  return !pending_samples.empty() || eos_pending;
}

int ExoPlayerBridge::SkipData(JNIEnv* env, jint j_type, jlong position_us) {
  SbMediaType type = static_cast<SbMediaType>(j_type);
  std::lock_guard lock(mutex_);
  auto& pending_samples = type == kSbMediaTypeAudio ? pending_audio_samples_
                                                    : pending_video_samples_;

  int skip_count = 0;
  while (!pending_samples.empty() &&
         pending_samples.front()->timestamp() < position_us) {
    pending_samples.pop_front();
    skip_count++;
  }
  return skip_count;
}

jlong ExoPlayerBridge::GetBufferedPositionUs(JNIEnv* env, jint j_type) const {
  SbMediaType type = static_cast<SbMediaType>(j_type);
  std::lock_guard lock(const_cast<std::mutex&>(mutex_));
  const auto& pending_samples = type == kSbMediaTypeAudio
                                    ? pending_audio_samples_
                                    : pending_video_samples_;
  bool eos_pending =
      type == kSbMediaTypeAudio ? audio_eos_pending_ : video_eos_pending_;

  if (eos_pending) {
    return kTimeEndOfSource;
  }
  if (pending_samples.empty()) {
    return 0;
  }
  return pending_samples.back()->timestamp();
}

void ExoPlayerBridge::OnInitialized(JNIEnv* env) {
  {
    std::lock_guard lock(mutex_);
    if (initialized_.exchange(true)) {
      SB_LOG(WARNING)
          << "ExoPlayer called OnInitialized again after initialization.";
      return;
    }
  }
}

void ExoPlayerBridge::OnReady(JNIEnv*) {
  if (seeking_.exchange(false)) {
    SB_CHECK(prerolled_cb_);
    prerolled_cb_();
  }
}

void ExoPlayerBridge::OnError(JNIEnv* env, jstring msg) {
  playback_error_occurred_.store(true);
  std::string error_msg = ConvertJavaStringToUTF8(env, msg);
  SB_LOG(ERROR) << "Reporting playback error: " << error_msg;
  ReportError(error_msg);
}

void ExoPlayerBridge::OnEnded(JNIEnv*) const {
  SB_CHECK(ended_cb_);
  ended_cb_();
}

void ExoPlayerBridge::OnDroppedVideoFrames(JNIEnv* env, jint count) {
  dropped_frames_.fetch_add(count);
}

void ExoPlayerBridge::OnIsPlayingChanged(JNIEnv*, jboolean is_playing) {
  is_playing_.store(is_playing);
}

bool ExoPlayerBridge::ShouldAbortOperation() const {
  if (playback_error_occurred_.load()) {
    SB_LOG(ERROR)
        << "Aborting ExoPlayer operation after a playback error occurred.";
    return true;
  }
  return false;
}

void ExoPlayerBridge::ReportError(const std::string& msg) const {
  SB_CHECK(error_cb_);
  error_cb_(kSbPlayerErrorDecode, msg);
}
}  // namespace starboard
