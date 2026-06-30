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

#ifndef MEDIA_BASE_STARBOARD_EXPERIMENTAL_FEATURES_H_
#define MEDIA_BASE_STARBOARD_EXPERIMENTAL_FEATURES_H_

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

#include "media/base/media_export.h"

namespace media {
namespace internal {

// Experiment framework uses 0 as the sentinel value for unset.
// e.g.)
// http://go/latestexpcl/player_web/features/player_web_cobalt.impl.gcl;l=332;rcl=862772714
constexpr int kH5vccUnsetSentinel = 0;

}  // namespace internal

// Strongly typed identifier for an experimental feature key.
//
// Threading & Lifetime: Instances are lightweight constexpr wrappers around a
// compile-time string_view. They are thread-safe and can be safely shared
// across any thread or passed by value.
template <typename T>
class MEDIA_EXPORT ExperimentalFeatureKey {
 public:
  using ValueType = T;

  constexpr explicit ExperimentalFeatureKey(std::string_view key) : key_(key) {}

  constexpr std::string_view key() const { return key_; }

 private:
  std::string_view key_;
};

// Encapsulates a key-value mapping of experimental feature settings configured
// via H5vcc settings and passed to Starboard platform media components.
//
// Threading & Lifetime: Copyable and movable value container initialized during
// renderer startup or Mojo IPC configuration. Instances are not thread-safe for
// concurrent mutation; each component or thread should maintain its own
// instance.
class MEDIA_EXPORT ExperimentalFeatures {
 public:
  using Value = std::variant<int64_t, std::string>;
  using Map = std::map<std::string, Value, std::less<>>;

  ExperimentalFeatures();
  explicit ExperimentalFeatures(Map settings);
  ExperimentalFeatures(const ExperimentalFeatures&);
  ExperimentalFeatures& operator=(const ExperimentalFeatures&);
  ExperimentalFeatures(ExperimentalFeatures&&);
  ExperimentalFeatures& operator=(ExperimentalFeatures&&);
  ~ExperimentalFeatures();

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

  const Map& settings() const { return settings_; }

  friend MEDIA_EXPORT std::ostream& operator<<(
      std::ostream& os,
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

// -----------------------------------------------------------------------------
// Setting Key Constants
// -----------------------------------------------------------------------------
// Key constants for experimental features and settings consumed within the
// Chromium media layer. For platform-level Starboard features, see
// starboard/shared/starboard/experimental_features.h.
// keep-sorted start by_regex=k\w+ newline_separated=yes
inline constexpr ExperimentalFeatureKey<bool> kMediaBypassMojoForMedia(
    "Media.BypassMojoForMedia");

inline constexpr ExperimentalFeatureKey<bool> kMediaEnableTrivialOptimizations(
    "Media.EnableTrivialOptimizations");

inline constexpr ExperimentalFeatureKey<bool> kMediaForceClearSurfaceView(
    "Media.ForceClearSurfaceView");

inline constexpr ExperimentalFeatureKey<bool> kMediaForceDecodeToTexture(
    "Media.ForceDecodeToTexture");

inline constexpr ExperimentalFeatureKey<int> kMediaMaxSamplesPerWrite(
    "Media.MaxSamplesPerWrite");
// keep-sorted end

}  // namespace media

#endif  // MEDIA_BASE_STARBOARD_EXPERIMENTAL_FEATURES_H_
