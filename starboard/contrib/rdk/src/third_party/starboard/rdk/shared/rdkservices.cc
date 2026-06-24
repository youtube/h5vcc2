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

#ifndef MODULE_NAME
#define MODULE_NAME CobaltRDKServices
#endif

#include "third_party/starboard/rdk/shared/rdkservices.h"

#include <atomic>
#include <string>
#include <cstring>
#include <algorithm>
#include <memory>
#include <mutex>

#include <websocket/JSONRPCLink.h>

#include <interfaces/json/JsonData_HDRProperties.h>
#include <interfaces/json/JsonData_PlayerProperties.h>
#include <interfaces/json/JsonData_DeviceIdentification.h>
#include <interfaces/json/JsonData_DeviceInfo.h>

#ifdef HAS_SECURITY_AGENT
#include <securityagent/securityagent.h>
#endif

#include "starboard/audio_sink.h"
#include "starboard/event.h"
#include "starboard/media.h"
#include "starboard/common/once.h"
#include "starboard/common/media.h"
#include "starboard/extension/accessibility.h"
#include "starboard/common/file.h"
#include "starboard/shared/starboard/media/mime_supportability_cache.h"

#include "third_party/starboard/rdk/shared/accessibility_data.h"
#include "third_party/starboard/rdk/shared/log_override.h"
#include "third_party/starboard/rdk/shared/application_rdk.h"
#include "third_party/starboard/rdk/shared/player/player_internal.h"
#include "third_party/starboard/rdk/shared/time_constants.h"

MODULE_NAME_DECLARATION(BUILD_REFERENCE);

using namespace  WPEFramework;

namespace starboard {

namespace {

const uint32_t kDefaultTimeoutMs = 100;
const char kDisplayInfoCallsign[] = "DisplayInfo.1";
const char kPlayerInfoCallsign[] = "PlayerInfo.1";
const char kDeviceIdentificationCallsign[] = "DeviceIdentification.1";
const char kNetworkCallsign[] = "org.rdk.Network.1";
const char kTTSCallsign[] = "org.rdk.TextToSpeech.1";
const char kAuthServiceCallsign[] = "org.rdk.AuthService.1";
const char kUserSetingsCallsign[] = "org.rdk.UserSettings.1";
const char kDeviceInfoCallsign[] = "DeviceInfo.1";
const char kBluetoothCallsign[] = "org.rdk.Bluetooth.1";

const char kAuthServiceExperienceFile[] = "/opt/www/authService/experience.dat";

const uint32_t kPriviligedRequestErrorCode = -32604U;

class ServiceLink {
  std::unique_ptr<JSONRPC::LinkType<Core::JSON::IElement>> link_;
  std::string callsign_;

#ifdef HAS_SECURITY_AGENT
  static Core::OptionalType<std::string> getToken() {
    if (getenv("THUNDER_SECURITY_OFF") != nullptr)
      return { };

    const uint32_t kMaxBufferSize = 2 * 1024;
    const std::string payload = "https://www.youtube.com";

    Core::OptionalType<std::string> token;
    std::vector<uint8_t> buffer;
    buffer.resize(kMaxBufferSize);

    for(int i = 0; i < 5; ++i) {
      uint32_t inputLen = std::min(kMaxBufferSize, payload.length());
      ::memcpy (buffer.data(), payload.c_str(), inputLen);

      int outputLen = GetToken(kMaxBufferSize, inputLen, buffer.data());
      SB_DCHECK(outputLen != 0);

      if (outputLen > 0) {
        token = std::string(reinterpret_cast<const char*>(buffer.data()), outputLen);
        break;
      }
      else if (outputLen < 0) {
        uint32_t rc = -outputLen;
        if (rc == Core::ERROR_TIMEDOUT && i < 5) {
          SB_LOG(ERROR) << "Failed to get token, trying again. rc = " << rc << " ( " << Core::ErrorToString(rc) << " )";
          continue;
        }
        SB_LOG(ERROR) << "Failed to get token, give up. rc = " << rc << " ( " << Core::ErrorToString(rc) << " )";
      }
      break;
    }
    return token;
  }
#endif

  static std::string buildQuery() {
    std::string query;
#ifdef HAS_SECURITY_AGENT
    static const auto token = getToken();
    if (token.IsSet() && !token.Value().empty())
      query = "token=" + token.Value();
#endif
    return query;
  }

public:
  static bool enableEnvOverrides() {
    static bool enable_env_overrides = ([]() {
      std::string envValue;
      if ((Core::SystemInfo::GetEnvironment("COBALT_ENABLE_OVERRIDES", envValue) == true) && (envValue.empty() == false)) {
        return envValue.compare("1") == 0 || envValue.compare("true") == 0;
      }
      return false;
    })();
    return enable_env_overrides;
  }

  ServiceLink(const std::string callsign) : callsign_(callsign) {
  }

  template <typename PARAMETERS>
  uint32_t Get(const uint32_t waitTime, const string& method, PARAMETERS& sendObject) {
    if (enableEnvOverrides()) {
      std::string envValue;
      std::string envName = Core::JSONRPC::Message::Callsign(callsign_) + "_" + method;
      envName.erase(std::remove(envName.begin(), envName.end(), '.'), envName.end());
      if (Core::SystemInfo::GetEnvironment(envName, envValue) == true) {
        return sendObject.FromString(envValue) ? Core::ERROR_NONE : Core::ERROR_GENERAL;
      }
    }
    if (!EnsureLink())
      return Core::ERROR_UNAVAILABLE;
    return link_->template Get<PARAMETERS>(waitTime, method, sendObject);
  }

  template <typename PARAMETERS, typename HANDLER, typename REALOBJECT>
  uint32_t Dispatch(const uint32_t waitTime, const string& method, const PARAMETERS& parameters, const HANDLER& callback, REALOBJECT* objectPtr) {
    if (!EnsureLink())
      return Core::ERROR_UNAVAILABLE;
    return link_->template Dispatch<PARAMETERS, HANDLER, REALOBJECT>(waitTime, method, parameters, callback, objectPtr);
  }

  template <typename HANDLER, typename REALOBJECT>
  uint32_t Dispatch(const uint32_t waitTime, const string& method, const HANDLER& callback, REALOBJECT* objectPtr) {
    if (!EnsureLink())
      return Core::ERROR_UNAVAILABLE;
    return link_->template Dispatch<void, HANDLER, REALOBJECT>(waitTime, method, callback, objectPtr);
  }

  template <typename INBOUND, typename METHOD, typename REALOBJECT>
  uint32_t Subscribe(const uint32_t waitTime, const string& eventName, const METHOD& method, REALOBJECT* objectPtr) {
    if (!EnsureLink())
      return enableEnvOverrides() ? Core::ERROR_NONE : Core::ERROR_UNAVAILABLE;
    return link_->template Subscribe<INBOUND, METHOD, REALOBJECT>(waitTime, eventName, method, objectPtr);
  }

  void Unsubscribe(const uint32_t waitTime, const string& eventName) {
    if (!link_)
      return;
    return link_->Unsubscribe(waitTime, eventName);
  }

  void Teardown() {
    link_.reset();
  }

