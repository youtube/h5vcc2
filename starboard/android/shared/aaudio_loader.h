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

#ifndef STARBOARD_ANDROID_SHARED_AAUDIO_LOADER_H_
#define STARBOARD_ANDROID_SHARED_AAUDIO_LOADER_H_

#include <aaudio/AAudio.h>

#include <memory>

#include "starboard/common/log.h"
#include "starboard/common/pass_key.h"

namespace starboard {

// AAudioLoader is a singleton responsible for dynamically loading libaaudio.so
// and resolving its symbols at runtime. This allows Cobalt to support AAudio on
// devices where it is available while maintaining compatibility with older
// Android versions.
//
// Lifetime and Ownership:
// This is a singleton class with a static lifetime. The single instance is
// managed internally and is never deleted.
//
// Threading Model:
// This class is thread-safe and its methods can be called from any thread.
class AAudioLoader {
 public:
  static AAudioLoader* GetInstance();

  AAudioLoader(PassKey<AAudioLoader>, void* lib_handle);
  ~AAudioLoader();

  // Function wrappers
  const char* convertResultToText(aaudio_result_t returnCode);
  aaudio_result_t createStreamBuilder(AAudioStreamBuilder** builder);
  aaudio_result_t streamBuilder_delete(AAudioStreamBuilder* builder);
  void streamBuilder_setDirection(AAudioStreamBuilder* builder,
                                  aaudio_direction_t direction);
  void streamBuilder_setSharingMode(AAudioStreamBuilder* builder,
                                    aaudio_sharing_mode_t sharingMode);
  void streamBuilder_setPerformanceMode(
      AAudioStreamBuilder* builder,
      aaudio_performance_mode_t performanceMode);
  void streamBuilder_setFormat(AAudioStreamBuilder* builder,
                               aaudio_format_t format);
  void streamBuilder_setChannelCount(AAudioStreamBuilder* builder,
                                     int32_t channelCount);
  void streamBuilder_setSampleRate(AAudioStreamBuilder* builder,
                                   int32_t sampleRate);
  void streamBuilder_setDataCallback(AAudioStreamBuilder* builder,
                                     AAudioStream_dataCallback callback,
                                     void* userData);
  void streamBuilder_setBufferCapacityInFrames(AAudioStreamBuilder* builder,
                                               int32_t numFrames);
  aaudio_result_t streamBuilder_openStream(AAudioStreamBuilder* builder,
                                           AAudioStream** stream);
  aaudio_result_t stream_close(AAudioStream* stream);
  aaudio_result_t stream_requestStart(AAudioStream* stream);
  aaudio_result_t stream_requestPause(AAudioStream* stream);
  aaudio_result_t stream_requestStop(AAudioStream* stream);
  aaudio_result_t stream_requestFlush(AAudioStream* stream);
  aaudio_stream_state_t stream_getState(AAudioStream* stream);
  aaudio_result_t stream_getTimestamp(AAudioStream* stream,
                                      clockid_t clockId,
                                      int64_t* framePosition,
                                      int64_t* timeNanoseconds);
  aaudio_result_t stream_write(AAudioStream* stream,
                               const void* buffer,
                               int32_t numFrames,
                               int64_t timeoutNanoseconds);
  int32_t stream_getBufferSizeInFrames(AAudioStream* stream);
  int32_t stream_getFramesPerBurst(AAudioStream* stream);
  int64_t stream_getFramesRead(AAudioStream* stream);
  int32_t stream_getXRunCount(AAudioStream* stream);

 private:
  void* lib_handle_ = nullptr;

