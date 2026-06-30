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

#include "media/base/starboard/experimental_features.h"

#include <utility>

namespace media {

ExperimentalFeatures::ExperimentalFeatures() = default;
ExperimentalFeatures::ExperimentalFeatures(Map settings)
    : settings_(std::move(settings)) {}
ExperimentalFeatures::ExperimentalFeatures(const ExperimentalFeatures&) =
    default;
ExperimentalFeatures& ExperimentalFeatures::operator=(
    const ExperimentalFeatures&) = default;
ExperimentalFeatures::ExperimentalFeatures(ExperimentalFeatures&&) = default;
ExperimentalFeatures& ExperimentalFeatures::operator=(ExperimentalFeatures&&) =
    default;
ExperimentalFeatures::~ExperimentalFeatures() = default;

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

}  // namespace media
