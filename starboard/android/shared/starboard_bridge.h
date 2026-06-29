// Copyright 2024 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_ANDROID_SHARED_STARBOARD_BRIDGE_H_
#define STARBOARD_ANDROID_SHARED_STARBOARD_BRIDGE_H_

#include <jni.h>

#include "base/memory/singleton.h"
#include "starboard/common/size.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {

// This class serves as a bridge between the native code and Android
// StarboardBridge Java class.
class StarboardBridge {
 public:
  // Returns the singleton.
  static StarboardBridge* GetInstance();

  void Initialize(JNIEnv* env, jobject obj);

  int64_t GetAppStartTimestamp(JNIEnv* env);

  void ApplicationStarted(JNIEnv* env);

  void ApplicationStopping(JNIEnv* env);

  void AppendArgs(JNIEnv* env, std::vector<std::string>* args_vector);

  jni_zero::ScopedJavaLocalRef<jintArray> GetSupportedHdrTypes(JNIEnv* env);

  void RaisePlatformError(JNIEnv* env,
                          jint errorType,
                          jlong data,
                          const std::string& url);

  bool IsPlatformErrorShowing(JNIEnv* env);

  void RequestSuspend(JNIEnv* env);

  jni_zero::ScopedJavaLocalRef<jobject> GetTextToSpeechHelper(JNIEnv* env);

  jni_zero::ScopedJavaLocalRef<jobject> GetCaptionSettings(JNIEnv* env);

  jni_zero::ScopedJavaLocalRef<jobject> GetResourceOverlay(JNIEnv* env);

  jni_zero::ScopedJavaLocalRef<jstring> GetSystemLocaleId(JNIEnv* env);

  std::string GetAdvertisingId(JNIEnv* env);
  bool GetLimitAdTracking(JNIEnv* env);
  std::string GetFriendlyName(JNIEnv* env);
  double GetScreenDiagonal(JNIEnv* env);

  void CloseApp(JNIEnv* env);

  std::string GetTimeZoneId(JNIEnv* env);

  jni_zero::ScopedJavaLocalRef<jobject> GetDisplayDpi(JNIEnv* env);

  Size GetDeviceResolution(JNIEnv* env);

  bool IsNetworkConnected(JNIEnv* env);

  void ReportFullyDrawn(JNIEnv* env);

  void SetCrashContext(JNIEnv* env, const char* key, const char* value);

  bool IsMicrophoneDisconnected(JNIEnv* env);
  bool IsMicrophoneMute(JNIEnv* env);
  jni_zero::ScopedJavaLocalRef<jobject> GetAudioPermissionRequester(
      JNIEnv* env);

  void ResetVideoSurface(JNIEnv* env);
  void SetVideoSurfaceBounds(JNIEnv* env, int x, int y, int width, int height);

  jni_zero::ScopedJavaLocalRef<jobject> GetAudioOutputManager(JNIEnv* env);

  jni_zero::ScopedJavaLocalRef<jobject> GetExoPlayerManager(JNIEnv* env);

  std::string GetUserAgentAuxField(JNIEnv* env) const;

  bool IsAmatiDevice(JNIEnv* env) const;

  std::string GetBuildFingerprint(JNIEnv* env) const;

  int64_t GetPlayServicesVersion(JNIEnv* env) const;

  jni_zero::ScopedJavaLocalRef<jobject> OpenCobaltService(
      JNIEnv* env,
      jlong native_service,
      const char* service_name);
  void CloseCobaltService(JNIEnv* env, const char* service_name);
  bool HasCobaltService(JNIEnv* env, const char* service_name);
  void CloseAllCobaltService(JNIEnv* env) const;

  void HideSplashScreen(JNIEnv* env) const;

  void SetStartupMilestone(jint milestone) const;
  void SetStartupDiagnosisInfo(const char* key, const char* value) const;

 private:
  StarboardBridge() = default;
  ~StarboardBridge() = default;

  // Prevent copy construction and assignment
  StarboardBridge(const StarboardBridge&) = delete;
  StarboardBridge& operator=(const StarboardBridge&) = delete;

  friend struct base::DefaultSingletonTraits<StarboardBridge>;

  // Java StarboardBridge instance.
  jni_zero::ScopedJavaGlobalRef<jobject> j_starboard_bridge_;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_STARBOARD_BRIDGE_H_
