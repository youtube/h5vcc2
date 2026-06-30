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

#include "starboard/android/shared/aaudio_loader.h"

#include <dlfcn.h>

#include <memory>

namespace starboard {

AAudioLoader* AAudioLoader::GetInstance() {
  static AAudioLoader* instance = []() -> AAudioLoader* {
    void* lib_handle = dlopen("libaaudio.so", RTLD_NOW);
    if (!lib_handle) {
      SB_LOG(INFO) << "AAudio is not supported on this device (failed to load "
                      "libaaudio.so).";
      return nullptr;
    }
    auto loader =
        std::make_unique<AAudioLoader>(PassKey<AAudioLoader>(), lib_handle);
    if (!loader->lib_handle_) {
      return nullptr;
    }
    return loader.release();
  }();
  return instance;
}

AAudioLoader::AAudioLoader(PassKey<AAudioLoader>, void* lib_handle)
    : lib_handle_(lib_handle) {
  // Helper macro to resolve and check symbol
#define RESOLVE_SYMBOL(name)                                         \
  pfn_##name = (PFN_##name)dlsym(lib_handle_, #name);                \
  if (!pfn_##name) {                                                 \
    SB_LOG(WARNING) << "Failed to resolve AAudio symbol: " << #name; \
    dlclose(lib_handle_);                                            \
    lib_handle_ = nullptr;                                           \
    return;                                                          \
  }

  RESOLVE_SYMBOL(AAudio_convertResultToText);
  RESOLVE_SYMBOL(AAudio_createStreamBuilder);
  RESOLVE_SYMBOL(AAudioStreamBuilder_delete);
  RESOLVE_SYMBOL(AAudioStreamBuilder_setDirection);
  RESOLVE_SYMBOL(AAudioStreamBuilder_setSharingMode);
  RESOLVE_SYMBOL(AAudioStreamBuilder_setPerformanceMode);
  RESOLVE_SYMBOL(AAudioStreamBuilder_setFormat);
  RESOLVE_SYMBOL(AAudioStreamBuilder_setChannelCount);
  RESOLVE_SYMBOL(AAudioStreamBuilder_setSampleRate);
  RESOLVE_SYMBOL(AAudioStreamBuilder_setDataCallback);
  RESOLVE_SYMBOL(AAudioStreamBuilder_setBufferCapacityInFrames);
  RESOLVE_SYMBOL(AAudioStreamBuilder_openStream);
  RESOLVE_SYMBOL(AAudioStream_close);
  RESOLVE_SYMBOL(AAudioStream_requestStart);
  RESOLVE_SYMBOL(AAudioStream_requestPause);
  RESOLVE_SYMBOL(AAudioStream_requestStop);
  RESOLVE_SYMBOL(AAudioStream_requestFlush);
  RESOLVE_SYMBOL(AAudioStream_getState);
  RESOLVE_SYMBOL(AAudioStream_getTimestamp);
  RESOLVE_SYMBOL(AAudioStream_write);
  RESOLVE_SYMBOL(AAudioStream_getBufferSizeInFrames);
  RESOLVE_SYMBOL(AAudioStream_getFramesPerBurst);
  RESOLVE_SYMBOL(AAudioStream_getFramesRead);
  RESOLVE_SYMBOL(AAudioStream_getXRunCount);
#undef RESOLVE_SYMBOL

  SB_LOG(INFO) << "Successfully initialized AAudio NDK API.";
}

AAudioLoader::~AAudioLoader() {
  if (lib_handle_) {
    dlclose(lib_handle_);
  }
}

// Wrapper implementations
const char* AAudioLoader::convertResultToText(aaudio_result_t returnCode) {
  return pfn_AAudio_convertResultToText(returnCode);
}

aaudio_result_t AAudioLoader::createStreamBuilder(
    AAudioStreamBuilder** builder) {
  return pfn_AAudio_createStreamBuilder(builder);
}

aaudio_result_t AAudioLoader::streamBuilder_delete(
    AAudioStreamBuilder* builder) {
  return pfn_AAudioStreamBuilder_delete(builder);
}

void AAudioLoader::streamBuilder_setDirection(AAudioStreamBuilder* builder,
                                              aaudio_direction_t direction) {
  pfn_AAudioStreamBuilder_setDirection(builder, direction);
}

void AAudioLoader::streamBuilder_setSharingMode(
    AAudioStreamBuilder* builder,
    aaudio_sharing_mode_t sharingMode) {
  pfn_AAudioStreamBuilder_setSharingMode(builder, sharingMode);
}

void AAudioLoader::streamBuilder_setPerformanceMode(
    AAudioStreamBuilder* builder,
    aaudio_performance_mode_t performanceMode) {
  pfn_AAudioStreamBuilder_setPerformanceMode(builder, performanceMode);
}

void AAudioLoader::streamBuilder_setFormat(AAudioStreamBuilder* builder,
                                           aaudio_format_t format) {
  pfn_AAudioStreamBuilder_setFormat(builder, format);
}

void AAudioLoader::streamBuilder_setChannelCount(AAudioStreamBuilder* builder,
                                                 int32_t channelCount) {
  pfn_AAudioStreamBuilder_setChannelCount(builder, channelCount);
}

void AAudioLoader::streamBuilder_setSampleRate(AAudioStreamBuilder* builder,
                                               int32_t sampleRate) {
  pfn_AAudioStreamBuilder_setSampleRate(builder, sampleRate);
}

void AAudioLoader::streamBuilder_setDataCallback(
    AAudioStreamBuilder* builder,
    AAudioStream_dataCallback callback,
    void* userData) {
  pfn_AAudioStreamBuilder_setDataCallback(builder, callback, userData);
}

void AAudioLoader::streamBuilder_setBufferCapacityInFrames(
    AAudioStreamBuilder* builder,
    int32_t numFrames) {
  pfn_AAudioStreamBuilder_setBufferCapacityInFrames(builder, numFrames);
}

aaudio_result_t AAudioLoader::streamBuilder_openStream(
    AAudioStreamBuilder* builder,
    AAudioStream** stream) {
  return pfn_AAudioStreamBuilder_openStream(builder, stream);
}

aaudio_result_t AAudioLoader::stream_close(AAudioStream* stream) {
  return pfn_AAudioStream_close(stream);
}

aaudio_result_t AAudioLoader::stream_requestStart(AAudioStream* stream) {
  return pfn_AAudioStream_requestStart(stream);
}

aaudio_result_t AAudioLoader::stream_requestPause(AAudioStream* stream) {
  return pfn_AAudioStream_requestPause(stream);
}

aaudio_result_t AAudioLoader::stream_requestStop(AAudioStream* stream) {
  return pfn_AAudioStream_requestStop(stream);
}

aaudio_result_t AAudioLoader::stream_requestFlush(AAudioStream* stream) {
  return pfn_AAudioStream_requestFlush(stream);
}

aaudio_stream_state_t AAudioLoader::stream_getState(AAudioStream* stream) {
  return pfn_AAudioStream_getState(stream);
}

aaudio_result_t AAudioLoader::stream_getTimestamp(AAudioStream* stream,
                                                  clockid_t clockId,
                                                  int64_t* framePosition,
                                                  int64_t* timeNanoseconds) {
  return pfn_AAudioStream_getTimestamp(stream, clockId, framePosition,
                                       timeNanoseconds);
}

aaudio_result_t AAudioLoader::stream_write(AAudioStream* stream,
                                           const void* buffer,
                                           int32_t numFrames,
                                           int64_t timeoutNanoseconds) {
  return pfn_AAudioStream_write(stream, buffer, numFrames, timeoutNanoseconds);
}

int32_t AAudioLoader::stream_getBufferSizeInFrames(AAudioStream* stream) {
  return pfn_AAudioStream_getBufferSizeInFrames(stream);
}

int32_t AAudioLoader::stream_getFramesPerBurst(AAudioStream* stream) {
  return pfn_AAudioStream_getFramesPerBurst(stream);
}

int64_t AAudioLoader::stream_getFramesRead(AAudioStream* stream) {
  return pfn_AAudioStream_getFramesRead(stream);
}

int32_t AAudioLoader::stream_getXRunCount(AAudioStream* stream) {
  return pfn_AAudioStream_getXRunCount(stream);
}

}  // namespace starboard
