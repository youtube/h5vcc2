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

#include "components/cdm/renderer/fairplay_key_system_info.h"

using media::EmeConfig;
using media::EmeFeatureSupport;
using media::EmeInitDataType;
using media::EmeMediaType;
using media::EncryptionScheme;
using media::SupportedCodecs;

namespace cdm {

FairplayKeySystemInfo::FairplayKeySystemInfo(
    SupportedCodecs codecs,
    base::flat_set<EncryptionScheme> encryption_schemes,
    SupportedCodecs hw_secure_codecs,
    EmeFeatureSupport persistent_state_support,
    EmeFeatureSupport distinctive_identifier_support)
    : codecs_(codecs),
      encryption_schemes_(std::move(encryption_schemes)),
      hw_secure_codecs_(hw_secure_codecs),
      persistent_state_support_(persistent_state_support),
      distinctive_identifier_support_(distinctive_identifier_support) {}

FairplayKeySystemInfo::~FairplayKeySystemInfo() = default;

std::string FairplayKeySystemInfo::GetBaseKeySystemName() const {
  return kFairplayKeySystem;
}

bool FairplayKeySystemInfo::IsSupportedKeySystem(
    const std::string& key_system) const {
  return key_system == kFairplayKeySystem;
}

bool FairplayKeySystemInfo::ShouldUseBaseKeySystemName() const {
  return true;
}

bool FairplayKeySystemInfo::IsSupportedInitDataType(
    EmeInitDataType init_data_type) const {
  // FairPlay supports sinf/skd (from WebKit) and fairplay (from Cobalt
  // AVPlayer).
  return init_data_type == EmeInitDataType::SINF ||
         init_data_type == EmeInitDataType::SKD ||
         init_data_type == EmeInitDataType::FAIRPLAY;
}

EmeConfig::Rule FairplayKeySystemInfo::GetEncryptionSchemeConfigRule(
    EncryptionScheme encryption_scheme) const {
  if (encryption_schemes_.contains(encryption_scheme)) {
    return EmeConfig::SupportedRule();
  }
  return EmeConfig::UnsupportedRule();
}

SupportedCodecs FairplayKeySystemInfo::GetSupportedCodecs() const {
  return codecs_;
}

SupportedCodecs FairplayKeySystemInfo::GetSupportedHwSecureCodecs() const {
  return hw_secure_codecs_;
}

EmeConfig::Rule FairplayKeySystemInfo::GetRobustnessConfigRule(
    const std::string& key_system,
    EmeMediaType media_type,
    const std::string& requested_robustness,
    const bool* hw_secure_requirement) const {
  // FairPlay does not define robustness levels (unlike Widevine), so only
  // accept empty robustness strings.
  return requested_robustness.empty() ? EmeConfig::SupportedRule()
                                      : EmeConfig::UnsupportedRule();
}

EmeConfig::Rule FairplayKeySystemInfo::GetPersistentLicenseSessionSupport()
    const {
  return EmeConfig::UnsupportedRule();
}

EmeFeatureSupport FairplayKeySystemInfo::GetPersistentStateSupport() const {
  return persistent_state_support_;
}

EmeFeatureSupport FairplayKeySystemInfo::GetDistinctiveIdentifierSupport()
    const {
  return distinctive_identifier_support_;
}

}  // namespace cdm
