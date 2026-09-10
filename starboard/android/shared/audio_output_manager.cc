// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/android/shared/audio_output_manager.h"

#include <atomic>

#include "starboard/android/shared/media_capabilities_cache.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/android/shared/starboard_bridge.h"
#include "starboard/common/log.h"
#include "starboard/common/string.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/media/mime_supportability_cache.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "cobalt/android/jni_headers/AudioOutputManager_jni.h"

namespace starboard {

namespace {

using jni_zero::JavaParamRef;
using jni_zero::ScopedJavaLocalRef;

// Constants for output types from
// https://developer.android.com/reference/android/media/AudioDeviceInfo.
constexpr int TYPE_AUX_LINE = 19;
constexpr int TYPE_BLE_BROADCAST = 30;
constexpr int TYPE_BLE_HEADSET = 26;
constexpr int TYPE_BLE_SPEAKER = 27;
constexpr int TYPE_BLUETOOTH_A2DP = 8;
constexpr int TYPE_BLUETOOTH_SCO = 7;
constexpr int TYPE_BUILTIN_EARPIECE = 1;
constexpr int TYPE_BUILTIN_MIC = 15;
constexpr int TYPE_BUILTIN_SPEAKER = 2;
constexpr int TYPE_BUILTIN_SPEAKER_SAFE = 24;
constexpr int TYPE_BUS = 21;
constexpr int TYPE_DOCK = 13;
constexpr int TYPE_DOCK_ANALOG = 31;
constexpr int TYPE_FM = 14;
constexpr int TYPE_FM_TUNER = 16;
constexpr int TYPE_HDMI = 9;
constexpr int TYPE_HDMI_ARC = 10;
constexpr int TYPE_HDMI_EARC = 29;
constexpr int TYPE_HEARING_AID = 23;
constexpr int TYPE_IP = 20;
constexpr int TYPE_LINE_ANALOG = 5;
constexpr int TYPE_LINE_DIGITAL = 6;
constexpr int TYPE_REMOTE_SUBMIX = 25;
constexpr int TYPE_TELEPHONY = 18;
constexpr int TYPE_TV_TUNER = 17;
constexpr int TYPE_UNKNOWN = 0;
constexpr int TYPE_USB_ACCESSORY = 12;
constexpr int TYPE_USB_DEVICE = 11;
constexpr int TYPE_USB_HEADSET = 22;
constexpr int TYPE_WIRED_HEADPHONES = 4;
constexpr int TYPE_WIRED_HEADSET = 3;

std::atomic<bool> g_enable_seamless_audio_switching = false;

SbMediaAudioConnector GetConnectorFromAndroidOutputType(
    int android_output_device_type) {
  switch (android_output_device_type) {
    case TYPE_AUX_LINE:
      return kSbMediaAudioConnectorAnalog;
    case TYPE_BLE_BROADCAST:
      return kSbMediaAudioConnectorBluetooth;
    case TYPE_BLE_HEADSET:
      return kSbMediaAudioConnectorBluetooth;
    case TYPE_BLE_SPEAKER:
      return kSbMediaAudioConnectorBluetooth;
    case TYPE_BLUETOOTH_A2DP:
      return kSbMediaAudioConnectorBluetooth;
    case TYPE_BLUETOOTH_SCO:
      return kSbMediaAudioConnectorBluetooth;
    case TYPE_BUILTIN_EARPIECE:
      return kSbMediaAudioConnectorBuiltIn;
    case TYPE_BUILTIN_MIC:
      return kSbMediaAudioConnectorBuiltIn;
    case TYPE_BUILTIN_SPEAKER:
      return kSbMediaAudioConnectorBuiltIn;
    case TYPE_BUILTIN_SPEAKER_SAFE:
      return kSbMediaAudioConnectorBuiltIn;
    case TYPE_BUS:
      return kSbMediaAudioConnectorUnknown;
    case TYPE_DOCK:
      return kSbMediaAudioConnectorUnknown;
    case TYPE_DOCK_ANALOG:
      return kSbMediaAudioConnectorAnalog;
    case TYPE_FM:
      return kSbMediaAudioConnectorUnknown;
    case TYPE_FM_TUNER:
      return kSbMediaAudioConnectorUnknown;
    case TYPE_HDMI:
      return kSbMediaAudioConnectorHdmi;
    case TYPE_HDMI_ARC:
      return kSbMediaAudioConnectorHdmi;
    case TYPE_HDMI_EARC:
      return kSbMediaAudioConnectorHdmi;
    case TYPE_HEARING_AID:
      return kSbMediaAudioConnectorUnknown;
    case TYPE_IP:
      return kSbMediaAudioConnectorRemoteWired;
    case TYPE_LINE_ANALOG:
      return kSbMediaAudioConnectorAnalog;
    case TYPE_LINE_DIGITAL:
      return kSbMediaAudioConnectorUnknown;
    case TYPE_REMOTE_SUBMIX:
      return kSbMediaAudioConnectorRemoteOther;
    case TYPE_TELEPHONY:
      return kSbMediaAudioConnectorUnknown;
    case TYPE_TV_TUNER:
      return kSbMediaAudioConnectorUnknown;
    case TYPE_UNKNOWN:
      return kSbMediaAudioConnectorUnknown;
    case TYPE_USB_ACCESSORY:
      return kSbMediaAudioConnectorUsb;
    case TYPE_USB_DEVICE:
      return kSbMediaAudioConnectorUsb;
    case TYPE_USB_HEADSET:
      return kSbMediaAudioConnectorUsb;
    case TYPE_WIRED_HEADPHONES:
      return kSbMediaAudioConnectorAnalog;
    case TYPE_WIRED_HEADSET:
      return kSbMediaAudioConnectorAnalog;
    default:
      SB_LOG(WARNING) << "Encountered unknown audio output device type "
                      << android_output_device_type;
      return kSbMediaAudioConnectorUnknown;
  }
}
}  // namespace

SB_EXPORT_ANDROID AudioOutputManager* AudioOutputManager::GetInstance() {
  return base::Singleton<AudioOutputManager>::get();
}

ScopedJavaLocalRef<jobject> AudioOutputManager::CreateAudioTrackBridge(
    JNIEnv* env,
    int sample_type,
    int sample_rate,
    int channel_count,
    int max_samples_per_write,
    int preferred_buffer_size_in_bytes,
    std::optional<int> tunnel_mode_audio_session_id,
    jboolean is_web_audio) {
  SB_DCHECK(env);
  return Java_AudioOutputManager_createAudioTrackBridge(
      env, j_audio_output_manager_, sample_type, sample_rate, channel_count,
      max_samples_per_write, preferred_buffer_size_in_bytes,
      tunnel_mode_audio_session_id.value_or(TUNNEL_MODE_AUDIO_SESSION_ID_NONE),
      is_web_audio);
}

void AudioOutputManager::DestroyAudioTrackBridge(
    JNIEnv* env,
    ScopedJavaLocalRef<jobject>& obj) {
  SB_DCHECK(env);
  return Java_AudioOutputManager_destroyAudioTrackBridge(
      env, j_audio_output_manager_, obj);
}

bool AudioOutputManager::GetOutputDeviceInfo(JNIEnv* env,
                                             jint index,
                                             ScopedJavaLocalRef<jobject>& obj) {
  SB_DCHECK(env);
  jboolean output_device_info_java =
      Java_AudioOutputManager_getOutputDeviceInfo(env, j_audio_output_manager_,
                                                  index, obj);

  return output_device_info_java == JNI_TRUE;
}

int AudioOutputManager::GetMinBufferSize(JNIEnv* env,
                                         jint sample_type,
                                         jint sample_rate,
                                         jint channel_count) {
  SB_DCHECK(env);
  return Java_AudioOutputManager_getMinBufferSize(
      env, j_audio_output_manager_, sample_type, sample_rate, channel_count);
}

int AudioOutputManager::GetMinBufferSizeInFrames(
    JNIEnv* env,
    SbMediaAudioSampleType sample_type,
    int channels,
    int sampling_frequency_hz) {
  int audio_track_min_buffer_size = GetMinBufferSize(
      env, GetAudioFormatSampleType(kSbMediaAudioCodingTypePcm, sample_type),
      sampling_frequency_hz, channels);
  return audio_track_min_buffer_size / channels /
         GetBytesPerSample(sample_type);
}

void AudioOutputManager::SetSeamlessAudioSwitching(bool enable) {
  g_enable_seamless_audio_switching.store(enable, std::memory_order_relaxed);
}

std::optional<int> AudioOutputManager::GenerateTunnelModeAudioSessionId(
    JNIEnv* env,
    int numberOfChannels) {
  SB_DCHECK(env);
  int session_id = Java_AudioOutputManager_generateTunnelModeAudioSessionId(
      env, j_audio_output_manager_, numberOfChannels);
  if (session_id == TUNNEL_MODE_AUDIO_SESSION_ID_NONE) {
    return std::nullopt;
  }
  return session_id;
}

bool AudioOutputManager::HasPassthroughSupportFor(JNIEnv* env, int encoding) {
  SB_DCHECK(env);
  return Java_AudioOutputManager_hasPassthroughSupportFor(
             env, j_audio_output_manager_, encoding) == JNI_TRUE;
}

bool AudioOutputManager::GetAudioConfiguration(
    JNIEnv* env,
    int index,
    SbMediaAudioConfiguration* configuration) {
  *configuration = {};

  ScopedJavaLocalRef<jobject> j_output_device_info =
      Java_OutputDeviceInfo_Constructor(env);

  if (Java_AudioOutputManager_getOutputDeviceInfo(
          env, j_audio_output_manager_, index, j_output_device_info) !=
      JNI_TRUE) {
    SB_LOG(WARNING)
        << "Call to AudioOutputManager.getOutputDeviceInfo() failed.";
    return false;
  }

  configuration->connector = GetConnectorFromAndroidOutputType(
      Java_OutputDeviceInfo_getType(env, j_output_device_info));
  configuration->latency = 0;
  configuration->coding_type = kSbMediaAudioCodingTypePcm;
  configuration->number_of_channels =
      Java_OutputDeviceInfo_getChannels(env, j_output_device_info);

  if (configuration->connector != kSbMediaAudioConnectorHdmi) {
    configuration->number_of_channels = 2;
  }

  return true;
}

AudioOutputManager::AudioOutputManager() {
  JNIEnv* env = jni_zero::AttachCurrentThread();
  SB_DCHECK(env);
  j_audio_output_manager_ =
      StarboardBridge::GetInstance()->GetAudioOutputManager(env);
}

void JNI_AudioOutputManager_OnAudioDeviceChanged(JNIEnv* env) {
  // Audio output device change could change passthrough decoder capabilities,
  // so we have to reload codec capabilities.
  MediaCapabilitiesCache::GetInstance()->ClearCache();
  MimeSupportabilityCache::GetInstance()->ClearCachedMimeSupportabilities();
}

jboolean JNI_AudioOutputManager_IsSeamlessAudioSwitchingEnabled(JNIEnv* env) {
  return g_enable_seamless_audio_switching.load(std::memory_order_relaxed);
}

}  // namespace starboard