  bool EnsureLink() {
    if (!link_) {
      static const bool kHasThunderAccessEnv = (getenv("THUNDER_ACCESS") != nullptr);
      if (kHasThunderAccessEnv)
        link_.reset(new JSONRPC::LinkType<Core::JSON::IElement>(callsign_, nullptr, false, buildQuery()));
    }
    return !!link_;
  }
};

struct VariableTimeout {
  const uint32_t min_ms;   // milliseconds
  const uint64_t deadline; // ticks
  VariableTimeout(uint32_t min, uint32_t max)
    : min_ms(min), deadline(Core::Time::Now().Ticks() + max * Core::Time::TicksPerMillisecond) {
  }
  uint32_t value() const {
    auto now = Core::Time::Now().Ticks();
    return (now < deadline)
      ? std::max(static_cast<uint32_t>((deadline - now) / Core::Time::TicksPerMillisecond), min_ms)
      : min_ms;
  }
};

struct DeviceIdImpl {
  DeviceIdImpl() {
    JsonData::DeviceIdentification::DeviceidentificationData data;
    uint32_t rc = ServiceLink(kDeviceIdentificationCallsign)
      .Get(2000, "deviceidentification", data);
    if (Core::ERROR_NONE == rc) {
      chipset = data.Chipset.Value();
      firmware_version = data.Firmwareversion.Value();
      std::replace(chipset.begin(), chipset.end(), ' ', '-');
    }
    if (Core::ERROR_NONE != rc) {
      #if defined(SB_PLATFORM_CHIPSET_MODEL_NUMBER_STRING)
      chipset = SB_PLATFORM_CHIPSET_MODEL_NUMBER_STRING;
      #endif
      #if defined(SB_PLATFORM_FIRMWARE_VERSION_STRING)
      firmware_version = SB_PLATFORM_FIRMWARE_VERSION_STRING;
      #endif
    }
  }
  std::string chipset;
  std::string firmware_version;
};

SB_ONCE_INITIALIZE_FUNCTION(DeviceIdImpl, GetDeviceIdImpl);

struct AccessibilityImpl {
private:
  std::mutex mutex_;
  SbAccessibilityDisplaySettings display_settings_ { };
  SbAccessibilityCaptionSettings caption_settings_ { };
  bool is_voice_guidance_enabled_ { false };

public:
  AccessibilityImpl() {
    memset(&display_settings_, 0, sizeof(display_settings_));
    memset(&caption_settings_, 0, sizeof(caption_settings_));

    if (ServiceLink::enableEnvOverrides()) {
      std::string envValue;
      if (Core::SystemInfo::GetEnvironment("AccessibilitySettings_json", envValue) == true) {
        SetSettings(envValue);

        std::string test;
        bool r = GetSettings(test);
        SB_LOG(INFO) << "Initialized from 'AccessibilitySettings_json',"
                     << " env variable json: '" << envValue << "',"
                     << " conversion result: " << r << ","
                     << " accessibility setting json: '" << test << "'";
      }
    }
  }

  void SetSettings(const std::string& json) {
    SB_LOG(INFO) << "Updating accessibility settings: " << json;

    JsonData::Accessibility::AccessibilityData settings;
    Core::OptionalType<Core::JSON::Error> error;
    if ( !settings.FromString(json, error) ) {
      SB_LOG(ERROR) << "Failed to parse accessibility settings, error: "
                    << (error.IsSet() ? Core::JSON::ErrorDisplayMessage(error.Value()): "Unknown");
      return;
    }

    std::lock_guard lock(mutex_);

    bool was_cc_enabled = caption_settings_.is_enabled;
    bool was_highcontrast_enabled = display_settings_.is_high_contrast_text_enabled;

    memset(&display_settings_, 0, sizeof(display_settings_));
    memset(&caption_settings_, 0, sizeof(caption_settings_));

    const auto& cc = settings.ClosedCaptions;

    caption_settings_.supports_is_enabled = true;
    caption_settings_.supports_set_enabled = false;
    caption_settings_.is_enabled = cc.IsEnabled.Value();

    if (cc.BackgroundColor.IsSet()) {
      caption_settings_.background_color = cc.BackgroundColor.Value();
      caption_settings_.background_color_state = kSbAccessibilityCaptionStateSet;
    }
    if (cc.BackgroundOpacity.IsSet()) {
      caption_settings_.background_opacity = cc.BackgroundOpacity.Value();
      caption_settings_.background_opacity_state = kSbAccessibilityCaptionStateSet;
    }
    if (cc.CharacterEdgeStyle.IsSet()) {
      caption_settings_.character_edge_style = cc.CharacterEdgeStyle.Value();
      caption_settings_.character_edge_style_state = kSbAccessibilityCaptionStateSet;
    }
    if (cc.FontColor.IsSet()) {
      caption_settings_.font_color = cc.FontColor.Value();
      caption_settings_.font_color_state = kSbAccessibilityCaptionStateSet;
    }
    if (cc.FontFamily.IsSet()) {
      caption_settings_.font_family = cc.FontFamily.Value();
      caption_settings_.font_family_state = kSbAccessibilityCaptionStateSet;
    }
    if (cc.FontOpacity.IsSet()) {
      caption_settings_.font_opacity = cc.FontOpacity.Value();
      caption_settings_.font_opacity_state = kSbAccessibilityCaptionStateSet;
    }
    if (cc.FontSize.IsSet()) {
      caption_settings_.font_size = cc.FontSize.Value();
      caption_settings_.font_size_state = kSbAccessibilityCaptionStateSet;
    }
    if (cc.WindowColor.IsSet()) {
      caption_settings_.window_color = cc.WindowColor.Value();
      caption_settings_.window_color_state = kSbAccessibilityCaptionStateSet;
    }
    if (cc.WindowOpacity.IsSet()) {
      caption_settings_.window_opacity = cc.WindowOpacity.Value();
      caption_settings_.window_opacity_state = kSbAccessibilityCaptionStateSet;
    }

    if (settings.TextDisplay.IsHighContrastTextEnabled.IsSet()) {
      display_settings_.has_high_contrast_text_setting = true;
      display_settings_.is_high_contrast_text_enabled =
        settings.TextDisplay.IsHighContrastTextEnabled.Value();
    }
  }

  bool GetSettings(std::string& out_json) {
    JsonData::Accessibility::AccessibilityData settings;

    {
      std::lock_guard lock(mutex_);
      if (caption_settings_.supports_is_enabled) {
        auto& cc = settings.ClosedCaptions;
        cc.IsEnabled = caption_settings_.is_enabled;
        if (caption_settings_.background_color_state)
          cc.BackgroundColor = caption_settings_.background_color;
        if (caption_settings_.background_opacity_state)
          cc.BackgroundOpacity = caption_settings_.background_opacity;
        if (caption_settings_.character_edge_style_state)
          cc.CharacterEdgeStyle = caption_settings_.character_edge_style;
        if (caption_settings_.font_color_state)
          cc.FontColor = caption_settings_.font_color;
        if (caption_settings_.font_family_state)
          cc.FontFamily = caption_settings_.font_family;
        if (caption_settings_.font_opacity_state)
          cc.FontOpacity = caption_settings_.font_opacity;
        if (caption_settings_.font_size_state)
          cc.FontSize = caption_settings_.font_size;
        if (caption_settings_.window_color_state)
          cc.WindowColor = caption_settings_.window_color;
        if (caption_settings_.window_opacity_state)
          cc.WindowOpacity = caption_settings_.window_opacity;
      }

      if (display_settings_.has_high_contrast_text_setting)
        settings.TextDisplay.IsHighContrastTextEnabled = display_settings_.is_high_contrast_text_enabled;
    }

    return settings.ToString(out_json);
  }

  bool GetCaptionSettings(SbAccessibilityCaptionSettings* out) {
    if (out) {
      std::lock_guard lock(mutex_);
      memcpy(out, &caption_settings_,  sizeof(caption_settings_));
      return true;
    }
    return false;
  }

  bool GetDisplaySettings(SbAccessibilityDisplaySettings* out) {
    if (out) {
      std::lock_guard lock(mutex_);
      memcpy(out, &display_settings_,  sizeof(display_settings_));
      return true;
    }
    return false;
  }

  void SetCaptionEnabled(bool enabled, bool notify_on_change = true) {
    {
      std::lock_guard lock(mutex_);
       notify_on_change &= (caption_settings_.is_enabled != enabled);
       caption_settings_.is_enabled = enabled;
    }
  }

  void SetHighContrastEnabled(bool enabled, bool notify_on_change = true) {
    {
      std::lock_guard lock(mutex_);
      notify_on_change &= (display_settings_.is_high_contrast_text_enabled != enabled);
      display_settings_.is_high_contrast_text_enabled = enabled;
    }
  }

  void SetVoiceGuidanceEnabled(bool enabled, bool notify_on_change = true) {
    {
      std::lock_guard lock(mutex_);
      notify_on_change &= (is_voice_guidance_enabled_ != enabled);
      is_voice_guidance_enabled_ = enabled;
    }
    if (notify_on_change && ApplicationRdk::Get()) {
      SB_LOG(INFO) << "Accessibility voice guidance setting changed, enabled = " << enabled;
      ApplicationRdk::Get()->InjectAccessibilityTextToSpeechSettingsChanged(enabled);
    }
  }

