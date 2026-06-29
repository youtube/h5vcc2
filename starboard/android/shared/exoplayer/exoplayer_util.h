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

#ifndef STARBOARD_ANDROID_SHARED_EXOPLAYER_EXOPLAYER_UTIL_H_
#define STARBOARD_ANDROID_SHARED_EXOPLAYER_EXOPLAYER_UTIL_H_

#include <jni.h>

#include "base/android/jni_android.h"
#include "starboard/media.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {

bool ShouldEnableTunneledPlayback(const SbMediaVideoStreamInfo& stream_info);

jni_zero::ScopedJavaLocalRef<jobject> CreateAudioFormat(
    const SbMediaAudioStreamInfo& stream_info);

jni_zero::ScopedJavaLocalRef<jobject> CreateVideoFormat(
    const SbMediaVideoStreamInfo& stream_info);
}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_EXOPLAYER_EXOPLAYER_UTIL_H_
