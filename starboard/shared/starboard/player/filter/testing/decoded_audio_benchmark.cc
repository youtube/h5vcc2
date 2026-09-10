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

#include "starboard/shared/starboard/player/decoded_audio_internal.h"
#include "third_party/google_benchmark/src/include/benchmark/benchmark.h"

namespace starboard {
namespace {

constexpr int kChannels = 2;
constexpr int64_t kTimestampUsec = 1'000'000;
constexpr int kNumFrames = 2048;  // Realistic audio frame size
constexpr int kSizeInBytes = kNumFrames * kChannels * sizeof(int16_t);

DecodedAudio CreateFloatInterleavedBuffer() {
  int float_size = kNumFrames * kChannels * sizeof(float);
  DecodedAudio buffer(kChannels, kSbMediaAudioSampleTypeFloat32, kTimestampUsec,
                      float_size);
  float* data = buffer.data_as_float32();
  for (int i = 0; i < kNumFrames * kChannels; ++i) {
    // Generates a linear ramp of float values in [-1.0f, 1.0f] spanning the
    // full audio range.
    data[i] = -1.0f + 2.0f * (static_cast<float>(i) / (kNumFrames * kChannels));
  }
  return buffer;
}

DecodedAudio CreateInt16InterleavedBuffer() {
  DecodedAudio buffer(kChannels, kSbMediaAudioSampleTypeInt16Deprecated,
                      kTimestampUsec, kSizeInBytes);
  int16_t* data = buffer.data_as_int16();
  for (int i = 0; i < kNumFrames * kChannels; ++i) {
    // Generates a linear ramp covering the full range of int16_t [-32768,
    // 32767].
    data[i] = static_cast<int16_t>(i % 65536 - 32768);
  }
  return buffer;
}

void BM_SwitchSampleType_Int16ToFloat_Cpp(benchmark::State& state) {
  auto src = CreateInt16InterleavedBuffer();
  for (auto _ : state) {
    auto dst = src.SwitchFormatTo(kSbMediaAudioSampleTypeFloat32,
                                  /*force_simd=*/false);
    benchmark::DoNotOptimize(dst);
  }
  state.SetItemsProcessed(state.iterations() * kNumFrames);
}
BENCHMARK(BM_SwitchSampleType_Int16ToFloat_Cpp);

void BM_SwitchSampleType_Int16ToFloat_Neon(benchmark::State& state) {
  auto src = CreateInt16InterleavedBuffer();
  for (auto _ : state) {
    auto dst = src.SwitchFormatTo(kSbMediaAudioSampleTypeFloat32,
                                  /*force_simd=*/true);
    benchmark::DoNotOptimize(dst);
  }
  state.SetItemsProcessed(state.iterations() * kNumFrames);
}
BENCHMARK(BM_SwitchSampleType_Int16ToFloat_Neon);

void BM_SwitchSampleType_FloatToInt16_Cpp(benchmark::State& state) {
  auto src = CreateFloatInterleavedBuffer();
  for (auto _ : state) {
    auto dst = src.SwitchFormatTo(kSbMediaAudioSampleTypeInt16Deprecated,
                                  /*force_simd=*/false);
    benchmark::DoNotOptimize(dst);
  }
  state.SetItemsProcessed(state.iterations() * kNumFrames);
}
BENCHMARK(BM_SwitchSampleType_FloatToInt16_Cpp);

void BM_SwitchSampleType_FloatToInt16_Neon(benchmark::State& state) {
  auto src = CreateFloatInterleavedBuffer();
  for (auto _ : state) {
    auto dst = src.SwitchFormatTo(kSbMediaAudioSampleTypeInt16Deprecated,
                                  /*force_simd=*/true);
    benchmark::DoNotOptimize(dst);
  }
  state.SetItemsProcessed(state.iterations() * kNumFrames);
}
BENCHMARK(BM_SwitchSampleType_FloatToInt16_Neon);

}  // namespace
}  // namespace starboard
