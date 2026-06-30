// Copyright 2021 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/android/shared/audio_track_bridge.h"

#include <algorithm>
#include <variant>

#include "starboard/android/shared/audio_output_manager.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/android/shared/safe_jni.h"
#include "starboard/audio_sink.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "third_party/jni_zero/jni_zero.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "cobalt/android/jni_headers/AudioTrackBridge_jni.h"

namespace starboard {

namespace {

using jni_zero::AttachCurrentThread;
using jni_zero::JavaParamRef;
using jni_zero::ScopedJavaGlobalRef;
using jni_zero::ScopedJavaLocalRef;

const jint kNoOffset = 0;

}  // namespace

std::unique_ptr<AudioTrackBridge> AudioTrackBridge::Create(
    SbMediaAudioCodingType coding_type,
    std::optional<SbMediaAudioSampleType> sample_type,
    int channels,
    int sampling_frequency_hz,
    int preferred_buffer_size_in_bytes,
    std::optional<int> tunnel_mode_audio_session_id,
    bool is_web_audio) {
  if (coding_type == kSbMediaAudioCodingTypePcm) {
    SB_DCHECK(SbAudioSinkIsAudioSampleTypeSupported(sample_type.value()));

    // TODO: Support query if platform supports float type for tunnel mode.
    if (tunnel_mode_audio_session_id) {
      SB_DCHECK_EQ(sample_type.value(), kSbMediaAudioSampleTypeInt16Deprecated);
    }
  } else {
    SB_DCHECK(coding_type == kSbMediaAudioCodingTypeAc3 ||
              coding_type == kSbMediaAudioCodingTypeDolbyDigitalPlus);
    // TODO: Support passthrough under tunnel mode.
    SB_DCHECK(!tunnel_mode_audio_session_id);
    // TODO: |sample_type| is not used in passthrough mode, we should make this
    // explicit.
  }

  int max_samples_per_write = [&] {
    if (coding_type != kSbMediaAudioCodingTypePcm) {
      SB_DCHECK(!sample_type);
      return kMaxFramesPerRequest;
    }
    return channels * kMaxFramesPerRequest;
  }();

  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jobject> j_audio_track_bridge =
      AudioOutputManager::GetInstance()->CreateAudioTrackBridge(
          env, GetAudioFormatSampleType(coding_type, sample_type),
          sampling_frequency_hz, channels, max_samples_per_write,
          preferred_buffer_size_in_bytes, tunnel_mode_audio_session_id,
          is_web_audio);

  if (j_audio_track_bridge.is_null()) {
    // One of the cases that this may hit is when output happened to be switched
    // to a device that doesn't support tunnel mode.
    // TODO: Find a way to exclude the device from tunnel mode playback, to
    //       avoid infinite loop in creating the audio sink on a device
    //       claims to support tunnel mode but fails to create the audio sink.
    // TODO: Currently this will be reported as a general decode error,
    //       investigate if this can be reported as a capability changed error.
    SB_LOG(WARNING) << "Failed to create |j_audio_track_bridge|.";
    return nullptr;
  }

  const auto j_audio_data_var = [&]() -> DataArray {
    if (coding_type == kSbMediaAudioCodingTypePcm &&
        sample_type == kSbMediaAudioSampleTypeFloat32) {
      ScopedJavaLocalRef<jfloatArray> j_float_array =
          Java_AudioTrackBridge_getPreAllocatedAudioDataAsFloatArray(
              env, j_audio_track_bridge);
      SB_CHECK(j_float_array);
      return FloatArray(env, j_float_array);
    }
    ScopedJavaLocalRef<jbyteArray> j_byte_array =
        Java_AudioTrackBridge_getPreAllocatedAudioDataAsByteArray(
            env, j_audio_track_bridge);
    SB_CHECK(j_byte_array);
    return ByteArray(env, j_byte_array);
  }();

  return std::make_unique<AudioTrackBridge>(
      PassKey<AudioTrackBridge>(), max_samples_per_write, j_audio_track_bridge,
      j_audio_data_var);
}

AudioTrackBridge::AudioTrackBridge(
    PassKey<AudioTrackBridge>,
    int max_samples_per_write,
    const ScopedJavaLocalRef<jobject>& j_audio_track_bridge,
    const DataArray& j_audio_data)
    : max_samples_per_write_(max_samples_per_write),
      j_audio_track_bridge_(j_audio_track_bridge),
      j_audio_data_(j_audio_data) {}

AudioTrackBridge::~AudioTrackBridge() {
  if (!j_audio_track_bridge_.is_null()) {
    // Both the global and local references refer to the exact same Java object
    // in the JVM.
    JNIEnv* env = AttachCurrentThread();
    ScopedJavaLocalRef<jobject> audio_track_bridge =
        j_audio_track_bridge_.AsLocalRef(env);
    AudioOutputManager::GetInstance()->DestroyAudioTrackBridge(
        env, audio_track_bridge);
  }
}

void AudioTrackBridge::Play() {
  JNIEnv* env = AttachCurrentThread();
  Java_AudioTrackBridge_play(env, j_audio_track_bridge_);
  SB_LOG(INFO) << "AudioTrackBridge playing.";
}

void AudioTrackBridge::Pause() {
  JNIEnv* env = AttachCurrentThread();
  Java_AudioTrackBridge_pause(env, j_audio_track_bridge_);
  if (jni_zero::ClearException(env)) {
    SB_LOG(WARNING) << "Ignored JNI exception during AudioTrackBridge Pause.";
    return;
  }
  SB_LOG(INFO) << "AudioTrackBridge paused.";
}

void AudioTrackBridge::Stop() {
  JNIEnv* env = AttachCurrentThread();
  Java_AudioTrackBridge_stop(env, j_audio_track_bridge_);
  if (jni_zero::ClearException(env)) {
    SB_LOG(WARNING) << "Ignored JNI exception during AudioTrackBridge Stop.";
    return;
  }
  SB_LOG(INFO) << "AudioTrackBridge stopped.";
}

void AudioTrackBridge::PauseAndFlush() {
  JNIEnv* env = AttachCurrentThread();
  // For an immediate stop, use pause(), followed by flush() to discard audio
  // data that hasn't been played back yet.
  Java_AudioTrackBridge_pause(env, j_audio_track_bridge_);
  if (jni_zero::ClearException(env)) {
    SB_LOG(WARNING) << "Ignored JNI exception during AudioTrackBridge pause in PauseAndFlush.";
    return;
  }
  // Flushes the audio data currently queued for playback. Any data that has
  // been written but not yet presented will be discarded.
  Java_AudioTrackBridge_flush(env, j_audio_track_bridge_);
  if (jni_zero::ClearException(env)) {
    SB_LOG(WARNING) << "Ignored JNI exception during AudioTrackBridge flush in PauseAndFlush.";
  }
}

int AudioTrackBridge::WriteSample(Span<const float> samples) {
  JNIEnv* env = AttachCurrentThread();
  int num_of_samples = static_cast<int>(samples.size());
  SB_DCHECK_LE(num_of_samples, max_samples_per_write_);

  num_of_samples = std::min(num_of_samples, max_samples_per_write_);

  SB_CHECK(std::holds_alternative<FloatArray>(j_audio_data_));
  const auto& float_array = std::get<FloatArray>(j_audio_data_);

  SetFloatArrayRegion(env, float_array, kNoOffset, num_of_samples,
                      samples.data());

  return Java_AudioTrackBridge_write(env, j_audio_track_bridge_, float_array,
                                     num_of_samples);
}

int AudioTrackBridge::WriteSample(Span<const uint16_t> samples,
                                  int64_t sync_time) {
  JNIEnv* env = AttachCurrentThread();
  int num_of_samples = static_cast<int>(samples.size());
  SB_DCHECK_LE(num_of_samples, max_samples_per_write_);

  num_of_samples = std::min(num_of_samples, max_samples_per_write_);

  SB_CHECK(std::holds_alternative<ByteArray>(j_audio_data_));
  const auto& byte_array = std::get<ByteArray>(j_audio_data_);

  SetByteArrayRegion(env, byte_array, kNoOffset,
                     num_of_samples * sizeof(uint16_t),
                     reinterpret_cast<const jbyte*>(samples.data()));

  int bytes_written = Java_AudioTrackBridge_writeWithPresentationTime(
      env, j_audio_track_bridge_, byte_array, num_of_samples * sizeof(uint16_t),
      sync_time);
  if (bytes_written < 0) {
    // Error code returned as negative value, like AudioTrack.ERROR_DEAD_OBJECT.
    return bytes_written;
  }
  SB_DCHECK_EQ(bytes_written % sizeof(uint16_t), static_cast<size_t>(0));

  return bytes_written / sizeof(uint16_t);
}

int AudioTrackBridge::WriteSample(Span<const uint8_t> buffer,
                                  int64_t sync_time) {
  JNIEnv* env = AttachCurrentThread();
  int num_of_bytes = static_cast<int>(buffer.size());
  SB_DCHECK_LE(num_of_bytes, max_samples_per_write_);

  num_of_bytes = std::min(num_of_bytes, max_samples_per_write_);

  SB_CHECK(std::holds_alternative<ByteArray>(j_audio_data_));
  const auto& byte_array = std::get<ByteArray>(j_audio_data_);

  SetByteArrayRegion(env, byte_array, kNoOffset, num_of_bytes,
                     reinterpret_cast<const jbyte*>(buffer.data()));

  int bytes_written = Java_AudioTrackBridge_writeWithPresentationTime(
      env, j_audio_track_bridge_, byte_array, num_of_bytes, sync_time);

  if (bytes_written < 0) {
    // Error code returned as negative value, like AudioTrack.ERROR_DEAD_OBJECT.
    return bytes_written;
  }
  return bytes_written;
}

void AudioTrackBridge::SetPlaybackRate(double playback_rate) {
  JNIEnv* env = AttachCurrentThread();
  // AudioTrack doesn't support playback speed of 0.
  SB_DCHECK_GT(playback_rate, 0.0);

  jboolean status = Java_AudioTrackBridge_setPlaybackRate(
      env, j_audio_track_bridge_, static_cast<float>(playback_rate));
  if (!status) {
    SB_LOG(ERROR) << "Failed to set playback rate to " << playback_rate;
  }
}

void AudioTrackBridge::SetVolume(double volume) {
  JNIEnv* env = AttachCurrentThread();
  jint status = Java_AudioTrackBridge_setVolume(env, j_audio_track_bridge_,
                                                static_cast<float>(volume));
  if (status != 0) {
    SB_LOG(ERROR) << "Failed to set volume to " << volume;
  }
}

int64_t AudioTrackBridge::GetAudioTimestamp(int64_t* updated_at) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_audio_timestamp =
      Java_AudioTrackBridge_getAudioTimestamp(env, j_audio_track_bridge_);
  SB_DCHECK(!j_audio_timestamp.is_null());

  if (updated_at) {
    *updated_at =
        Java_AudioTimestamp_getNanoTime(env, j_audio_timestamp) / 1000;
  }
  return Java_AudioTimestamp_getFramePosition(env, j_audio_timestamp);
}

bool AudioTrackBridge::GetAndResetHasAudioDeviceChanged() {
  JNIEnv* env = AttachCurrentThread();
  return AudioOutputManager::GetInstance()->GetAndResetHasAudioDeviceChanged(
      env);
}

int AudioTrackBridge::GetUnderrunCount() {
  JNIEnv* env = AttachCurrentThread();
  return Java_AudioTrackBridge_getUnderrunCount(env, j_audio_track_bridge_);
}

int AudioTrackBridge::GetStartThresholdInFrames() {
  JNIEnv* env = AttachCurrentThread();
  return Java_AudioTrackBridge_getStartThresholdInFrames(env,
                                                         j_audio_track_bridge_);
}

AudioTrack::PlayState AudioTrackBridge::GetPlayState() {
  JNIEnv* env = AttachCurrentThread();
  return static_cast<PlayState>(
      Java_AudioTrackBridge_getPlayState(env, j_audio_track_bridge_));
}

}  // namespace starboard
