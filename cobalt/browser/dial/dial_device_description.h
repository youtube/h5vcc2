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

#ifndef COBALT_BROWSER_DIAL_DIAL_DEVICE_DESCRIPTION_H_
#define COBALT_BROWSER_DIAL_DIAL_DEVICE_DESCRIPTION_H_

#include <string>

namespace in_app_dial {

// This class contains device-specific information that is used to implement
// parts of the DIAL spec (M-SEARCH responses and /dd.xml queries).
//
// It performs blocking operations and must be instantiated and used from an I/O
// thread.
class DialDeviceDescription final {
 public:
  DialDeviceDescription();
  ~DialDeviceDescription();

  const std::string& formatted_uuid() const { return formatted_uuid_; }

  const std::string& AsXml() const { return dd_xml_; }

 private:
  friend class DialHttpServerTest;

  DialDeviceDescription(std::string_view uuid,
                        std::string_view friendly_name,
                        std::string_view manufacturer_name,
                        std::string_view model_name);

  const std::string formatted_uuid_;

  const std::string dd_xml_;
};

}  // namespace in_app_dial

#endif  // COBALT_BROWSER_DIAL_DIAL_DEVICE_DESCRIPTION_H_
