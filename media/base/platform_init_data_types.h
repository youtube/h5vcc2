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

#ifndef MEDIA_BASE_PLATFORM_INIT_DATA_TYPES_H_
#define MEDIA_BASE_PLATFORM_INIT_DATA_TYPES_H_

#include <string>

#include "media/base/media_export.h"

namespace media {

// Platform-specific DRM init data type string for PLATFORM_DRM.
// Internal code sets the string at startup; public code only reads it.
MEDIA_EXPORT void SetPlatformDrmInitDataTypeString(
    const std::string& type_string);
MEDIA_EXPORT const std::string& GetPlatformDrmInitDataTypeString();

}  // namespace media

#endif  // MEDIA_BASE_PLATFORM_INIT_DATA_TYPES_H_