  bool GetVoiceGuidanceEnabled() {
    std::lock_guard lock(mutex_);
    return is_voice_guidance_enabled_;
  }
};

SB_ONCE_INITIALIZE_FUNCTION(AccessibilityImpl, GetAccessibility);

struct TextToSpeechImpl {
private:
  std::atomic_bool is_enabled_ { false };
  std::atomic_bool needs_refresh_ { true };
  std::atomic_bool did_subscribe_ { false };
  int64_t speech_id_ { -1 };
  int32_t speech_request_num_ { 0 };
  ServiceLink tts_link_ { kTTSCallsign };
  std::mutex mutex_;
  std::condition_variable condition_;
  std::string client_id_;

  struct IsTTSEnabledInfo : public Core::JSON::Container {
    IsTTSEnabledInfo()
      : Core::JSON::Container() {
      Add(_T("isenabled"), &IsEnabled);
    }
    IsTTSEnabledInfo(const IsTTSEnabledInfo&) = delete;
    IsTTSEnabledInfo& operator=(const IsTTSEnabledInfo&) = delete;

    Core::JSON::Boolean IsEnabled;
  };

  struct SpeakResult : public Core::JSON::Container {
    SpeakResult()
      : Core::JSON::Container()
      , SpeechId(-1) {
      Add(_T("speechid"), &SpeechId);
    }
    SpeakResult(const SpeakResult&) = delete;
    SpeakResult& operator=(const SpeakResult&) = delete;

    Core::JSON::DecSInt64 SpeechId;
  };

  struct StateInfo : public Core::JSON::Container {
    StateInfo()
      : Core::JSON::Container()
      , State(false) {
      Add(_T("state"), &State);
    }
    StateInfo(const StateInfo& other)
      : Core::JSON::Container()
      , State(other.State) {
      Add(_T("state"), &State);
    }
    StateInfo& operator=(const StateInfo&) = delete;

    Core::JSON::Boolean State;
  };

  void OnCancelResult(const Core::JSON::String&, const Core::JSONRPC::Error*) {
  }

  void OnStateChanged(const StateInfo& info) {
    is_enabled_.store( info.State.Value() );
    GetAccessibility()->SetVoiceGuidanceEnabled( info.State.Value() );
  }

  void OnSpeakResult(const SpeakResult& result, const Core::JSONRPC::Error* err) {
    std::lock_guard lock(mutex_);
    if (err) {
      SB_LOG(ERROR)
          << "TTS speak request failed. Error code: "
          << err->Code.Value()
          << " message: "
          << err->Text.Value();
      speech_id_ = -1;
    }
    else {
      speech_id_ = result.SpeechId;
    }
    --speech_request_num_;
    condition_.notify_all();
  }

public:
  TextToSpeechImpl() = default;

  void Speak(const std::string &text) {
    if (!IsEnabled())
      return;

    JsonObject params;
    params.Set(_T("text"), text);
    params.Set(_T("callsign"), client_id_ );

    uint64_t rc = tts_link_.Dispatch(kDefaultTimeoutMs, "speak", params, &TextToSpeechImpl::OnSpeakResult, this);
    if (Core::ERROR_NONE == rc) {
      std::lock_guard lock(mutex_);
      ++speech_request_num_;
    }
  }

  void Cancel() {
    int64_t speechId = -1;

    {
      std::unique_lock lock(mutex_);
      if (speech_request_num_ != 0) {
        if (condition_.wait_for(lock, std::chrono::microseconds(kSbTimeMillisecond)) == std::cv_status::timeout || speech_request_num_ != 0)
          return;
      }
      speechId = std::exchange(speech_id_, -1);
    }

    if (speechId < 0)
      return;

    JsonObject params;
    params.Set(_T("speechid"), speechId);

    tts_link_.Dispatch(kDefaultTimeoutMs, "cancel", params, &TextToSpeechImpl::OnCancelResult, this);
  }

  bool IsEnabled() {
    Refresh();
    return is_enabled_.load() || GetAccessibility()->GetVoiceGuidanceEnabled();
  }

  void Refresh() {
    if (!needs_refresh_.load())
      return;

    uint32_t rc;
    if (!did_subscribe_.load()) {
      bool old_val = did_subscribe_.exchange(true);
      if (old_val == false) {
        rc = tts_link_.Subscribe<StateInfo>(kDefaultTimeoutMs, "onttsstatechanged", &TextToSpeechImpl::OnStateChanged, this);
        if (Core::ERROR_UNAVAILABLE == rc || kPriviligedRequestErrorCode == rc) {
          needs_refresh_.store(false);
          SB_LOG(ERROR) << "Failed to subscribe to '" << kTTSCallsign
                        << ".onstatechanged' event, rc=" << rc
                        << " ( " << Core::ErrorToString(rc) << " )";
          return;
        }
        if (Core::ERROR_NONE != rc && Core::ERROR_DUPLICATE_KEY != rc) {
          did_subscribe_.store(false);
          SB_LOG(ERROR) << "Failed to subscribe to '" << kTTSCallsign
                        << ".onstatechanged' event, rc=" << rc
                        << " ( " << Core::ErrorToString(rc) << " )."
                        << " Going to try again next time.";
          return;
        }
      }
    }

    IsTTSEnabledInfo info;
    rc = tts_link_.Get(kDefaultTimeoutMs, "isttsenabled", info);
    if (Core::ERROR_NONE == rc) {
      is_enabled_.store( info.IsEnabled.Value() );
    }
    if (Core::SystemInfo::GetEnvironment(_T("CLIENT_IDENTIFIER"), client_id_) == true) {
      std::string::size_type pos = client_id_.find(',');
      if (pos != std::string::npos)
        client_id_.erase(pos, std::string::npos);
    } else {
      client_id_ = "Cobalt";
    }

    needs_refresh_.store(false);
  }

  void Teardown() {
    if (did_subscribe_.load()) {
      tts_link_.Unsubscribe(kDefaultTimeoutMs, "onttsstatechanged");
      did_subscribe_.store(false);
    }
    tts_link_.Teardown();
    needs_refresh_.store(true);
    is_enabled_.store(false);
  }
};

SB_ONCE_INITIALIZE_FUNCTION(TextToSpeechImpl, GetTextToSpeech);

struct SystemPropertiesImpl {
  struct SystemPropertiesData : public Core::JSON::Container {
    SystemPropertiesData()
      : Core::JSON::Container() {
      Add(_T("modelname"), &ModelName);
      Add(_T("brandname"), &BrandName);
      Add(_T("modelyear"), &ModelYear);
      Add(_T("chipsetmodelnumber"), &ChipsetModelNumber);
      Add(_T("firmwareversion"), &FirmwareVersion);
      Add(_T("integratorname"), &IntegratorName);
      Add(_T("friendlyname"), &FriendlyName);
      Add(_T("devicetype"), &DeviceType);
    }
    SystemPropertiesData(const SystemPropertiesData&) = delete;
    SystemPropertiesData& operator=(const SystemPropertiesData&) = delete;

    Core::JSON::String ModelName;
    Core::JSON::String BrandName;
    Core::JSON::String ModelYear;
    Core::JSON::String ChipsetModelNumber;
    Core::JSON::String FirmwareVersion;
    Core::JSON::String IntegratorName;
    Core::JSON::String FriendlyName;
    Core::JSON::String DeviceType;
  };

  void SetSettings(const std::string& json) {
    std::lock_guard lock(mutex_);
    Core::OptionalType<Core::JSON::Error> error;
    if ( !props_.FromString(json, error) ) {
      props_.Clear();
      SB_LOG(ERROR) << "Failed to parse systemproperties settings, error: "
                    << (error.IsSet() ? Core::JSON::ErrorDisplayMessage(error.Value()): "Unknown");
      return;
    }
  }

  bool GetSettings(std::string& out_json) {
    std::lock_guard lock(mutex_);
    return props_.ToString(out_json);
  }

  bool GetModelName(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.ModelName.IsSet() && !props_.ModelName.Value().empty()) {
      out = props_.ModelName.Value();
      return true;
    }
    return false;
  }

