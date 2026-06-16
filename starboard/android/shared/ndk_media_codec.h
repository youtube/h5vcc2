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

#ifndef STARBOARD_ANDROID_SHARED_NDK_MEDIA_CODEC_H_
#define STARBOARD_ANDROID_SHARED_NDK_MEDIA_CODEC_H_

#include <media/NdkMediaCodec.h>

#include <memory>
#include <optional>
#include <string>

#include "starboard/android/shared/frame_rate_estimator.h"
#include "starboard/android/shared/media_codec.h"
#include "starboard/common/pass_key.h"
#include "starboard/common/result.h"
#include "starboard/common/size.h"
#include "starboard/common/span.h"
#include "starboard/shared/starboard/player/job_thread.h"

namespace starboard {

// NdkMediaCodec is an implementation of the MediaCodec interface using the
// Android NDK AMediaCodec API. It uses asynchronous callbacks for buffer
// availability and is preferred on supported devices (API 28+) for reduced JNI
// overhead.
//
// Lifetime and Ownership:
// Objects of this class are typically owned and managed by the media player
// pipeline (e.g., via std::unique_ptr).
//
// Threading Model:
// This class is not thread-safe and is expected to be called on a single
// media/decoder thread (Thread-affine).
class NdkMediaCodec : public MediaCodec {
 public:
  using Handler = MediaCodec::Handler;

  static std::unique_ptr<NdkMediaCodec> Create(
      SbMediaVideoCodec video_codec,
      const std::string& decoder_name,
      const Size& frame_size_hint,
      int fps,
      const std::optional<Size>& max_frame_size,
      Handler* handler,
      const jni_zero::JavaRef<jobject>& j_surface,
      bool enable_frame_renderer_listener,
      int max_video_input_size);

  explicit NdkMediaCodec(PassKey<NdkMediaCodec>,
                         Handler* handler,
                         AMediaCodec* codec,
                         int fps);
  ~NdkMediaCodec() override;

  // Disallow copy and assignment.
  NdkMediaCodec(const NdkMediaCodec&) = delete;
  NdkMediaCodec& operator=(const NdkMediaCodec&) = delete;

  Span<uint8_t> GetInputBufferAddress(jint index) override;
  jint QueueInputBuffer(jint index,
                        jint offset,
                        jint size,
                        jlong presentation_time_microseconds,
                        jint flags,
                        jboolean is_decode_only) override;
  jint QueueSecureInputBuffer(jint index,
                              jint offset,
                              const SbDrmSampleInfo& drm_sample_info,
                              jlong presentation_time_microseconds,
                              jboolean is_decode_only) override;

  Span<uint8_t> GetOutputBufferAddress(jint index) override;
  void ReleaseOutputBuffer(jint index, jboolean render) override;
  void ReleaseOutputBufferAtTimestamp(jint index,
                                      jlong render_timestamp_ns) override;

  void SetPlaybackRate(double playback_rate) override;
  bool Restart() override;
  jint Flush() override;
  std::optional<FrameSize> GetOutputSize() override;
  std::optional<AudioOutputFormatResult> GetAudioOutputFormat() override;

 private:
  static void OnInputBufferAvailableCallback(AMediaCodec* codec,
                                             void* user_data,
                                             int32_t index);
  static void OnOutputBufferAvailableCallback(AMediaCodec* codec,
                                              void* user_data,
                                              int32_t index,
                                              AMediaCodecBufferInfo* info);
  static void OnFormatChangedCallback(AMediaCodec* codec,
                                      void* user_data,
                                      AMediaFormat* format);
  static void OnErrorCallback(AMediaCodec* codec,
                              void* user_data,
                              media_status_t error,
                              int32_t action_code,
                              const char* detail);
  static void OnFrameRenderedCallback(AMediaCodec* codec,
                                      void* user_data,
                                      int64_t media_time_us,
                                      int64_t system_time_ns);

  void OnInputBufferAvailable(int32_t index);
  void OnOutputBufferAvailable(int32_t index, AMediaCodecBufferInfo info);
  void OnFormatChanged();
  void OnError(media_status_t error,
               int32_t action_code,
               const std::string& detail);
  void OnFrameRendered(int64_t presentation_time_us);

  void UpdateOperatingRate();
  void UpdateFrameRate(int64_t presentation_time_us);

  Handler* const handler_;
  AMediaCodec* codec_ = nullptr;
  bool is_frame_rendered_callback_enabled_ = false;

  struct OperatingRateState {
    // The requested playback speed multiplier (e.g., 1.0, 2.0).
    double playback_speed = 1.0;

    // The nominal frame rate of the video (in fps), updated dynamically.
    int base_fps;

    // The calculated target operating rate (playback_speed * base_fps) last
    // set on MediaCodec, used to avoid redundant updates.
    double operating_fps = 0.0;
  };
  OperatingRateState rate_state_;

  FrameRateEstimator frame_rate_estimator_;

  std::unique_ptr<JobThread> job_thread_;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_NDK_MEDIA_CODEC_H_
