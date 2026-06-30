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

#ifndef STARBOARD_SHARED_STARBOARD_EXPERIMENTAL_FEATURES_H_
#define STARBOARD_SHARED_STARBOARD_EXPERIMENTAL_FEATURES_H_

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "starboard/extension/experimental/experimental_features.h"

namespace starboard {
namespace internal {

// Experiment framework uses 0 as the sentinel value for unset.
// e.g.)
// http://go/latestexpcl/player_web/features/player_web_cobalt.impl.gcl;l=332;rcl=862772714
constexpr int kH5vccUnsetSentinel = 0;

}  // namespace internal

// Strongly typed identifier for an experimental feature key used within
// Starboard.
//
// Threading & Lifetime: Lightweight constexpr wrappers around a compile-time
// string_view. Thread-safe and intended to be used as static constants.
template <typename T>
class ExperimentalFeatureKey {
 public:
  using ValueType = T;
  constexpr explicit ExperimentalFeatureKey(std::string_view key) : key_(key) {}
  constexpr std::string_view key() const { return key_; }

 private:
  std::string_view key_;
};

// Container for experimental feature settings stored per-thread in Starboard.
//
// Threading & Lifetime: Managed per thread via function-scoped thread-local
// storage. Instances are copyable and owned by the thread-local accessor.
class ExperimentalFeatures {
 public:
  using Value = std::variant<int64_t, std::string>;
  using Map = std::map<std::string, Value, std::less<>>;

  ExperimentalFeatures() = default;
  explicit ExperimentalFeatures(Map settings);
  ~ExperimentalFeatures() = default;

  // Returns the boolean value for the given key, falling back to false if the
  // key is missing or unset.
  bool GetBool(const ExperimentalFeatureKey<bool>& key) const {
    return Get(key).value_or(false);
  }

  template <typename T>
  std::optional<T> Get(const ExperimentalFeatureKey<T>& key) const {
    auto it = settings_.find(key.key());
    if (it == settings_.end()) {
      return std::nullopt;
    }
    return GetValue<T>(it->second);
  }

  friend std::ostream& operator<<(std::ostream& os,
                                  const ExperimentalFeatures& features);

 private:
  template <typename T>
  std::optional<T> GetValue(const Value& val) const;

  Map settings_;
};

template <typename T>
inline std::optional<T> ExperimentalFeatures::GetValue(const Value& val) const {
  static_assert(!std::is_same_v<T, T>,
                "Unsupported type for ExperimentalFeatures::Get");
  return std::nullopt;
}

template <>
inline std::optional<bool> ExperimentalFeatures::GetValue<bool>(
    const Value& val) const {
  auto* int_val = std::get_if<int64_t>(&val);
  if (!int_val) {
    return std::nullopt;
  }
  return *int_val != 0;
}

template <>
inline std::optional<int> ExperimentalFeatures::GetValue<int>(
    const Value& val) const {
  auto* int_val = std::get_if<int64_t>(&val);
  if (!int_val || *int_val == internal::kH5vccUnsetSentinel) {
    return std::nullopt;
  }
  if (*int_val < std::numeric_limits<int>::min() ||
      *int_val > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  return static_cast<int>(*int_val);
}

template <>
inline std::optional<std::string> ExperimentalFeatures::GetValue<std::string>(
    const Value& val) const {
  auto* str_val = std::get_if<std::string>(&val);
  if (!str_val) {
    return std::nullopt;
  }
  return *str_val;
}

// Sets the experimental features for the current thread.
void SetExperimentalFeaturesForCurrentThread(
    const StarboardExtensionExperimentalFeatures* experimental_features);

// Gets the experimental features for the current thread.
const ExperimentalFeatures& GetExperimentalFeaturesForCurrentThread();

// Get the extension API for configuring experimental features.
const void* GetExperimentalFeaturesConfigurationApi();

// -----------------------------------------------------------------------------
// Experimental Feature Key Constants
// -----------------------------------------------------------------------------
// Key constants for experimental features consumed directly within the
// Starboard platform implementation layer. For Chromium media layer settings,
// see media/base/starboard/experimental_features.h.
// keep-sorted start by_regex=k\w+ newline_separated=yes
inline constexpr ExperimentalFeatureKey<bool> kMediaAllowAudioWritingOnPause(
    "Media.AllowAudioWritingOnPause");

inline constexpr ExperimentalFeatureKey<bool> kMediaDecodedAudioBufferPool(
    "Media.DecodedAudioBufferPool");

inline constexpr ExperimentalFeatureKey<bool>
    kMediaEnableAv1StartupOptimization("Media.EnableAv1StartupOptimization");

inline constexpr ExperimentalFeatureKey<bool> kMediaEnableFlushDuringSeek(
    "Media.EnableFlushDuringSeek");

inline constexpr ExperimentalFeatureKey<bool> kMediaEnableLowLatency(
    "Media.EnableLowLatency");

inline constexpr ExperimentalFeatureKey<bool> kMediaEnableResetAudioDecoder(
    "Media.EnableResetAudioDecoder");

inline constexpr ExperimentalFeatureKey<bool>
    kMediaEnableSimdBasedAudioFormatSwitching(
        "Media.EnableSimdBasedAudioFormatSwitching");

inline constexpr ExperimentalFeatureKey<bool> kMediaEnableTrivialOptimizations(
    "Media.EnableTrivialOptimizations");

inline constexpr ExperimentalFeatureKey<bool>
    kMediaEnableVideoRendererVspAdjustment(
        "Media.EnableVideoRendererVspAdjustment");

inline constexpr ExperimentalFeatureKey<bool> kMediaFlushAudioTrackDuringSeek(
    "Media.FlushAudioTrackDuringSeek");

inline constexpr ExperimentalFeatureKey<bool> kMediaForceClearSurfaceView(
    "Media.ForceClearSurfaceView");

inline constexpr ExperimentalFeatureKey<bool>
    kMediaIgnoreMediaCodecCallbacksDuringFlushing(
        "Media.IgnoreMediaCodecCallbacksDuringFlushing");

inline constexpr ExperimentalFeatureKey<bool> kMediaSkipFlushOnDecoderTeardown(
    "Media.SkipFlushOnDecoderTeardown");

inline constexpr ExperimentalFeatureKey<bool> kMediaSkipVideoFramesOver60Fps(
    "Media.SkipVideoFramesOver60Fps");

inline constexpr ExperimentalFeatureKey<int>
    kMediaVideoDecoderInitialPrerollCount(
        "Media.VideoDecoderInitialPrerollCount");

inline constexpr ExperimentalFeatureKey<bool> kMediaVideoFrameImplPool(
    "Media.VideoFrameImplPool");

inline constexpr ExperimentalFeatureKey<int>
    kMediaVideoRendererMinDecodedFrames("Media.VideoRendererMinDecodedFrames");

inline constexpr ExperimentalFeatureKey<int> kMediaVideoRendererMinInputBuffers(
    "Media.VideoRendererMinInputBuffers");
// keep-sorted end

}  // namespace starboard

#endif  // STARBOARD_SHARED_STARBOARD_EXPERIMENTAL_FEATURES_H_
