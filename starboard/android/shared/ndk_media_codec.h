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

#include "starboard/android/shared/media_codec.h"
#include "starboard/common/pass_key.h"
#include "starboard/common/result.h"
#include "starboard/common/size.h"

namespace starboard {

// NdkMediaCodec is an implementation of the MediaCodec interface using the
// Android NDK AMediaCodec API. It uses asynchronous callbacks for buffer
// availability and is preferred on supported devices (API 28+) for reduced JNI
// overhead.
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
      jobject j_surface,
      jobject j_media_crypto,
      const SbMediaColorMetadata* color_metadata,
      bool enable_frame_renderer_listener,
      bool require_secured_decoder,
      bool require_software_codec,
      int max_video_input_size);

  explicit NdkMediaCodec(PassKey<NdkMediaCodec>,
                         Handler* handler,
                         AMediaCodec* codec);
  ~NdkMediaCodec() override;

  DataSpan GetInputBufferAddress(jint index) override;
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

  DataSpan GetOutputBufferAddress(jint index) override;
  void ReleaseOutputBuffer(jint index, jboolean render) override;
  void ReleaseOutputBufferAtTimestamp(jint index,
                                      jlong render_timestamp_ns) override;

  void SetPlaybackRate(double playback_rate) override;
  bool Restart() override;
  jint Flush() override;
  std::optional<FrameSize> GetOutputSize() override;
  std::optional<AudioOutputFormatResult> GetAudioOutputFormat() override;

  void OnInputBufferAvailable(int32_t index);
  void OnOutputBufferAvailable(int32_t index, AMediaCodecBufferInfo* info);
  void OnFormatChanged(AMediaFormat* format);
  void OnError(media_status_t error, int32_t actionCode, const char* detail);
  void OnFrameRendered(int64_t presentation_time_us);

 private:
  Handler* const handler_;
  AMediaCodec* codec_ = nullptr;
  bool is_frame_rendered_callback_enabled_ = false;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_NDK_MEDIA_CODEC_H_
