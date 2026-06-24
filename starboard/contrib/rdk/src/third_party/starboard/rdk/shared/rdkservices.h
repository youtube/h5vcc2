//
// Copyright 2020 Comcast Cable Communications Management, LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef THIRD_PARTY_STARBOARD_RDK_SHARED_RDKSERVICES_H_
#define THIRD_PARTY_STARBOARD_RDK_SHARED_RDKSERVICES_H_

#include "starboard/configuration.h"

#include <string>

struct SbAccessibilityCaptionSettings;
struct SbAccessibilityDisplaySettings;
struct SbMediaAudioConfiguration;

namespace starboard {

struct ResolutionInfo {
  ResolutionInfo() {}
  ResolutionInfo(int32_t w, int32_t h)
    : Width(w), Height(h) {}
  int32_t Width { 1920 };
  int32_t Height { 1080 };
};

class DisplayInfo {
public:
  enum HdrCaps : uint8_t {
    kHdrNone        = 0u,
    kHdr10          = (1u << 0),
    kHdr10Plus      = (1u << 1),
    kHdrHlg         = (1u << 2),
    kHdrDolbyVision = (1u << 3),
    kHdrTechnicolor = (1u << 4),
  };

  static ResolutionInfo GetResolution();
  static float GetDiagonalSizeInInches();
  static uint32_t GetHDRCaps();
};

class DeviceIdentification {
public:
  static std::string GetChipset();
  static std::string GetFirmwareVersion();
};

class NetworkInfo {
public:
  static void Initialize();
  static bool IsConnectionTypeWireless();
  static bool IsDisconnected();
};

class TextToSpeech {
public:
  static bool IsEnabled();
  static void Speak(const std::string& text);
  static void Cancel();
};

class Accessibility {
public:
  static void SetSettings(const std::string& json);
  static bool GetSettings(std::string& out_json);
  static bool GetCaptionSettings(SbAccessibilityCaptionSettings* out);
  static bool GetDisplaySettings(SbAccessibilityDisplaySettings* out);
};

class SystemProperties {
public:
  static void SetSettings(const std::string& json);
  static bool GetSettings(std::string& out_json);
  static bool GetChipset(std::string &out);
  static bool GetFirmwareVersion(std::string &out);
  static bool GetIntegratorName(std::string &out);
  static bool GetBrandName(std::string &out);
  static bool GetModelName(std::string &out);
  static bool GetModelYear(std::string &out);
  static bool GetFriendlyName(std::string &out);
  static bool GetDeviceType(std::string &out);
};

class AdvertisingId {
public:
  static void SetSettings(const std::string& json);
  static bool GetSettings(std::string& out_json);
  static bool GetIfa(std::string &out);
  static bool GetIfaType(std::string &out);
  static bool GetLmtAdTracking(std::string &out);
};

class AuthService {
public:
  static bool IsAvailable();
  static bool GetExperience(std::string &out);
};

class DeviceInfo {
public:
  static bool GetAudioConfiguration(int index, SbMediaAudioConfiguration* out_audio_configuration);
  static bool GetBrandName(std::string& out);
};

class UserSettings {
public:
  static bool GetPresentationLanguage(std::string &out);
};

void TeardownJSONRPCLink();

}  // namespace starboard

#endif  // THIRD_PARTY_STARBOARD_RDK_SHARED_RDKSERVICES_H_