  bool GetBrandName(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.BrandName.IsSet() && !props_.BrandName.Value().empty()) {
      out = props_.BrandName.Value();
      return true;
    }
    return false;
  }

  bool GetModelYear(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.ModelYear.IsSet() && !props_.ModelYear.Value().empty()) {
      out = props_.ModelYear.Value();
      return true;
    }
    return false;
  }

  bool GetChipset(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.ChipsetModelNumber.IsSet() && !props_.ChipsetModelNumber.Value().empty()) {
      out = props_.ChipsetModelNumber.Value();
      return true;
    }
    return false;
  }

  bool GetFirmwareVersion(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.FirmwareVersion.IsSet() && !props_.FirmwareVersion.Value().empty()) {
      out = props_.FirmwareVersion.Value();
      return true;
    }
    return false;
  }

  bool GetIntegratorName(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.IntegratorName.IsSet() && !props_.IntegratorName.Value().empty()) {
      out = props_.IntegratorName.Value();
      return true;
    }
    return false;
  }

  bool GetFriendlyName(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.FriendlyName.IsSet() && !props_.FriendlyName.Value().empty()) {
      out = props_.FriendlyName.Value();
      return true;
    }
    return false;
  }

  bool GetDeviceType(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.DeviceType.IsSet() && !props_.DeviceType.Value().empty()) {
      out = props_.DeviceType.Value();
      return true;
    }
    return false;
  }

private:
  std::mutex mutex_;
  SystemPropertiesData props_;
};

SB_ONCE_INITIALIZE_FUNCTION(SystemPropertiesImpl, GetSystemProperties);

struct AdvertisingIdImpl {
  struct AdvertisingData : public Core::JSON::Container {
    AdvertisingData()
      : Core::JSON::Container() {
      Add(_T("ifa"), &Ifa);
      Add(_T("ifa_type"), &IfaType);
      Add(_T("lmt"), &Lmt);
    }
    AdvertisingData(const AdvertisingData&) = delete;
    AdvertisingData& operator=(const AdvertisingData&) = delete;

    Core::JSON::String Ifa;
    Core::JSON::String IfaType;
    Core::JSON::String Lmt;
  };

  void SetSettings(const std::string& json) {
    std::lock_guard lock(mutex_);
    Core::OptionalType<Core::JSON::Error> error;
    if ( !props_.FromString(json, error) ) {
      props_.Clear();
      SB_LOG(ERROR) << "Failed to parse advertisingid settings, error: "
                    << (error.IsSet() ? Core::JSON::ErrorDisplayMessage(error.Value()): "Unknown");
      return;
    }
  }

  bool GetSettings(std::string& out_json) {
    std::lock_guard lock(mutex_);
    return props_.ToString(out_json);
  }

  bool GetIfa(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.Ifa.IsSet() && !props_.Ifa.Value().empty()) {
      out = props_.Ifa.Value();
      return true;
    }
    return false;
  }

  bool GetIfaType(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.IfaType.IsSet() && !props_.IfaType.Value().empty()) {
      out = props_.IfaType.Value();
      return true;
    }
    return false;
  }

  bool GetLmtAdTracking(std::string &out) {
    std::lock_guard lock(mutex_);
    if (props_.Lmt.IsSet() && !props_.Lmt.Value().empty()) {
      out = props_.Lmt.Value();
      return true;
    }
    return false;
  }

private:
  std::mutex mutex_;
  AdvertisingData props_;
};

SB_ONCE_INITIALIZE_FUNCTION(AdvertisingIdImpl, GetAdvertisingProperties);

struct AuthServiceImpl {
  bool IsAvailable() const {
    return is_available_;
  }

  bool GetExperience(std::string &out) {
    if (!IsAvailable())
      return false;

    std::lock_guard lock(mutex_);
    if (!experience_.empty()) {
      out = experience_;
      return true;
    }

    JsonObject data;
    uint32_t rc = ServiceLink(kAuthServiceCallsign)
      .Get(kDefaultTimeoutMs, "getExperience", data);
    if (Core::ERROR_NONE == rc && data.Get("success").Boolean()) {
      experience_ = data.Get("experience").Value();
      out = experience_;
      return true;
    }

    // Try to read directly from file
    ScopedFile file(kAuthServiceExperienceFile, O_RDONLY);
    if ( file.IsValid() ) {
      const int kBufferSize = 128;
      char buffer[kBufferSize];
      int bytes_read = file.ReadAll(buffer, kBufferSize);
      bytes_read = std::min(bytes_read, kBufferSize - 1);
      buffer[bytes_read] = '\0';
      experience_.assign(buffer);
      out = experience_;
      return true;
    }

    is_available_ = false;
    return false;
  }

private:
  std::mutex mutex_;
  bool is_available_ { true };
  std::string experience_;
};

SB_ONCE_INITIALIZE_FUNCTION(AuthServiceImpl, GetAuthService);

struct DisplayInfoImpl {
  ResolutionInfo GetResolution() {
    Refresh();
    return resolution_info_;
  }
  uint32_t GetHDRCaps() {
    Refresh();
    return hdr_caps_;
  }
  float GetDiagonalSizeInInches() {
    Refresh();
    return diagonal_size_in_inches_;
  }
  void Teardown() {
    if (did_subscribe_.load()) {
      display_info_.Unsubscribe(kDefaultTimeoutMs, "updated");
      did_subscribe_.store(false);
    }
    display_info_.Teardown();
    needs_refresh_.store(true);
  }

private:
  void Refresh();
  void OnUpdated(const Core::JSON::String&);
  void ForceNeedsRefresh() {  needs_refresh_.store(true); }

  ServiceLink display_info_ { kDisplayInfoCallsign };
  ResolutionInfo resolution_info_ { };
  uint32_t hdr_caps_ { DisplayInfo::kHdrNone };
  float diagonal_size_in_inches_ { 0.f };
  std::atomic_bool needs_refresh_ { true };
  std::atomic_bool did_subscribe_ { false };
};

SB_ONCE_INITIALIZE_FUNCTION(DisplayInfoImpl, GetDisplayInfo);

