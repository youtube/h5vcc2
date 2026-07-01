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

#ifndef COMPONENTS_CDM_RENDERER_FAIRPLAY_KEY_SYSTEM_INFO_H_
#define COMPONENTS_CDM_RENDERER_FAIRPLAY_KEY_SYSTEM_INFO_H_

#include <string>

#include "base/containers/flat_set.h"
#include "media/base/eme_constants.h"
#include "media/base/key_system_info.h"

namespace cdm {

// YouTube/Cobalt-specific key system for FairPlay, distinct from Apple's
// standard "com.apple.fps" variants.
inline constexpr char kFairplayKeySystem[] = "com.youtube.fairplay";

// KeySystemInfo for the YouTube/Cobalt FairPlay key system on tvOS.
//
// This advertises the FairPlay-specific init data types and capability rules
// used by EME before requests are handed to the Starboard CDM. Instances are
// created on the renderer main thread during key-system registration and
// ownership is transferred via media::KeySystemInfos to media::KeySystemsImpl.
class FairplayKeySystemInfo : public media::KeySystemInfo {
 public:
  FairplayKeySystemInfo(
      media::SupportedCodecs codecs,
      base::flat_set<media::EncryptionScheme> encryption_schemes,
      media::SupportedCodecs hw_secure_codecs,
      media::EmeFeatureSupport persistent_state_support,
      media::EmeFeatureSupport distinctive_identifier_support);
  ~FairplayKeySystemInfo() override;

  std::string GetBaseKeySystemName() const override;
  bool IsSupportedKeySystem(const std::string& key_system) const override;
  bool ShouldUseBaseKeySystemName() const override;
  bool IsSupportedInitDataType(
      media::EmeInitDataType init_data_type) const override;
  media::EmeConfig::Rule GetEncryptionSchemeConfigRule(
      media::EncryptionScheme encryption_scheme) const override;
  media::SupportedCodecs GetSupportedCodecs() const override;
  media::SupportedCodecs GetSupportedHwSecureCodecs() const override;
  media::EmeConfig::Rule GetRobustnessConfigRule(
      const std::string& key_system,
      media::EmeMediaType media_type,
      const std::string& requested_robustness,
      const bool* hw_secure_requirement) const override;
  media::EmeConfig::Rule GetPersistentLicenseSessionSupport() const override;
  media::EmeFeatureSupport GetPersistentStateSupport() const override;
  media::EmeFeatureSupport GetDistinctiveIdentifierSupport() const override;

 private:
  const media::SupportedCodecs codecs_;
  const base::flat_set<media::EncryptionScheme> encryption_schemes_;
  const media::SupportedCodecs hw_secure_codecs_;
  const media::EmeFeatureSupport persistent_state_support_;
  const media::EmeFeatureSupport distinctive_identifier_support_;
};

}  // namespace cdm

#endif  // COMPONENTS_CDM_RENDERER_FAIRPLAY_KEY_SYSTEM_INFO_H_
