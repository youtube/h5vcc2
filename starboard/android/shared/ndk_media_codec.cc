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

#include "starboard/android/shared/ndk_media_codec.h"

#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <media/NdkMediaFormat.h>

#include "starboard/android/shared/media_common.h"
#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/string.h"

// Suppress availability warnings for API 28+ async callback symbols
#pragma clang diagnostic ignored "-Wunguarded-availability"

namespace starboard {

namespace {

struct AMediaCodecDeleter {
  void operator()(AMediaCodec* c) const { AMediaCodec_delete(c); }
};

struct AMediaFormatDeleter {
  void operator()(AMediaFormat* f) const { AMediaFormat_delete(f); }
};

struct ANativeWindowDeleter {
  void operator()(ANativeWindow* w) const { ANativeWindow_release(w); }
};

using ScopedAMediaCodec = std::unique_ptr<AMediaCodec, AMediaCodecDeleter>;
using ScopedAMediaFormat = std::unique_ptr<AMediaFormat, AMediaFormatDeleter>;
using ScopedANativeWindow =
    std::unique_ptr<ANativeWindow, ANativeWindowDeleter>;

// Action codes returned by AMediaCodecOnError callback, matching
// MediaCodec.CodecException constants.
// https://developer.android.com/reference/android/media/MediaCodec.CodecException#ACTION_TRANSIENT
constexpr int32_t kActionTransient = 1;
// https://developer.android.com/reference/android/media/MediaCodec.CodecException#ACTION_RECOVERABLE
constexpr int32_t kActionRecoverable = 2;

void OnFrameRenderedCallback(AMediaCodec* codec,
                             void* userdata,
                             int64_t mediaTimeUs,
                             int64_t systemNano) {
  auto* bridge = reinterpret_cast<NdkMediaCodec*>(userdata);
  bridge->OnFrameRendered(mediaTimeUs);
}

void OnInputBufferAvailableCallback(AMediaCodec* codec,
                                    void* userdata,
                                    int32_t index) {
  auto* bridge = reinterpret_cast<NdkMediaCodec*>(userdata);
  bridge->OnInputBufferAvailable(index);
}

void OnOutputBufferAvailableCallback(AMediaCodec* codec,
                                     void* userdata,
                                     int32_t index,
                                     AMediaCodecBufferInfo* info) {
  auto* bridge = reinterpret_cast<NdkMediaCodec*>(userdata);
  bridge->OnOutputBufferAvailable(index, info);
}

void OnFormatChangedCallback(AMediaCodec* codec,
                             void* userdata,
                             AMediaFormat* format) {
  auto* bridge = reinterpret_cast<NdkMediaCodec*>(userdata);
  bridge->OnFormatChanged(format);
}

void OnErrorCallback(AMediaCodec* codec,
                     void* userdata,
                     media_status_t error,
                     int32_t actionCode,
                     const char* detail) {
  auto* bridge = reinterpret_cast<NdkMediaCodec*>(userdata);
  bridge->OnError(error, actionCode, detail);
}

}  // namespace

std::unique_ptr<NdkMediaCodec> NdkMediaCodec::Create(
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
    int max_video_input_size) {
  const char* mime = SupportedVideoCodecToMimeType(video_codec);
  if (!mime) {
    SB_LOG(ERROR) << "Unsupported video codec: "
                  << GetMediaVideoCodecName(video_codec);
    return nullptr;
  }
  JNIEnv* env = jni_zero::AttachCurrentThread();

  ScopedAMediaCodec scoped_codec(
      AMediaCodec_createCodecByName(decoder_name.c_str()),
      AMediaCodecDeleter());
  if (!scoped_codec) {
    SB_LOG(ERROR) << "AMediaCodec_createCodecByName failed for "
                  << decoder_name;
    return nullptr;
  }

  ScopedAMediaFormat format(AMediaFormat_new(), AMediaFormatDeleter());
  AMediaFormat_setString(format.get(), AMEDIAFORMAT_KEY_MIME, mime);
  AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_WIDTH,
                        frame_size_hint.width);
  AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_HEIGHT,
                        frame_size_hint.height);

  if (max_frame_size) {
    AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_MAX_WIDTH,
                          max_frame_size->width);
    AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_MAX_HEIGHT,
                          max_frame_size->height);
  }
  if (max_video_input_size > 0) {
    AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_MAX_INPUT_SIZE,
                          max_video_input_size);
  }

  ScopedANativeWindow native_window(
      j_surface ? ANativeWindow_fromSurface(env, j_surface) : nullptr,
      ANativeWindowDeleter());

  if (j_surface && !native_window) {
    SB_LOG(ERROR) << "Failed to get ANativeWindow from jobject surface.";
    return nullptr;
  }

  media_status_t status = AMediaCodec_configure(
      scoped_codec.get(), format.get(), native_window.get(), /*crypto=*/nullptr,
      /*flags=*/0);

  if (status != AMEDIA_OK) {
    SB_LOG(ERROR) << "AMediaCodec_configure failed with status " << status;
    return nullptr;
  }

  AMediaCodec* codec = scoped_codec.release();
  auto bridge =
      std::make_unique<NdkMediaCodec>(PassKey<NdkMediaCodec>(), handler, codec);

  AMediaCodecOnAsyncNotifyCallback callbacks = {
      OnInputBufferAvailableCallback,
      OnOutputBufferAvailableCallback,
      OnFormatChangedCallback,
      OnErrorCallback,
  };
  status = AMediaCodec_setAsyncNotifyCallback(codec, callbacks, bridge.get());
  if (status != AMEDIA_OK) {
    SB_LOG(ERROR) << "AMediaCodec_setAsyncNotifyCallback failed with status "
                  << status;
    return nullptr;
  }

  status = AMediaCodec_start(codec);
  if (status != AMEDIA_OK) {
    SB_LOG(ERROR) << "AMediaCodec_start failed with status " << status;
    return nullptr;
  }

  if (enable_frame_renderer_listener &&
      AMediaCodec_setOnFrameRenderedCallback) {
    status = AMediaCodec_setOnFrameRenderedCallback(
        codec, OnFrameRenderedCallback, bridge.get());
    if (status == AMEDIA_OK) {
      bridge->is_frame_rendered_callback_enabled_ = true;
      SB_LOG(INFO) << "AMediaCodec_setOnFrameRenderedCallback successfully "
                      "registered.";
    } else {
      SB_LOG(WARNING) << "AMediaCodec_setOnFrameRenderedCallback failed with "
                         "status "
                      << status;
    }
  } else if (enable_frame_renderer_listener) {
    SB_LOG(WARNING) << "AMediaCodec_setOnFrameRenderedCallback fn not "
                       "found in libmediandk.so.";
  }

  SB_LOG(INFO) << "NdkMediaCodec created: dedoder=" << decoder_name
               << ", codec=" << GetMediaVideoCodecName(video_codec)
               << ", frame_size_hint=" << frame_size_hint
               << ", max_frame_size=" << ToString(max_frame_size);
  return bridge;
}