void DisplayInfoImpl::Refresh() {
  if (!needs_refresh_.load())
    return;

  VariableTimeout timeout(kDefaultTimeoutMs, 1000);
  uint32_t rc;

  if (!did_subscribe_.load()) {
    bool old_val = did_subscribe_.exchange(true);
    if (old_val == false) {
      rc = display_info_.Subscribe<Core::JSON::String>(timeout.value(), "updated", &DisplayInfoImpl::OnUpdated, this);
      if (Core::ERROR_UNAVAILABLE == rc || kPriviligedRequestErrorCode == rc) {
        needs_refresh_.store(false);
        SB_LOG(ERROR) << "Failed to subscribe to '" << kDisplayInfoCallsign
                      << ".updated' event, rc=" << rc
                      << " ( " << Core::ErrorToString(rc) << " )";
        return;
      }
      if (Core::ERROR_NONE != rc && Core::ERROR_DUPLICATE_KEY != rc) {
        did_subscribe_.store(false);
        SB_LOG(ERROR) << "Failed to subscribe to '" << kDisplayInfoCallsign
                      << ".updated' event, rc=" << rc
                      << " ( " << Core::ErrorToString(rc) << " )."
                      << " Going to try again next time.";
        return;
      }
    }
  }

  bool needs_refresh = false;

  Core::JSON::String resolution;
  rc = ServiceLink(kPlayerInfoCallsign).Get(timeout.value(), "resolution", resolution);
  if (Core::ERROR_NONE == rc && resolution.IsSet()) {
    if (resolution.Value().find("Resolution2160") != std::string::npos) {
      resolution_info_ = ResolutionInfo { 3840 , 2160 };
    } else {
      resolution_info_ = ResolutionInfo { 1920 , 1080 };
    }
  } else {
    needs_refresh |= (Core::ERROR_ASYNC_FAILED == rc || Core::ERROR_TIMEDOUT  == rc);
    resolution_info_ = ResolutionInfo { 1920 , 1080 };
    SB_LOG(ERROR) << "Failed to get 'resolution', rc=" << rc << " ( " << Core::ErrorToString(rc) << " )";
  }

  Core::JSON::DecUInt16 widthincentimeters, heightincentimeters;
  rc = display_info_.Get(timeout.value(), "widthincentimeters", widthincentimeters);
  if (Core::ERROR_NONE != rc) {
    needs_refresh |= (Core::ERROR_ASYNC_FAILED == rc);
    widthincentimeters.Clear();
    SB_LOG(ERROR) << "Failed to get 'DisplayInfo.widthincentimeters', rc=" << rc << " ( " << Core::ErrorToString(rc) << " )";
  }

  rc = display_info_.Get(timeout.value(), "heightincentimeters", heightincentimeters);
  if (Core::ERROR_NONE != rc) {
    needs_refresh |= (Core::ERROR_ASYNC_FAILED == rc);
    heightincentimeters.Clear();
    SB_LOG(ERROR) << "Failed to get 'DisplayInfo.heightincentimeters', rc=" << rc << " ( " << Core::ErrorToString(rc) << " )";
  }

  if (widthincentimeters && heightincentimeters) {
    diagonal_size_in_inches_ = sqrtf(powf(widthincentimeters, 2) + powf(heightincentimeters, 2)) / 2.54f;
  } else {
    diagonal_size_in_inches_ = 0.f;
  }

  auto detectHdrCaps = [&](const char* method)
  {
    using HdrTypes = Core::JSON::ArrayType<Core::JSON::EnumType<Exchange::IHDRProperties::HDRType>>;

    HdrTypes types;

    uint32_t rc = display_info_.Get(timeout.value(), method, types);
    if (Core::ERROR_NONE != rc) {
      needs_refresh |= (Core::ERROR_ASYNC_FAILED == rc || Core::ERROR_TIMEDOUT  == rc);
      SB_LOG(ERROR) << "Failed to get '" << method << "', rc=" << rc << " ( " << Core::ErrorToString(rc) << " )";
      return 0u;
    }

    uint32_t result = 0u;
    auto index(types.Elements());
    while (index.Next()) {
      switch(index.Current()) {
        case Exchange::IHDRProperties::HDR_10:
          result |= DisplayInfo::kHdr10;
          break;
        case Exchange::IHDRProperties::HDR_10PLUS:
          result |= DisplayInfo::kHdr10Plus;
          break;
        case Exchange::IHDRProperties::HDR_HLG:
          result |= DisplayInfo::kHdrHlg;
          break;
        case Exchange::IHDRProperties::HDR_DOLBYVISION:
          result |= DisplayInfo::kHdrDolbyVision;
          break;
        case Exchange::IHDRProperties::HDR_TECHNICOLOR:
          result |= DisplayInfo::kHdrTechnicolor;
          break;
        default:
          break;
      }
    }
    return result;
  };

  uint32_t tv_caps = detectHdrCaps("tvcapabilities");
  uint32_t stb_caps = detectHdrCaps("stbcapabilities");

  hdr_caps_ = tv_caps & stb_caps;

  needs_refresh_.store(false);

  if (needs_refresh) {
    SbEventSchedule([](void* data) {
      MimeSupportabilityCache::GetInstance()->ClearCachedMimeSupportabilities();
      GetDisplayInfo()->ForceNeedsRefresh();
    }, nullptr, kSbTimeSecond);
  }

  SB_LOG(INFO) << "Display info updated, resolution: "
               << resolution_info_.Width << 'x' << resolution_info_.Height
               << ", hdr caps: 0x" << std::hex << hdr_caps_
               << " (tvcaps: 0x"<< std::hex << tv_caps
               << ", stbcaps: 0x" << std::hex << stb_caps << ")"
               << ", diagonal size in inches: " << std::dec << diagonal_size_in_inches_;
}

void DisplayInfoImpl::OnUpdated(const Core::JSON::String&) {
  if (needs_refresh_.load() == false) {
    needs_refresh_.store(true);
    SbEventSchedule([](void* data) {
      // Clear mime cache until display info is updated
      MimeSupportabilityCache::GetInstance()->ClearCachedMimeSupportabilities();
      ApplicationRdk::Get()->DisplayInfoChanged();
    }, nullptr, 0);
  }
}

struct NetworkInfoImpl {
private:
  ServiceLink network_link_ { kNetworkCallsign };
  std::atomic_bool needs_refresh_ { true };
  std::atomic_bool did_subscribe_ { false };
  std::atomic_bool is_connected_  { false };
  std::atomic_bool is_connection_type_wireless_ { false };
  std::mutex mutex_;
  SbEventId event_id_ { kSbEventIdInvalid };

  struct InterfaceInfo : public Core::JSON::Container {
    InterfaceInfo()
      : Core::JSON::Container() {
      Init();
    }
    InterfaceInfo(const InterfaceInfo& other)
      : Core::JSON::Container()
      , InterfaceName(other.InterfaceName)
      , IsConnected(other.IsConnected)  {
      Init();
    }
    InterfaceInfo& operator=(const InterfaceInfo& rhs) {
      InterfaceName = rhs.InterfaceName;
      IsConnected = rhs.IsConnected;
      return *this;
    }
    Core::JSON::String  InterfaceName;
    Core::JSON::Boolean IsConnected;
  private:
    void Init() {
      Add(_T("interface"), &InterfaceName);
      Add(_T("connected"), &IsConnected);
    }
  };

  struct InterfacesInfo : public Core::JSON::Container {
    InterfacesInfo(const InterfacesInfo&) = delete;
    InterfacesInfo& operator=(const InterfacesInfo&) = delete;
    InterfacesInfo()
      : Core::JSON::Container() {
      Add(_T("interfaces"), &Interfaces);
    }
    Core::JSON::ArrayType<InterfaceInfo> Interfaces;
  };

  void ScheduleRefresh(int64_t timeout) {
    std::lock_guard lock(mutex_);
    if (event_id_ == kSbEventIdInvalid) {
      needs_refresh_.store(true);
      event_id_ = SbEventSchedule([](void* data) {
        auto& self = *static_cast<NetworkInfoImpl*>(data);
        self.mutex_.lock();
        self.event_id_ = kSbEventIdInvalid;
        self.mutex_.unlock();
        self.Refresh();
      }, this, timeout);
    }
  }

  void Refresh() {
    if (!needs_refresh_.load())
      return;

    uint32_t rc;
    if (!did_subscribe_.load()) {
      bool old_val = did_subscribe_.exchange(true);
      if (old_val == false) {
        rc = network_link_.Subscribe<Core::JSON::String>(
          kDefaultTimeoutMs, "onConnectionStatusChanged",
          &NetworkInfoImpl::OnConnectionStatusChanged, this);
        if (Core::ERROR_NONE != rc && Core::ERROR_DUPLICATE_KEY != rc) {
          SB_LOG(ERROR) << "Failed to subscribe to '" << kNetworkCallsign
                        << ".onConnectionStatusChanged' event, rc = " << rc
                        << " ( " << Core::ErrorToString(rc) << " )";
          did_subscribe_.store(false);
        }
      }
    }

    InterfacesInfo info;
    rc = network_link_.Get(kDefaultTimeoutMs, "getInterfaces", info);
    if (Core::ERROR_UNAVAILABLE == rc || kPriviligedRequestErrorCode == rc) {
      SB_LOG(ERROR) << "'" << kNetworkCallsign << ".getInterfaces' failed, rc = " << rc
                    << " ( " << Core::ErrorToString(rc) << " )";
      needs_refresh_.store(false);
      return;
    }
    else if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "'" << kNetworkCallsign << ".getInterfaces' failed, rc = " << rc
                    << " ( " << Core::ErrorToString(rc) << " ). Trying again in 5 seconds.";
      ScheduleRefresh(5 * kSbTimeSecond);
    }
    else {
      needs_refresh_.store(false);

      bool has_connected_interface = false;
      auto index(info.Interfaces.Elements());
      while (index.Next()) {
        const auto& it = index.Current();
        if (it.IsConnected) {
          SB_LOG(INFO) << "Found connected interface: " << it.InterfaceName.Value();
          has_connected_interface = true;
          break;
        }
      }
      if (!has_connected_interface) {
        SB_LOG(INFO) << "All interfaces are disconnected...";
      }

      if (is_connected_.load() != has_connected_interface) {
        is_connected_.store(has_connected_interface);
        if (has_connected_interface)
          ApplicationRdk::Get()->InjectOsNetworkConnectedEvent();
        else
          ApplicationRdk::Get()->InjectOsNetworkDisconnectedEvent();
      }
    }

