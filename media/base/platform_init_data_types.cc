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

#include "media/base/platform_init_data_types.h"

#include "base/check.h"
#include "base/no_destructor.h"

namespace media {

namespace {

std::string& StoredString() {
  static base::NoDestructor<std::string> s;
  return *s;
}

}  // namespace

void SetPlatformDrmInitDataTypeString(const std::string& type_string) {
  std::string& stored = StoredString();
  DCHECK(stored.empty());
  stored = type_string;
}

const std::string& GetPlatformDrmInitDataTypeString() {
  return StoredString();
}

}  // namespace media
