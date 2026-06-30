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

#include "starboard/android/shared/media_codec_bridge.h"

#include "base/android/jni_array.h"
#include "base/android/jni_bytebuffer.h"
#include "base/android/jni_string.h"
#include "starboard/android/shared/media_capabilities_cache.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/common/check_op.h"
#include "starboard/common/media.h"
#include "starboard/common/string.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "third_party/jni_zero/jni_zero.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "cobalt/android/jni_headers/MediaCodecBridgeBuilder_jni.h"
#include "cobalt/android/jni_headers/MediaCodecBridge_jni.h"

namespace starboard {
namespace {

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaByteBufferToMutableSpan;
using base::android::ToJavaByteArray;
using base::android::ToJavaIntArray;
using jni_zero::AttachCurrentThread;
using jni_zero::JavaParamRef;
using jni_zero::ScopedJavaLocalRef;

// See
// https://developer.android.com/reference/android/media/MediaFormat.html#COLOR_RANGE_FULL.
const jint COLOR_RANGE_FULL = 1;
const jint COLOR_RANGE_LIMITED = 2;
// Not defined in MediaFormat. Represents unspecified color ID range.
const jint COLOR_RANGE_UNSPECIFIED = 0;

const jint COLOR_STANDARD_BT2020 = 6;
const jint COLOR_STANDARD_BT709 = 1;

const jint COLOR_TRANSFER_HLG = 7;
const jint COLOR_TRANSFER_SDR_VIDEO = 3;
const jint COLOR_TRANSFER_ST2084 = 6;

// A special value to represent that no mapping between an SbMedia* HDR
// metadata value and Android HDR metadata value is possible.  This value
// implies that HDR playback should not be attempted.
const jint COLOR_VALUE_UNKNOWN = -1;

jint SbMediaPrimaryIdToColorStandard(SbMediaPrimaryId primary_id) {
  switch (primary_id) {
    case kSbMediaPrimaryIdBt709:
      return COLOR_STANDARD_BT709;
    case kSbMediaPrimaryIdBt2020:
      return COLOR_STANDARD_BT2020;
    default:
      return COLOR_VALUE_UNKNOWN;
  }
}

jint SbMediaTransferIdToColorTransfer(SbMediaTransferId transfer_id) {
  switch (transfer_id) {
    case kSbMediaTransferIdBt709:
      return COLOR_TRANSFER_SDR_VIDEO;
    case kSbMediaTransferIdSmpteSt2084:
      return COLOR_TRANSFER_ST2084;
    case kSbMediaTransferIdAribStdB67:
      return COLOR_TRANSFER_HLG;
    default:
      return COLOR_VALUE_UNKNOWN;
  }
}

jint SbMediaRangeIdToColorRange(SbMediaRangeId range_id) {
  switch (range_id) {
    case kSbMediaRangeIdLimited:
      return COLOR_RANGE_LIMITED;
    case kSbMediaRangeIdFull:
      return COLOR_RANGE_FULL;
    case kSbMediaRangeIdUnspecified:
      return COLOR_RANGE_UNSPECIFIED;
    default:
      return COLOR_VALUE_UNKNOWN;
  }
}

}  // namespace

// static
std::unique_ptr<MediaCodecBridge> MediaCodecBridge::CreateAudioMediaCodec(
    const AudioStreamInfo& audio_stream_info,
    Handler* handler,
    const jni_zero::JavaRef<jobject>& j_media_crypto) {
  bool is_passthrough = false;
  const char* mime =
      SupportedAudioCodecToMimeType(audio_stream_info.codec, &is_passthrough);
  if (!mime) {
    SB_LOG(ERROR) << "Unsupported codec " << audio_stream_info.codec << ".";
    return nullptr;
  }

  std::string decoder_name =
      MediaCapabilitiesCache::GetInstance()->FindAudioDecoder(
          mime, /* bitrate = */ 0);

  if (decoder_name.empty()) {
    SB_LOG(ERROR) << "Failed to find decoder for " << audio_stream_info.codec
                  << ".";
    return nullptr;
  }

  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jbyteArray> configuration_data;
  if (audio_stream_info.codec == kSbMediaAudioCodecOpus &&
      !audio_stream_info.audio_specific_config.empty()) {
    configuration_data.Reset(
        ToJavaByteArray(env, audio_stream_info.audio_specific_config.data(),
                        audio_stream_info.audio_specific_config.size()));
  }

  std::unique_ptr<MediaCodecBridge> native_media_codec_bridge(
      new MediaCodecBridge(handler));
  ScopedJavaLocalRef<jobject> j_media_codec_bridge =
      Java_MediaCodecBridgeBuilder_createAudioDecoder(
          env, reinterpret_cast<jlong>(native_media_codec_bridge.get()),
          ConvertUTF8ToJavaString(env, mime),
          ConvertUTF8ToJavaString(env, decoder_name),
          audio_stream_info.samples_per_second,
          audio_stream_info.number_of_channels, j_media_crypto,
          configuration_data);

  if (!j_media_codec_bridge) {
    SB_LOG(ERROR) << "Failed to create codec bridge for "
                  << audio_stream_info.codec << ".";
    return nullptr;
  }

  SB_LOG(INFO) << __func__ << ": audio_stream_info=" << audio_stream_info;

  native_media_codec_bridge->Initialize(j_media_codec_bridge.obj());
  return native_media_codec_bridge;
}

// static
NonNullResult<std::unique_ptr<MediaCodecBridge>>
MediaCodecBridge::CreateVideoMediaCodec(
    SbMediaVideoCodec video_codec,
    const std::string& decoder_name,
    const char* mime,
    const Size& frame_size_hint,
    int fps,
    const std::optional<Size>& max_frame_size,
    Handler* handler,
    const jni_zero::JavaRef<jobject>& j_surface,
    const jni_zero::JavaRef<jobject>& j_media_crypto,
    const SbMediaColorMetadata* color_metadata,
    const MediaCodec::VideoPlatformOptions& platform_options) {
  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jobject> j_color_info(nullptr);
  if (color_metadata) {
    jint color_standard =
        SbMediaPrimaryIdToColorStandard(color_metadata->primaries);
    jint color_transfer =
        SbMediaTransferIdToColorTransfer(color_metadata->transfer);
    jint color_range = SbMediaRangeIdToColorRange(color_metadata->range);

    if (color_standard != COLOR_VALUE_UNKNOWN &&
        color_transfer != COLOR_VALUE_UNKNOWN &&
        color_range != COLOR_VALUE_UNKNOWN) {
      const auto& mastering_metadata = color_metadata->mastering_metadata;
      j_color_info.Reset(Java_ColorInfo_Constructor(
          env, color_range, color_standard, color_transfer,
          mastering_metadata.primary_r_chromaticity_x,
          mastering_metadata.primary_r_chromaticity_y,
          mastering_metadata.primary_g_chromaticity_x,
          mastering_metadata.primary_g_chromaticity_y,
          mastering_metadata.primary_b_chromaticity_x,
          mastering_metadata.primary_b_chromaticity_y,
          mastering_metadata.white_point_chromaticity_x,
          mastering_metadata.white_point_chromaticity_y,
          mastering_metadata.luminance_max, mastering_metadata.luminance_min,
          color_metadata->max_cll, color_metadata->max_fall,
          platform_options.force_big_endian_hdr_metadata));
    }
  }

  ScopedJavaLocalRef<jobject> j_create_media_codec_bridge_result(
      Java_CreateMediaCodecBridgeResult_Constructor(env));

  std::unique_ptr<MediaCodecBridge> native_media_codec_bridge(
      new MediaCodecBridge(handler));
  Java_MediaCodecBridge_createVideoMediaCodecBridge(
      env, reinterpret_cast<jlong>(native_media_codec_bridge.get()),
      ConvertUTF8ToJavaString(env, mime),
      ConvertUTF8ToJavaString(env, decoder_name), frame_size_hint.width,
      frame_size_hint.height, fps, max_frame_size ? max_frame_size->width : -1,
      max_frame_size ? max_frame_size->height : -1, j_surface, j_media_crypto,
      j_color_info,
      platform_options.tunnel_mode_audio_session_id.value_or(
          TUNNEL_MODE_AUDIO_SESSION_ID_NONE),
      platform_options.max_input_size,
      platform_options.enable_frame_renderer_listener,
      platform_options.skip_video_frames_over_60_fps,
      platform_options.ignore_mediacodec_callbacks_during_flushing,
      platform_options.enable_low_latency, j_create_media_codec_bridge_result);

  ScopedJavaLocalRef<jobject> j_media_codec_bridge(
      Java_CreateMediaCodecBridgeResult_mediaCodecBridge(
          env, j_create_media_codec_bridge_result));

  if (!j_media_codec_bridge) {
    ScopedJavaLocalRef<jstring> j_error_message(
        Java_CreateMediaCodecBridgeResult_errorMessage(
            env, j_create_media_codec_bridge_result));
    return Failure(ConvertJavaStringToUTF8(env, j_error_message));
  }

  SB_LOG(INFO) << __func__
               << ": video_codec=" << GetMediaVideoCodecName(video_codec)
               << ", frame_size_hint=" << frame_size_hint << ", fps=" << fps
               << ", max_frame_size=" << max_frame_size
               << ", has_color_metadata=" << ToString(!!color_metadata)
               << ", platform_options=" << platform_options;

  native_media_codec_bridge->Initialize(j_media_codec_bridge.obj());
  return native_media_codec_bridge;
}

MediaCodecBridge::MediaCodecBridge(Handler* handler) : handler_(handler) {
  SB_CHECK(handler_);
}

MediaCodecBridge::~MediaCodecBridge() {
  if (!j_media_codec_bridge_) {
    return;
  }

  JNIEnv* env = AttachCurrentThread();
  Java_MediaCodecBridge_release(env, j_media_codec_bridge_);
  if (jni_zero::ClearException(env)) {
    SB_LOG(WARNING) << "Ignored JNI exception during MediaCodecBridge release in teardown.";
  }
}

void MediaCodecBridge::Initialize(jobject j_media_codec_bridge) {
  SB_DCHECK(j_media_codec_bridge);

  JNIEnv* env = AttachCurrentThread();
  j_media_codec_bridge_.Reset(env, j_media_codec_bridge);
}

Span<uint8_t> MediaCodecBridge::GetInputBufferAddress(jint index) {
  SB_DCHECK_GE(index, 0);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> byte_buffer =
      Java_MediaCodecBridge_getInputBuffer(env, j_media_codec_bridge_, index);
  if (!byte_buffer) {
    return {};
  }
  auto span = JavaByteBufferToMutableSpan(env, byte_buffer.obj());
  return {span.data(), span.size()};
}

jint MediaCodecBridge::QueueInputBuffer(jint index,
                                        jint offset,
                                        jint size,
                                        jlong presentation_time_microseconds,
                                        jint flags,
                                        jboolean is_decode_only) {
  JNIEnv* env = AttachCurrentThread();
  return Java_MediaCodecBridge_queueInputBuffer(
      env, j_media_codec_bridge_, index, offset, size,
      presentation_time_microseconds, flags, is_decode_only);
}

jint MediaCodecBridge::QueueSecureInputBuffer(
    jint index,
    jint offset,
    const SbDrmSampleInfo& drm_sample_info,
    jlong presentation_time_microseconds,
    jboolean is_decode_only) {
  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jbyteArray> j_iv =
      ToJavaByteArray(env, drm_sample_info.initialization_vector,
                      drm_sample_info.initialization_vector_size);
  ScopedJavaLocalRef<jbyteArray> j_key_id = ToJavaByteArray(
      env, drm_sample_info.identifier, drm_sample_info.identifier_size);

  // Reshape the sub sample mapping like this:
  // [(c0, e0), (c1, e1), ...] -> [c0, c1, ...] and [e0, e1, ...]
  int32_t subsample_count = drm_sample_info.subsample_count;
  std::unique_ptr<jint[]> clear_bytes(new jint[subsample_count]);
  std::unique_ptr<jint[]> encrypted_bytes(new jint[subsample_count]);
  for (int i = 0; i < subsample_count; ++i) {
    clear_bytes[i] = drm_sample_info.subsample_mapping[i].clear_byte_count;
    encrypted_bytes[i] =
        drm_sample_info.subsample_mapping[i].encrypted_byte_count;
  }
  ScopedJavaLocalRef<jintArray> j_clear_bytes = ToJavaIntArray(
      env, base::span<const jint>(clear_bytes.get(),
                                  static_cast<size_t>(subsample_count)));
  ScopedJavaLocalRef<jintArray> j_encrypted_bytes = ToJavaIntArray(
      env, base::span<const jint>(encrypted_bytes.get(),
                                  static_cast<size_t>(subsample_count)));

  jint cipher_mode = CRYPTO_MODE_AES_CTR;
  jint blocks_to_encrypt = 0;
  jint blocks_to_skip = 0;
  if (drm_sample_info.encryption_scheme == kSbDrmEncryptionSchemeAesCbc) {
    cipher_mode = CRYPTO_MODE_AES_CBC;
    blocks_to_encrypt = drm_sample_info.encryption_pattern.crypt_byte_block;
    blocks_to_skip = drm_sample_info.encryption_pattern.skip_byte_block;
  }

  return Java_MediaCodecBridge_queueSecureInputBuffer(
      env, j_media_codec_bridge_, index, offset, j_iv, j_key_id, j_clear_bytes,
      j_encrypted_bytes, subsample_count, cipher_mode, blocks_to_encrypt,
      blocks_to_skip, presentation_time_microseconds, is_decode_only);
}

Span<uint8_t> MediaCodecBridge::GetOutputBufferAddress(jint index) {
  SB_DCHECK_GE(index, 0);
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> byte_buffer =
      Java_MediaCodecBridge_getOutputBuffer(env, j_media_codec_bridge_, index);
  if (!byte_buffer) {
    return {};
  }
  auto span = JavaByteBufferToMutableSpan(env, byte_buffer.obj());
  return {span.data(), span.size()};
}

void MediaCodecBridge::ReleaseOutputBuffer(jint index, jboolean render) {
  JNIEnv* env = AttachCurrentThread();
  Java_MediaCodecBridge_releaseOutputBuffer(env, j_media_codec_bridge_, index,
                                            render);
}

void MediaCodecBridge::ReleaseOutputBufferAtTimestamp(
    jint index,
    jlong render_timestamp_ns) {
  JNIEnv* env = AttachCurrentThread();
  Java_MediaCodecBridge_releaseOutputBufferAtTimestamp(
      env, j_media_codec_bridge_, index, render_timestamp_ns);
}

void MediaCodecBridge::SetPlaybackRate(double playback_rate) {
  JNIEnv* env = AttachCurrentThread();
  Java_MediaCodecBridge_setPlaybackRate(env, j_media_codec_bridge_,
                                        playback_rate);
}

bool MediaCodecBridge::Restart() {
  JNIEnv* env = AttachCurrentThread();
  return Java_MediaCodecBridge_restart(env, j_media_codec_bridge_) == JNI_TRUE;
}

jint MediaCodecBridge::Flush() {
  JNIEnv* env = AttachCurrentThread();
  jint status = Java_MediaCodecBridge_flush(env, j_media_codec_bridge_);
  if (jni_zero::ClearException(env)) {
    SB_LOG(ERROR) << "JNI exception occurred during MediaCodecBridge Flush.";
    return MEDIA_CODEC_ERROR;
  }
  return status;
}

std::optional<FrameSize> MediaCodecBridge::GetOutputSize() {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> result(
      Java_MediaCodecBridge_getOutputFormat(env, j_media_codec_bridge_));
  if (!result) {
    return std::nullopt;
  }

  return FrameSize({Java_MediaFormatWrapper_width(env, result),
                    Java_MediaFormatWrapper_height(env, result)},
                   Java_MediaFormatWrapper_formatHasCropValues(env, result));
}

std::optional<AudioOutputFormatResult>
MediaCodecBridge::GetAudioOutputFormat() {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> result(
      Java_MediaCodecBridge_getOutputFormat(env, j_media_codec_bridge_));
  if (!result) {
    return std::nullopt;
  }

  return AudioOutputFormatResult{
      Java_MediaFormatWrapper_sampleRate(env, result),
      Java_MediaFormatWrapper_channelCount(env, result),
  };
}

void MediaCodecBridge::OnMediaCodecError(
    JNIEnv* env,
    jboolean is_recoverable,
    jboolean is_transient,
    const JavaParamRef<jstring>& diagnostic_info) {
  std::string diagnostic_info_in_str =
      ConvertJavaStringToUTF8(env, diagnostic_info);
  handler_->OnMediaCodecError(is_recoverable, is_transient,
                              diagnostic_info_in_str);
}

void MediaCodecBridge::OnMediaCodecInputBufferAvailable(JNIEnv* env,
                                                        jint buffer_index) {
  handler_->OnMediaCodecInputBufferAvailable(buffer_index);
}

void MediaCodecBridge::OnMediaCodecOutputBufferAvailable(
    JNIEnv* env,
    jint buffer_index,
    jint flags,
    jint offset,
    jlong presentation_time_us,
    jint size) {
  handler_->OnMediaCodecOutputBufferAvailable(buffer_index, flags, offset,
                                              presentation_time_us, size);
}

void MediaCodecBridge::OnMediaCodecOutputFormatChanged(JNIEnv* env) {
  handler_->OnMediaCodecOutputFormatChanged();
}

void MediaCodecBridge::OnMediaCodecFrameRendered(
    JNIEnv* env,
    jlong presentation_time_us,
    jlong render_at_system_time_ns) {
  handler_->OnMediaCodecFrameRendered(presentation_time_us);
}

void MediaCodecBridge::OnMediaCodecFirstTunnelFrameReady(JNIEnv* env) {
  handler_->OnMediaCodecFirstTunnelFrameReady();
}

}  // namespace starboard