    InterfaceInfo default_interface;
    rc = network_link_.Get(kDefaultTimeoutMs, "getDefaultInterface", default_interface);
    if (Core::ERROR_NONE == rc) {
      std::string connection_type = default_interface.InterfaceName.Value();
      SB_LOG(INFO) << "Default connection type: " << connection_type;
      is_connection_type_wireless_.store(0 == connection_type.compare("WIFI"));
    }
    else {
      SB_LOG(INFO) << "Failed to get default interface, rc = " << rc
                   << " ( " << Core::ErrorToString(rc) << " )";
    }
  }

  void OnConnectionStatusChanged(const Core::JSON::String&) {
    ScheduleRefresh(100 * kSbTimeMillisecond);
  }

public:
  void Initialize() {
    Refresh();
  }

  bool IsDisconnected() {
    Refresh();
    return !is_connected_.load();
  }

  bool IsConnectionTypeWireless() {
    Refresh();
    return is_connection_type_wireless_.load();
  }

  void Teardown() {
    if (did_subscribe_.load()) {
      network_link_.Unsubscribe(kDefaultTimeoutMs, "onConnectionStatusChanged");
      did_subscribe_.store(false);
    }
    network_link_.Teardown();
    needs_refresh_.store(true);
    std::lock_guard lock(mutex_);
    if (event_id_ != kSbEventIdInvalid) {
      SbEventCancel(event_id_);
      event_id_ = kSbEventIdInvalid;
    }
  }
};

SB_ONCE_INITIALIZE_FUNCTION(NetworkInfoImpl, GetNetworkInfo);

struct DeviceInfoImpl {

  bool GetAudioConfiguration(int index, SbMediaAudioConfiguration* out_audio_configuration);

  void Teardown() {
    if (did_subscribe_.load()) {
      bluetooth_.Unsubscribe(kDefaultTimeoutMs, "onStatusChanged");
      did_subscribe_.store(false);
    }
    device_info_.Teardown();
    bluetooth_.Teardown();
    needs_refresh_.store(true);
  }

  bool GetBrandName(std::string& out);

private:
  struct DeviceDetailsData : public Core::JSON::Container {
    DeviceDetailsData()
      : Core::JSON::Container() {
      Init();
    }
    DeviceDetailsData(const DeviceDetailsData& other)
      : Core::JSON::Container()
      , Name(other.Name)
      , Devicetype(other.Devicetype) {
      Init();
    }
    DeviceDetailsData& operator=(const DeviceDetailsData& rhs) {
      Name = rhs.Name;
      Devicetype = rhs.Devicetype;
      return *this;
    }
    Core::JSON::String Name;
    Core::JSON::String Devicetype;
  private:
    void Init() {
      Add(_T("name"), &Name);
      Add(_T("deviceType"), &Devicetype);
    }
  };

  struct ConnectedDevicesData : public Core::JSON::Container {
    ConnectedDevicesData()
      : Core::JSON::Container() {
      Add(_T("connectedDevices"), &Connecteddevices);
    }
    ConnectedDevicesData(const ConnectedDevicesData&) = delete;
    ConnectedDevicesData& operator=(const ConnectedDevicesData&) = delete;
    Core::JSON::ArrayType<DeviceDetailsData> Connecteddevices;
  };

  struct StatusChangedData : public Core::JSON::Container {
    StatusChangedData()
      : Core::JSON::Container() {
      Init();
    }
    StatusChangedData(const StatusChangedData& other)
      : Core::JSON::Container()
      , Name(other.Name)
      , Newstatus(other.Newstatus)
      , Devicetype(other.Devicetype)
      , Connected(other.Connected) {
      Init();
    }
    StatusChangedData& operator=(const StatusChangedData& rhs) {
      Name = rhs.Name;
      Devicetype = rhs.Devicetype;
      Newstatus = rhs.Newstatus;
      Connected = rhs.Connected;
      return *this;
    }
    Core::JSON::String Name;
    Core::JSON::String Newstatus;
    Core::JSON::String Devicetype;
    Core::JSON::Boolean Connected;
  private:
    void Init() {
      Add(_T("name"), &Name);
      Add(_T("newStatus"), &Newstatus);
      Add(_T("deviceType"), &Devicetype);
      Add(_T("connected"), &Connected);
    }
  };

  void OnBluetoothStatusChanged(const StatusChangedData&);
  void Refresh();
  void ForceNeedsRefresh() {  needs_refresh_.store(true); }
  void InitAudioConfigurationForAudioPort(const std::string& port_name, SbMediaAudioConfiguration* out);

  static bool IsAudioOutputDeviceType(const std::string& type) {
    return (strcasestr(type.c_str(), "audio") ||
            strcasestr(type.c_str(), "headset") ||
            strcasestr(type.c_str(), "headphones") ||
            strcasestr(type.c_str(), "loudspeaker") ||
            strcasestr(type.c_str(), "handsfree"));
  };

  ServiceLink device_info_ { kDeviceInfoCallsign };
  ServiceLink bluetooth_ { kBluetoothCallsign };

  std::atomic_bool did_subscribe_ { false };
  std::atomic_bool needs_refresh_ { true };
  std::atomic_bool has_bluetooth_audio_connected_ { false };
  std::mutex mutex_;

  std::vector<SbMediaAudioConfiguration> audio_configurations_;
  Core::OptionalType<std::string> brand_name_;

  static constexpr SbMediaAudioConnector kAudioConnectorUnknown = static_cast<SbMediaAudioConnector>(0);
};

SB_ONCE_INITIALIZE_FUNCTION(DeviceInfoImpl, GetDeviceInfo);

void DeviceInfoImpl::InitAudioConfigurationForAudioPort(const std::string& port_name, SbMediaAudioConfiguration* audio_configuration)
{
  if (!audio_configuration)
    return;

  const auto& connectorType = [](const std::string& name) {
    if (strncasecmp(name.c_str(), "hdmi", 4) == 0)
      return kSbMediaAudioConnectorHdmi;
    else if (strncasecmp(name.c_str(), "spdif", 5) == 0)
      return kSbMediaAudioConnectorSpdif;
    else if (strncasecmp(name.c_str(), "bluetooth", 9) == 0)
      return kSbMediaAudioConnectorBluetooth;
    else if (strncasecmp(name.c_str(), "speaker", 7) == 0)
      return kSbMediaAudioConnectorBuiltIn;
    return kAudioConnectorUnknown;
  };

  memset(audio_configuration, 0, sizeof(SbMediaAudioConfiguration));

  audio_configuration->connector = connectorType(port_name);
  audio_configuration->coding_type = kSbMediaAudioCodingTypePcm;
  audio_configuration->number_of_channels = SbAudioSinkGetMaxChannels();
  return;
}

void DeviceInfoImpl::OnBluetoothStatusChanged(const StatusChangedData& data) {
  const char kConnectionChange[] = "CONNECTION_CHANGE";

  SB_LOG(INFO) << "Bluetooth status changed, new status: " << data.Newstatus.Value();

  if (data.Newstatus.Value().compare(0, sizeof(kConnectionChange), kConnectionChange) != 0)
    return;

  if (!IsAudioOutputDeviceType(data.Devicetype.Value()))
    return;

  SB_LOG(INFO) << "Audio device change."
               << " name: '" << data.Name.Value() << "',"
               << " type: '" << data.Devicetype.Value() << "',"
               << " connected: " << (data.Connected.Value() ? "yes" : "no");

  const auto& hasBluetoothConnector = [&]() -> bool {
    std::lock_guard lock(mutex_);
    return std::find_if(audio_configurations_.begin(), audio_configurations_.end(), [](const SbMediaAudioConfiguration& cfg) {
      return cfg.connector == kSbMediaAudioConnectorBluetooth;
    }) != audio_configurations_.end();
  };

  has_bluetooth_audio_connected_.store(data.Connected.Value());

  ForceNeedsRefresh();

  // Interrupt player only if new wireless device got connected
  if (data.Connected.Value() && !hasBluetoothConnector()) {
    SbEventSchedule([](void*) {
      AudioConfigurationChanged();
    }, nullptr, 0);
  }
}