NdkMediaCodec::NdkMediaCodec(PassKey<NdkMediaCodec>,
                             Handler* handler,
                             AMediaCodec* codec)
    : handler_(handler), codec_(codec) {
  SB_CHECK(handler_);
}

NdkMediaCodec::~NdkMediaCodec() {
  // Clear callbacks before teardown to prevent use-after-free races.
  // In-flight background callbacks can outlive AMediaCodec_delete,
  // risking a crash if they attempt to access a partially destroyed `this`.
  AMediaCodec_setAsyncNotifyCallback(codec_, /*callback=*/{},
                                     /*userdata=*/nullptr);
  if (is_frame_rendered_callback_enabled_) {
    AMediaCodec_setOnFrameRenderedCallback(codec_, /*callback=*/nullptr,
                                           /*userdata=*/nullptr);
  }
  AMediaCodec_stop(codec_);
  AMediaCodec_delete(codec_);
}

DataSpan NdkMediaCodec::GetInputBufferAddress(jint index) {
  size_t capacity = 0;
  uint8_t* address = AMediaCodec_getInputBuffer(codec_, index, &capacity);
  return {address, capacity};
}

jint NdkMediaCodec::QueueInputBuffer(jint index,
                                     jint offset,
                                     jint size,
                                     jlong presentation_time_microseconds,
                                     jint flags,
                                     jboolean is_decode_only) {
  media_status_t status = AMediaCodec_queueInputBuffer(
      codec_, index, offset, size, presentation_time_microseconds, flags);
  return status == AMEDIA_OK ? MEDIA_CODEC_OK : MEDIA_CODEC_ERROR;
}

jint NdkMediaCodec::QueueSecureInputBuffer(
    jint index,
    jint offset,
    const SbDrmSampleInfo& drm_sample_info,
    jlong presentation_time_microseconds,
    jboolean is_decode_only) {
  SB_NOTREACHED() << "NdkMediaCodec does not support secure playback.";
  return MEDIA_CODEC_ERROR;
}