  // Function pointer types
  using PFN_AAudio_convertResultToText =
      const char* (*)(aaudio_result_t returnCode);
  using PFN_AAudio_createStreamBuilder =
      aaudio_result_t (*)(AAudioStreamBuilder** builder);
  using PFN_AAudioStreamBuilder_delete =
      aaudio_result_t (*)(AAudioStreamBuilder* builder);
  using PFN_AAudioStreamBuilder_setDirection =
      void (*)(AAudioStreamBuilder* builder, aaudio_direction_t direction);
  using PFN_AAudioStreamBuilder_setSharingMode =
      void (*)(AAudioStreamBuilder* builder, aaudio_sharing_mode_t sharingMode);
  using PFN_AAudioStreamBuilder_setPerformanceMode =
      void (*)(AAudioStreamBuilder* builder,
               aaudio_performance_mode_t performanceMode);
  using PFN_AAudioStreamBuilder_setFormat =
      void (*)(AAudioStreamBuilder* builder, aaudio_format_t format);
  using PFN_AAudioStreamBuilder_setChannelCount =
      void (*)(AAudioStreamBuilder* builder, int32_t channelCount);
  using PFN_AAudioStreamBuilder_setSampleRate =
      void (*)(AAudioStreamBuilder* builder, int32_t sampleRate);
  using PFN_AAudioStreamBuilder_setDataCallback =
      void (*)(AAudioStreamBuilder* builder,
               AAudioStream_dataCallback callback,
               void* userData);
  using PFN_AAudioStreamBuilder_setBufferCapacityInFrames =
      void (*)(AAudioStreamBuilder* builder, int32_t numFrames);
  using PFN_AAudioStreamBuilder_openStream =
      aaudio_result_t (*)(AAudioStreamBuilder* builder, AAudioStream** stream);
  using PFN_AAudioStream_close = aaudio_result_t (*)(AAudioStream* stream);
  using PFN_AAudioStream_requestStart =
      aaudio_result_t (*)(AAudioStream* stream);
  using PFN_AAudioStream_requestPause =
      aaudio_result_t (*)(AAudioStream* stream);
  using PFN_AAudioStream_requestStop =
      aaudio_result_t (*)(AAudioStream* stream);
  using PFN_AAudioStream_requestFlush =
      aaudio_result_t (*)(AAudioStream* stream);
  using PFN_AAudioStream_getState =
      aaudio_stream_state_t (*)(AAudioStream* stream);
  using PFN_AAudioStream_getTimestamp =
      aaudio_result_t (*)(AAudioStream* stream,
                          clockid_t clockId,
                          int64_t* framePosition,
                          int64_t* timeNanoseconds);
  using PFN_AAudioStream_write =
      aaudio_result_t (*)(AAudioStream* stream,
                          const void* buffer,
                          int32_t numFrames,
                          int64_t timeoutNanoseconds);
  using PFN_AAudioStream_getBufferSizeInFrames =
      int32_t (*)(AAudioStream* stream);
  using PFN_AAudioStream_getFramesPerBurst = int32_t (*)(AAudioStream* stream);
  using PFN_AAudioStream_getFramesRead = int64_t (*)(AAudioStream* stream);
  using PFN_AAudioStream_getXRunCount = int32_t (*)(AAudioStream* stream);

  // Function pointers
  PFN_AAudio_convertResultToText pfn_AAudio_convertResultToText = nullptr;
  PFN_AAudio_createStreamBuilder pfn_AAudio_createStreamBuilder = nullptr;
  PFN_AAudioStreamBuilder_delete pfn_AAudioStreamBuilder_delete = nullptr;
  PFN_AAudioStreamBuilder_setDirection pfn_AAudioStreamBuilder_setDirection =
      nullptr;
  PFN_AAudioStreamBuilder_setSharingMode
      pfn_AAudioStreamBuilder_setSharingMode = nullptr;
  PFN_AAudioStreamBuilder_setPerformanceMode
      pfn_AAudioStreamBuilder_setPerformanceMode = nullptr;
  PFN_AAudioStreamBuilder_setFormat pfn_AAudioStreamBuilder_setFormat = nullptr;
  PFN_AAudioStreamBuilder_setChannelCount
      pfn_AAudioStreamBuilder_setChannelCount = nullptr;
  PFN_AAudioStreamBuilder_setSampleRate pfn_AAudioStreamBuilder_setSampleRate =
      nullptr;
  PFN_AAudioStreamBuilder_setDataCallback
      pfn_AAudioStreamBuilder_setDataCallback = nullptr;
  PFN_AAudioStreamBuilder_setBufferCapacityInFrames
      pfn_AAudioStreamBuilder_setBufferCapacityInFrames = nullptr;
  PFN_AAudioStreamBuilder_openStream pfn_AAudioStreamBuilder_openStream =
      nullptr;
  PFN_AAudioStream_close pfn_AAudioStream_close = nullptr;
  PFN_AAudioStream_requestStart pfn_AAudioStream_requestStart = nullptr;
  PFN_AAudioStream_requestPause pfn_AAudioStream_requestPause = nullptr;
  PFN_AAudioStream_requestStop pfn_AAudioStream_requestStop = nullptr;
  PFN_AAudioStream_requestFlush pfn_AAudioStream_requestFlush = nullptr;
  PFN_AAudioStream_getState pfn_AAudioStream_getState = nullptr;
  PFN_AAudioStream_getTimestamp pfn_AAudioStream_getTimestamp = nullptr;
  PFN_AAudioStream_write pfn_AAudioStream_write = nullptr;
  PFN_AAudioStream_getBufferSizeInFrames
      pfn_AAudioStream_getBufferSizeInFrames = nullptr;
  PFN_AAudioStream_getFramesPerBurst pfn_AAudioStream_getFramesPerBurst =
      nullptr;
  PFN_AAudioStream_getFramesRead pfn_AAudioStream_getFramesRead = nullptr;
  PFN_AAudioStream_getXRunCount pfn_AAudioStream_getXRunCount = nullptr;
};

}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_AAUDIO_LOADER_H_