void DeviceInfoImpl::Refresh() {
  if ( !needs_refresh_.load() || !needs_refresh_.exchange( false ) )
    return;

  std::vector<SbMediaAudioConfiguration> audio_configs;

  uint32_t rc;
  bool needs_refresh = false;
  VariableTimeout timeout { kDefaultTimeoutMs, 1000 };

  if (did_subscribe_.load() == false && did_subscribe_.exchange(true) == false) {
    rc = bluetooth_.Subscribe<StatusChangedData>(timeout.value(), "onStatusChanged", &DeviceInfoImpl::OnBluetoothStatusChanged, this);
    if (Core::ERROR_NONE != rc && Core::ERROR_DUPLICATE_KEY != rc) {
      SB_LOG(ERROR) << "Failed to subscribe to '" << kBluetoothCallsign
                    << ".onStatusChanged' event, rc=" << rc
                    << " ( " << Core::ErrorToString(rc) << " )";
    }
  }

  using namespace WPEFramework::JsonData::DeviceInfo;
  SupportedaudioportsData audio_ports;
  rc = device_info_.Get(timeout.value(), "supportedaudioports", audio_ports);
  if (Core::ERROR_NONE != rc) {
    SB_LOG(ERROR) << "'" << kDeviceInfoCallsign << ".supportedaudioports' failed, rc = " << rc
                  << " ( " << Core::ErrorToString(rc) << " )";
    needs_refresh |= (Core::ERROR_ASYNC_FAILED == rc || Core::ERROR_TIMEDOUT  == rc);
  } else if (audio_ports.SupportedAudioPorts.Length() == 0) {
    SB_LOG(INFO) << "No supported audio ports.";
  } else {
    auto index(audio_ports.SupportedAudioPorts.Elements());
    while (index.Next()) {
      const auto& port_name = index.Current().Value();
      SB_LOG(INFO) << "Supported audio port name: " << port_name;
      SbMediaAudioConfiguration configuration;
      InitAudioConfigurationForAudioPort(port_name, &configuration);
      if (configuration.connector != kAudioConnectorUnknown)
        audio_configs.push_back(std::move(configuration));
    }
  }

  if (has_bluetooth_audio_connected_.load()) {
    SbMediaAudioConfiguration configuration;
    InitAudioConfigurationForAudioPort("bluetooth", &configuration);
    audio_configs.push_back(std::move(configuration));
  } else {
    ConnectedDevicesData connected_devices;
    rc = bluetooth_.Get(timeout.value(), "getConnectedDevices", connected_devices);
    if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "'" << kBluetoothCallsign << ".getConnectedDevices' failed, rc = " << rc
                    << " ( " << Core::ErrorToString(rc) << " )";
      needs_refresh |= (Core::ERROR_ASYNC_FAILED == rc || Core::ERROR_TIMEDOUT  == rc);
    } else if (connected_devices.Connecteddevices.Length() == 0) {
      SB_LOG(INFO) << "No bluetooth connected devices.";
    } else {
      auto index(connected_devices.Connecteddevices.Elements());
      while (index.Next()) {
        const auto& device_details = index.Current();
        SB_LOG(INFO) << "Bluetooth device name: " << device_details.Name.Value() << ", type: " << device_details.Devicetype.Value();
        if (IsAudioOutputDeviceType(device_details.Devicetype.Value())) {
          SbMediaAudioConfiguration configuration;
          InitAudioConfigurationForAudioPort("bluetooth", &configuration);
          audio_configs.push_back(std::move(configuration));
          break;
        }
      }
    }
  }

  if (audio_configs.empty()) {
    SbMediaAudioConfiguration configuration;
    InitAudioConfigurationForAudioPort("", &configuration);
    audio_configs.push_back(std::move(configuration));
  }

  if (needs_refresh) {
    SbEventSchedule([](void* data) {
      GetDeviceInfo()->ForceNeedsRefresh();
    }, nullptr, kSbTimeSecond);
  }

  SB_LOG(INFO) << "Updated audio configuration:";
  for (const auto& config : audio_configs) {
    SB_LOG(INFO) << " connector: " << (uint32_t) config.connector << " (" << GetMediaAudioConnectorName(config.connector) << ")";
  }

  std::lock_guard lock(mutex_);
  std::swap(audio_configurations_, audio_configs);
}

bool DeviceInfoImpl::GetAudioConfiguration(int output_index, SbMediaAudioConfiguration* out_configuration) {
  SB_DCHECK(output_index >= 0);
  SB_DCHECK(out_configuration);

  if (!out_configuration || output_index < 0)
    return false;

  Refresh();

  std::lock_guard lock(mutex_);
  size_t index = output_index;
  if (index < audio_configurations_.size()) {
    *out_configuration = audio_configurations_[index];
    return true;
  }
  else if (index == 0) {
    InitAudioConfigurationForAudioPort("", out_configuration);
    return true;
  }

  return false;
}

bool DeviceInfoImpl::GetBrandName(std::string& out) {
  if (!brand_name_.IsSet()) {
    struct BrandNameInfo : public Core::JSON::Container {
      BrandNameInfo() : Core::JSON::Container() {
          Add(_T("brand"), &BrandName);
      }
      Core::JSON::String BrandName;
    } info;
    uint32_t rc = device_info_.Get(kDefaultTimeoutMs, "brandname", info);
    if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "Failed to get '" << kDeviceInfoCallsign
                    << ".brandname', rc=" << rc
                    << " ( " << Core::ErrorToString(rc) << " ).";
      if (rc == Core::ERROR_ASYNC_FAILED || rc == Core::ERROR_TIMEDOUT) {
        brand_name_.Clear();
      } else {
      brand_name_ = "";
      }
    } else {
      brand_name_ = info.BrandName.Value();
      SB_LOG(INFO) << "Device brandname: " << brand_name_.Value();
    }
  }
  out = brand_name_.Value();
  return !out.empty();
}

struct UserSettingsImpl {
private:
  std::mutex mutex_;
  std::vector<std::string> subscriptions_ { };
  bool needs_refresh_ { true };
  ServiceLink link_ { kUserSetingsCallsign };

  struct StateChangedInfo : public Core::JSON::Container {
    StateChangedInfo()
      : Core::JSON::Container()
      , Enabled(false) {
      Add(_T("enabled"), &Enabled);
    }
    StateChangedInfo(const StateChangedInfo& other)
      : Core::JSON::Container()
      , Enabled(other.Enabled) {
      Add(_T("enabled"), &Enabled);
    }
    StateChangedInfo& operator=(const StateChangedInfo&) = delete;

    Core::JSON::Boolean Enabled;
  };

  void OnVoiceGuidanceChanged(const StateChangedInfo& info) {
    bool is_voice_guidance_enabled = info.Enabled.Value();
    SB_LOG(INFO) << "User setting changed. voice guidance enabled = " << is_voice_guidance_enabled;
    GetAccessibility()->SetVoiceGuidanceEnabled(is_voice_guidance_enabled);
  };

  void OnHighContrastChanged(const StateChangedInfo& info) {
    bool is_highcontrast_enabled = info.Enabled.Value();
    SB_LOG(INFO) << "User setting changed. highcontrast enabled = " << is_highcontrast_enabled;
    GetAccessibility()->SetHighContrastEnabled(is_highcontrast_enabled);
  };

  void OnCaptionsChanged(const StateChangedInfo& info) {
    bool is_cc_enabled = info.Enabled.Value();
    SB_LOG(INFO) << "User setting changed. cc enabled = " << is_cc_enabled;
    GetAccessibility()->SetCaptionEnabled(is_cc_enabled);
  };

