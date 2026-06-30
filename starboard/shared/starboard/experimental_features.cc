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

#include "starboard/shared/starboard/experimental_features.h"

#include <optional>
#include <ostream>
#include <type_traits>
#include <utility>

#include "starboard/common/log.h"
#include "starboard/extension/experimental/experimental_features.h"
#include "starboard/shared/starboard/player/decoded_audio_internal.h"

namespace starboard {
namespace {

std::optional<ExperimentalFeatures>& GetThreadLocalExperimentalFeatures() {
  thread_local std::optional<ExperimentalFeatures> instance;
  static_assert(sizeof(instance) < 256,
                "ExperimentalFeatures is too large for thread-local storage.");
  return instance;
}

const StarboardExtensionExperimentalFeaturesConfigurationApi
    kExperimentalFeaturesConfigurationApi = {
        kStarboardExtensionExperimentalFeaturesConfigurationName,
        1,
        &SetExperimentalFeaturesForCurrentThread,
};

}  // namespace

ExperimentalFeatures::ExperimentalFeatures(Map settings)
    : settings_(std::move(settings)) {}

void SetExperimentalFeaturesForCurrentThread(
    const StarboardExtensionExperimentalFeatures* extension_features) {
  // |extension_features| cannot be null. We use a pointer here to support C API
  // compatibility.
  SB_CHECK(extension_features);
  ExperimentalFeatures::Map map;
  if (extension_features->entries) {
    for (size_t i = 0; i < extension_features->entry_count; ++i) {
      if (const auto& [key, value] = extension_features->entries[i]; key) {
        map.insert_or_assign(key, value);
      }
    }
  }
  auto& experimental_features = GetThreadLocalExperimentalFeatures();
  experimental_features = ExperimentalFeatures(std::move(map));

  if (experimental_features->GetBool(
          kMediaEnableSimdBasedAudioFormatSwitching)) {
    DecodedAudio::EnableSimdBasedAudioFormatSwitching();
  }
}

const ExperimentalFeatures& GetExperimentalFeaturesForCurrentThread() {
  const auto& experimental_features = GetThreadLocalExperimentalFeatures();
  SB_CHECK(experimental_features.has_value())
      << "ExperimentalFeatures are not set. This method was likely "
         "called on the wrong thread or a race condition occurred.";
  return *experimental_features;
}

const void* GetExperimentalFeaturesConfigurationApi() {
  return &kExperimentalFeaturesConfigurationApi;
}

std::ostream& operator<<(std::ostream& os,
                         const ExperimentalFeatures& features) {
  os << "{";
  const char* delim = "";
  for (const auto& [key, value] : features.settings_) {
    os << std::exchange(delim, ", ") << key << "=";
    std::visit([&os](const auto& val) { os << val; }, value);
  }
  return os << "}";
}

}  // namespace starboard
