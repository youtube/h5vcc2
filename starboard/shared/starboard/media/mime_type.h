// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_SHARED_STARBOARD_MEDIA_MIME_TYPE_H_
#define STARBOARD_SHARED_STARBOARD_MEDIA_MIME_TYPE_H_

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "starboard/shared/internal_only.h"

namespace starboard {

// This class can be used to parse a content type for media in the form of
// "type/subtype; param1=value1; param2="value2".  For example, the content type
// "video/webm; codecs="vp8, vp9"; width=1920; height=1080; framerate=59.96"
// will be parsed into:
//   type: video
//   subtype: webm
//   codecs: vp8, vp9
//   width: 1920
//   height: 1080
//   framerate: 59.96
//
// Note that "codecs" is a special case because:
// 1. It has to be the first parameter when available.
// 2. It may contain multiple values separated by comma.
//
// The following are the restrictions on the components:
// 1. Type/subtype has to be in the very beginning.
// 2. String values may be double quoted.
// 3. Numeric values cannot be double quoted.
class MimeType {
 public:
  enum ParamType {
    kParamTypeInteger,
    kParamTypeFloat,
    kParamTypeString,
    kParamTypeBoolean,
  };

  struct Param {
    // General MIME parameter.
    static constexpr char kCodecs[] = "codecs";

    // Format capability MIME parameters defined in:
    // http://google3/video/youtube/web/player/formats/capability.ts;l=9-94;rcl=770319862
    static constexpr char kWidth[] = "width";
    static constexpr char kHeight[] = "height";
    static constexpr char kFramerate[] = "framerate";
    static constexpr char kBitrate[] = "bitrate";
    static constexpr char kEotf[] = "eotf";
    static constexpr char kChannels[] = "channels";
    static constexpr char kCryptoblockformat[] = "cryptoblockformat";
    static constexpr char kDecodeToTexture[] = "decode-to-texture";
    static constexpr char kExperimental[] = "experimental";
    static constexpr char kTunnelMode[] = "tunnelmode";

    // Cobalt-specific playback experiment MIME parameters defined in:
    // http://google3/video/youtube/web/player/dash/manifest.ts;l=1112-1125;rcl=954649899
    static constexpr char kEnableFlushDuringSeek[] = "enableflushduringseek";
    static constexpr char kEnableResetAudioDecoder[] =
        "enableresetaudiodecoder";

    ParamType type;
    std::string name;
    std::string string_value;
    union {
      int int_value;
      float float_value;
      bool bool_value;
    };
  };

  static const int kInvalidParamIndex;

  // Expose the function as a helper function to parse a mime attribute.
  static bool ParseParamString(const std::string& param_string, Param* param);

  static std::optional<MimeType> Create(const std::string& content_type);

  const std::string& type() const { return type_; }
  const std::string& subtype() const { return subtype_; }
  const std::vector<std::string>& GetCodecs() const { return codecs_; }

  int GetParamCount() const;
  ParamType GetParamType(int index) const;
  const std::string& GetParamName(int index) const;
  // GetParamIndexByName() will return |kInvalidParamIndex| if the param name is
  // not found.
  int GetParamIndexByName(const char* name) const;

  int GetParamIntValue(int index) const;
  float GetParamFloatValue(int index) const;
  const std::string& GetParamStringValue(int index) const;
  bool GetParamBoolValue(int index) const;

  int GetParamIntValue(const char* name, int default_value) const;
  float GetParamFloatValue(const char* name, float default_value) const;
  const std::string& GetParamStringValue(
      const char* name,
      const std::string& default_value) const;
  bool GetParamBoolValue(const char* name, bool default_value) const;

  // Validate functions will return true if the param contains a valid value or
  // if param name is not found.
  bool ValidateIntParameter(const char* name) const;
  bool ValidateFloatParameter(const char* name) const;
  // Allows passing a pattern on the format "value_1|...|value_n"
  // where the parameter value must match one of the values in the pattern in
  // order to be considered valid.
  bool ValidateStringParameter(const char* name,
                               const std::string& pattern = "") const;
  bool ValidateBoolParameter(const char* name) const;

  friend std::ostream& operator<<(std::ostream& os, const MimeType& mime_type);

 private:
  // Use std::vector as the number of components are usually small and we'd like
  // to keep the order of components.
  using Params = std::vector<Param>;

  MimeType(std::string type,
           std::string subtype,
           std::vector<std::string> codecs,
           Params params);

  const std::string type_;
  const std::string subtype_;
  const std::vector<std::string> codecs_;
  const Params params_;
};

}  // namespace starboard

#endif  // STARBOARD_SHARED_STARBOARD_MEDIA_MIME_TYPE_H_