  bool RefreshSubscriptions() {
    using NotificationHandlerCb = void(UserSettingsImpl::*)(const StateChangedInfo&);

    const std::map<std::string, NotificationHandlerCb> kNotificationHandlers = {
      {"onVoiceGuidanceChanged", &UserSettingsImpl::OnVoiceGuidanceChanged },
      {"onHighContrastChanged", &UserSettingsImpl::OnHighContrastChanged },
      {"onCaptionsChanged", &UserSettingsImpl::OnCaptionsChanged },
    };

    uint32_t rc;

    for (const auto& kv : kNotificationHandlers) {
      const auto& name = kv.first;
      const auto& callback = kv.second;

      if (std::find(subscriptions_.begin(), subscriptions_.end(), name)
          != subscriptions_.end())
        continue;

      rc = link_.Subscribe<StateChangedInfo>(
        kDefaultTimeoutMs, name, callback, this);

      if (Core::ERROR_UNAVAILABLE == rc || kPriviligedRequestErrorCode == rc) {
        needs_refresh_ = false;
        SB_LOG(ERROR) << "Failed to subscribe to '" << kUserSetingsCallsign
                      << "." << name << "' event, rc = " << rc
                      << " ( " << Core::ErrorToString(rc) << " )";
        return false;
      }

      if (Core::ERROR_NONE != rc && Core::ERROR_DUPLICATE_KEY != rc) {
        SB_LOG(ERROR) << "Failed to subscribe to '" << kUserSetingsCallsign
                      << "." << name << "' event, rc = " << rc
                      << " ( " << Core::ErrorToString(rc) << " ).";
        continue;
      }

      subscriptions_.push_back(name);
    }

    return true;
  }

public:
  UserSettingsImpl() = default;

  void Refresh() {
    std::lock_guard lock(mutex_);
    if (!needs_refresh_)
      return;

    if (!RefreshSubscriptions())
      return;

    bool is_voice_guidance_enabled = false;
    bool is_cc_enabled = false;
    bool is_highcontrast_enabled = false;

    const std::map<std::string, bool*> kSettings = {
      {"getVoiceGuidance", &is_voice_guidance_enabled},
      {"getHighContrast", &is_highcontrast_enabled},
      {"getCaptions", &is_cc_enabled},
    };

    for (const auto& kv: kSettings) {
      uint32_t rc;
      Core::JSON::Boolean result;

      rc = link_.Get(kDefaultTimeoutMs, kv.first, result);

      if (Core::ERROR_NONE == rc) {
        SB_LOG(INFO) << "User setting: " << kv.first << " = " << result.Value();
        (*kv.second) = result.Value();
      } else {
        SB_LOG(ERROR) << "Failed to get user setting '" << kv.first<< ", rc=" << rc
                      << " ( " << Core::ErrorToString(rc) << " ).";
      }
    }

    needs_refresh_ = false;

    auto *accessibility = GetAccessibility();
    accessibility->SetVoiceGuidanceEnabled(is_voice_guidance_enabled, false);
    accessibility->SetCaptionEnabled(is_cc_enabled, false);
    accessibility->SetHighContrastEnabled(is_highcontrast_enabled, false);
  }

  void Teardown() {
    std::lock_guard lock(mutex_);
    for (const auto& subscription : subscriptions_)
      link_.Unsubscribe(kDefaultTimeoutMs, subscription.c_str());
    link_.Teardown();
    needs_refresh_ = true;
    subscriptions_.clear();
  }

  bool GetPresentationLanguage(std::string& out) {
    Core::JSON::String result;
    uint32_t rc = link_.Get(kDefaultTimeoutMs, "getPresentationLanguage", result);
    if (Core::ERROR_NONE != rc) {
      SB_LOG(ERROR) << "UserSettings.getPresentationLanguage failed, rc=" << rc;
      return false;
    }
    out = result.Value();
    return !out.empty();
  }
};

SB_ONCE_INITIALIZE_FUNCTION(UserSettingsImpl, GetUserSettings);

}  // namespace

ResolutionInfo DisplayInfo::GetResolution() {
  return GetDisplayInfo()->GetResolution();
}

float DisplayInfo::GetDiagonalSizeInInches() {
  return GetDisplayInfo()->GetDiagonalSizeInInches();
}

uint32_t DisplayInfo::GetHDRCaps() {
  return GetDisplayInfo()->GetHDRCaps();
}

std::string DeviceIdentification::GetChipset() {
  return GetDeviceIdImpl()->chipset;
}

std::string DeviceIdentification::GetFirmwareVersion() {
  return GetDeviceIdImpl()->firmware_version;
}

void NetworkInfo::Initialize() {
  GetNetworkInfo()->Initialize();
}

bool NetworkInfo::IsConnectionTypeWireless() {
  return GetNetworkInfo()->IsConnectionTypeWireless();
}

bool NetworkInfo::IsDisconnected() {
  return GetNetworkInfo()->IsDisconnected();
}

void TextToSpeech::Speak(const std::string& text) {
  GetTextToSpeech()->Speak(text);
}

bool TextToSpeech::IsEnabled() {
  GetUserSettings()->Refresh();
  return GetTextToSpeech()->IsEnabled();
}

void TextToSpeech::Cancel() {
  GetTextToSpeech()->Cancel();
}

bool Accessibility::GetCaptionSettings(SbAccessibilityCaptionSettings* out) {
  GetUserSettings()->Refresh();
  return GetAccessibility()->GetCaptionSettings(out);
}

bool Accessibility::GetDisplaySettings(SbAccessibilityDisplaySettings* out) {
  GetUserSettings()->Refresh();
  return GetAccessibility()->GetDisplaySettings(out);
}

void Accessibility::SetSettings(const std::string& json) {
  GetAccessibility()->SetSettings(json);
}

bool Accessibility::GetSettings(std::string& out_json) {
  return GetAccessibility()->GetSettings(out_json);
}

void AdvertisingId::SetSettings(const std::string& json) {
  GetAdvertisingProperties()->SetSettings(json);
}

bool AdvertisingId::GetSettings(std::string& out_json) {
  return GetAdvertisingProperties()->GetSettings(out_json);
}

bool AdvertisingId::GetIfa(std::string& out_json) {
  return GetAdvertisingProperties()->GetIfa(out_json);
}

bool AdvertisingId::GetIfaType(std::string& out_json) {
  return GetAdvertisingProperties()->GetIfaType(out_json);
}

bool AdvertisingId::GetLmtAdTracking(std::string& out_json) {
  return GetAdvertisingProperties()->GetLmtAdTracking(out_json);
}

void SystemProperties::SetSettings(const std::string& json) {
  GetSystemProperties()->SetSettings(json);
}

bool SystemProperties::GetSettings(std::string& out_json) {
  return GetSystemProperties()->GetSettings(out_json);
}

bool SystemProperties::GetChipset(std::string &out) {
  return GetSystemProperties()->GetChipset(out);
}

bool SystemProperties::GetFirmwareVersion(std::string &out) {
  return GetSystemProperties()->GetFirmwareVersion(out);
}

bool SystemProperties::GetIntegratorName(std::string &out) {
  return GetSystemProperties()->GetIntegratorName(out);
}

bool SystemProperties::GetBrandName(std::string &out) {
  return GetSystemProperties()->GetBrandName(out);
}

bool SystemProperties::GetModelName(std::string &out) {
  return GetSystemProperties()->GetModelName(out);
}

bool SystemProperties::GetModelYear(std::string &out) {
  return GetSystemProperties()->GetModelYear(out);
}

bool SystemProperties::GetFriendlyName(std::string &out) {
  return GetSystemProperties()->GetFriendlyName(out);
}

bool SystemProperties::GetDeviceType(std::string &out) {
  return GetSystemProperties()->GetDeviceType(out);
}

bool AuthService::IsAvailable() {
  return GetAuthService()->IsAvailable();
}

bool AuthService::GetExperience(std::string &out) {
  return GetAuthService()->GetExperience(out);
}

bool DeviceInfo::GetAudioConfiguration(int index, SbMediaAudioConfiguration* out_audio_configuration) {
  return GetDeviceInfo()->GetAudioConfiguration(index, out_audio_configuration);
}

bool DeviceInfo::GetBrandName(std::string& out) {
  return GetDeviceInfo()->GetBrandName(out);
}

bool UserSettings::GetPresentationLanguage(std::string& out) {
  return GetUserSettings()->GetPresentationLanguage(out);
}

void TeardownJSONRPCLink() {
  GetDisplayInfo()->Teardown();
  GetTextToSpeech()->Teardown();
  GetNetworkInfo()->Teardown();
  GetDeviceInfo()->Teardown();
  GetUserSettings()->Teardown();
}

}  // namespace starboard
