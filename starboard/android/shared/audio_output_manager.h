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

#ifndef STARBOARD_ANDROID_SHARED_AUDIO_OUTPUT_MANAGER_H_
#define STARBOARD_ANDROID_SHARED_AUDIO_OUTPUT_MANAGER_H_

#include <jni.h>

#include <optional>

#include "base/memory/singleton.h"
#include "starboard/media.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {

class AudioOutputManager {
 public:
  static void SetSeamlessAudioSwitching(bool enable);

  // Returns the singleton.
  static AudioOutputManager* GetInstance();

  jni_zero::ScopedJavaLocalRef<jobject> CreateAudioTrackBridge(
      JNIEnv* env,
      int sample_type,
      int sample_rate,
      int channel_count,
      int max_samples_per_write,
      int preferred_buffer_size_in_bytes,
      std::optional<int> tunnel_mode_audio_session_id,
      jboolean is_web_audio);

  void DestroyAudioTrackBridge(JNIEnv* env,
                               jni_zero::ScopedJavaLocalRef<jobject>& obj);

  bool GetOutputDeviceInfo(JNIEnv* env,
                           jint index,
                           jni_zero::ScopedJavaLocalRef<jobject>& obj);

  int GetMinBufferSize(JNIEnv* env,
                       jint sample_type,
                       jint sample_rate,
                       jint channel_count);

  int GetMinBufferSizeInFrames(JNIEnv* env,
                               SbMediaAudioSampleType sample_type,
                               int channels,
                               int sampling_frequency_hz);

  std::optional<int> GenerateTunnelModeAudioSessionId(JNIEnv* env,
                                                      int numberOfChannels);

  bool HasPassthroughSupportFor(JNIEnv* env, int encoding);

  bool GetAudioConfiguration(JNIEnv* env,
                             int index,
                             SbMediaAudioConfiguration* configuration);

 private:
  // The constructor runs exactly once because of
  // base::DefaultSingletonTraits<AudioOutputManager>.
  AudioOutputManager();
  ~AudioOutputManager() = default;

  // Prevent copy construction and assignment
  AudioOutputManager(const AudioOutputManager&) = delete;
  AudioOutputManager& operator=(const AudioOutputManager&) = delete;

  friend struct base::DefaultSingletonTraits<AudioOutputManager>;

  // Java AudioOutputManager instance.
  jni_zero::ScopedJavaGlobalRef<jobject> j_audio_output_manager_;
};

}  // namespace starboard

#endif  //  STARBOARD_ANDROID_SHARED_AUDIO_OUTPUT_MANAGER_H_
