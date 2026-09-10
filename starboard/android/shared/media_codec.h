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

#ifndef STARBOARD_ANDROID_SHARED_MEDIA_CODEC_H_
#define STARBOARD_ANDROID_SHARED_MEDIA_CODEC_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>

#include "starboard/common/result.h"
#include "starboard/common/size.h"
#include "starboard/common/span.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {

// GENERATED_JAVA_ENUM_PACKAGE: dev.cobalt.media
// GENERATED_JAVA_PREFIX_TO_STRIP: MEDIA_CODEC_
enum MediaCodecStatus {
  MEDIA_CODEC_OK,
  MEDIA_CODEC_DEQUEUE_INPUT_AGAIN_LATER,
  MEDIA_CODEC_DEQUEUE_OUTPUT_AGAIN_LATER,
  MEDIA_CODEC_OUTPUT_BUFFERS_CHANGED,
  MEDIA_CODEC_OUTPUT_FORMAT_CHANGED,
  MEDIA_CODEC_INPUT_END_OF_STREAM,
  MEDIA_CODEC_OUTPUT_END_OF_STREAM,
  MEDIA_CODEC_NO_KEY,
  MEDIA_CODEC_INSUFFICIENT_OUTPUT_PROTECTION,
  MEDIA_CODEC_ABORT,
  MEDIA_CODEC_ERROR,

  MEDIA_CODEC_MAX = MEDIA_CODEC_ERROR,
};

struct FrameSize {
  Size display_size;
  bool has_crop_values = false;

  FrameSize();
  FrameSize(Size display_size, bool has_crop_values);
};

std::ostream& operator<<(std::ostream& os, const FrameSize& size);

struct AudioOutputFormatResult {
  jint sample_rate;
  jint channel_count;
};

struct DequeueOutputResult {
  int32_t status;
  int32_t index;
  int32_t flags;
  int32_t offset;
  int64_t presentation_time_microseconds;
  int32_t num_bytes;
};

// MediaCodec is an abstract interface for Android MediaCodec functionality,
// providing a unified API for both JNI-based (MediaCodecBridge) and NDK-based
// (NdkMediaCodec) implementations. It is typically owned by MediaCodecDecoder.
//
// This class is not thread-safe.
class MediaCodec {
 public:
  struct VideoPlatformOptions {
    int max_input_size = 0;
    bool skip_video_frames_over_60_fps = false;
    bool ignore_mediacodec_callbacks_during_flushing = false;
    bool enable_frame_renderer_listener = false;
    bool require_secured_decoder = false;
    bool require_software_codec = false;
    std::optional<int> tunnel_mode_audio_session_id;
    bool enable_ndk_video = false;
    bool enable_reuse_video_codec = false;
  };

  static constexpr int32_t kBufferFlagCodecConfig = 2;
  static constexpr int32_t kBufferFlagEndOfStream = 4;

  class Handler {
   public:
    virtual ~Handler() = default;
    virtual void OnMediaCodecInputBufferAvailable(int32_t index) = 0;
    virtual void OnMediaCodecOutputBufferAvailable(int32_t index,
                                                   int32_t flags,
                                                   int32_t offset,
                                                   int64_t presentation_time_us,
                                                   int32_t size) = 0;
    virtual void OnMediaCodecOutputFormatChanged() = 0;
    virtual void OnMediaCodecError(bool is_recoverable,
                                   bool is_transient,
                                   const std::string& error_message) = 0;
    virtual void OnMediaCodecFrameRendered(int64_t presentation_time_us) = 0;
    virtual void OnMediaCodecFirstTunnelFrameReady() = 0;
  };

  class Factory {
   public:
    virtual ~Factory() = default;
    virtual std::unique_ptr<MediaCodec> CreateAudioMediaCodec(
        const AudioStreamInfo& audio_stream_info,
        Handler* handler,
        const jni_zero::JavaRef<jobject>& j_media_crypto) = 0;
    virtual NonNullResult<std::unique_ptr<MediaCodec>> CreateVideoMediaCodec(
        SbMediaVideoCodec video_codec,
        const Size& frame_size_hint,
        int fps,
        const std::optional<Size>& max_frame_size,
        Handler* handler,
        const jni_zero::JavaRef<jobject>& j_surface,
        const jni_zero::JavaRef<jobject>& j_media_crypto,
        const SbMediaColorMetadata* color_metadata,
        const VideoPlatformOptions& platform_options) = 0;
  };

  static std::unique_ptr<MediaCodec> CreateAudioMediaCodec(
      const AudioStreamInfo& audio_stream_info,
      Handler* handler,
      const jni_zero::JavaRef<jobject>& j_media_crypto);

  static NonNullResult<std::unique_ptr<MediaCodec>> CreateVideoMediaCodec(
      SbMediaVideoCodec video_codec,
      const Size& frame_size_hint,
      int fps,
      const std::optional<Size>& max_frame_size,
      Handler* handler,
      const jni_zero::JavaRef<jobject>& j_surface,
      const jni_zero::JavaRef<jobject>& j_media_crypto,
      const SbMediaColorMetadata* color_metadata,
      const VideoPlatformOptions& platform_options);

  static bool IsFrameRenderedCallbackEnabled();

  virtual ~MediaCodec() = default;

  virtual Span<uint8_t> GetInputBufferAddress(jint index) = 0;
  virtual jint QueueInputBuffer(jint index,
                                jint offset,
                                jint size,
                                jlong presentation_time_microseconds,
                                jint flags,
                                jboolean is_decode_only) = 0;
  virtual jint QueueSecureInputBuffer(jint index,
                                      jint offset,
                                      const SbDrmSampleInfo& drm_sample_info,
                                      jlong presentation_time_microseconds,
                                      jboolean is_decode_only) = 0;

  virtual Span<uint8_t> GetOutputBufferAddress(jint index) = 0;
  virtual void ReleaseOutputBuffer(jint index, jboolean render) = 0;
  virtual void ReleaseOutputBufferAtTimestamp(jint index,
                                              jlong render_timestamp_ns) = 0;

  virtual void SetPlaybackRate(double playback_rate) = 0;
  virtual bool Restart() = 0;
  virtual jint Flush() = 0;
  virtual std::optional<FrameSize> GetOutputSize() = 0;
  virtual std::optional<AudioOutputFormatResult> GetAudioOutputFormat() = 0;
};

// DefaultMediaCodecFactory is the production implementation of
// MediaCodec::Factory. It delegates codec creation directly to JNI/NDK-based
// wrappers.
//
// This class is stateless and is thread-safe.
class DefaultMediaCodecFactory : public MediaCodec::Factory {
 public:
  DefaultMediaCodecFactory() = default;
  ~DefaultMediaCodecFactory() override = default;

  std::unique_ptr<MediaCodec> CreateAudioMediaCodec(
      const AudioStreamInfo& audio_stream_info,
      MediaCodec::Handler* handler,
      const jni_zero::JavaRef<jobject>& j_media_crypto) override;

  NonNullResult<std::unique_ptr<MediaCodec>> CreateVideoMediaCodec(
      SbMediaVideoCodec video_codec,
      const Size& frame_size_hint,
      int fps,
      const std::optional<Size>& max_frame_size,
      MediaCodec::Handler* handler,
      const jni_zero::JavaRef<jobject>& j_surface,
      const jni_zero::JavaRef<jobject>& j_media_crypto,
      const SbMediaColorMetadata* color_metadata,
      const MediaCodec::VideoPlatformOptions& platform_options) override;
};

std::ostream& operator<<(std::ostream& os,
                         const MediaCodec::VideoPlatformOptions& options);

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_MEDIA_CODEC_H_