DataSpan NdkMediaCodec::GetOutputBufferAddress(jint index) {
  size_t capacity = 0;
  uint8_t* address = AMediaCodec_getOutputBuffer(codec_, index, &capacity);
  return {address, capacity};
}

void NdkMediaCodec::ReleaseOutputBuffer(jint index, jboolean render) {
  AMediaCodec_releaseOutputBuffer(codec_, index, render);
}

void NdkMediaCodec::ReleaseOutputBufferAtTimestamp(jint index,
                                                   jlong render_timestamp_ns) {
  AMediaCodec_releaseOutputBufferAtTime(codec_, index, render_timestamp_ns);
}

void NdkMediaCodec::SetPlaybackRate(double playback_rate) {
  SB_LOG(INFO) << "NdkMediaCodec::SetPlaybackRate: " << playback_rate;
  if (!codec_) {
    return;
  }
  ScopedAMediaFormat params(AMediaFormat_new(), AMediaFormatDeleter());
  AMediaFormat_setFloat(params.get(), AMEDIAFORMAT_KEY_OPERATING_RATE,
                        static_cast<float>(playback_rate));
  media_status_t status = AMediaCodec_setParameters(codec_, params.get());
  if (status != AMEDIA_OK) {
    SB_LOG(WARNING)
        << "AMediaCodec_setParameters failed to set operating-rate to "
        << playback_rate << " with status " << status;
  }
}

bool NdkMediaCodec::Restart() {
  media_status_t status = AMediaCodec_start(codec_);
  return status == AMEDIA_OK;
}

jint NdkMediaCodec::Flush() {
  media_status_t status = AMediaCodec_flush(codec_);
  return status == AMEDIA_OK ? MEDIA_CODEC_OK : MEDIA_CODEC_ERROR;
}

std::optional<FrameSize> NdkMediaCodec::GetOutputSize() {
  ScopedAMediaFormat format(AMediaCodec_getOutputFormat(codec_),
                            AMediaFormatDeleter());
  if (!format) {
    return std::nullopt;
  }
  int32_t width = 0;
  int32_t height = 0;
  int32_t crop_left = 0;
  int32_t crop_right = 0;
  int32_t crop_top = 0;
  int32_t crop_bottom = 0;

  bool has_crop =
      AMediaFormat_getInt32(format.get(), "crop-left", &crop_left) &&
      AMediaFormat_getInt32(format.get(), "crop-right", &crop_right) &&
      AMediaFormat_getInt32(format.get(), "crop-top", &crop_top) &&
      AMediaFormat_getInt32(format.get(), "crop-bottom", &crop_bottom);
  if (has_crop && crop_right >= crop_left && crop_bottom >= crop_top) {
    width = crop_right - crop_left + 1;
    height = crop_bottom - crop_top + 1;
  } else {
    AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_WIDTH, &width);
    AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_HEIGHT, &height);
  }

  return FrameSize({width, height}, has_crop);
}

std::optional<AudioOutputFormatResult> NdkMediaCodec::GetAudioOutputFormat() {
  SB_NOTREACHED() << "NdkMediaCodec does not support audio.";
  return std::nullopt;
}

void NdkMediaCodec::OnInputBufferAvailable(int32_t index) {
  handler_->OnMediaCodecInputBufferAvailable(index);
}

void NdkMediaCodec::OnOutputBufferAvailable(int32_t index,
                                            AMediaCodecBufferInfo* info) {
  handler_->OnMediaCodecOutputBufferAvailable(
      index, info->flags, info->offset, info->presentationTimeUs, info->size);
}

void NdkMediaCodec::OnFormatChanged(AMediaFormat* format) {
  handler_->OnMediaCodecOutputFormatChanged();
}

void NdkMediaCodec::OnError(media_status_t error,
                            int32_t actionCode,
                            const char* detail) {
  bool is_recoverable = (actionCode == kActionRecoverable);
  bool is_transient = (actionCode == kActionTransient);
  handler_->OnMediaCodecError(is_recoverable, is_transient,
                              detail ? detail : "NDK MediaCodec Error");
}

void NdkMediaCodec::OnFrameRendered(int64_t presentation_time_us) {
  handler_->OnMediaCodecFrameRendered(presentation_time_us);
}

}  // namespace starboard
