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

// clang-format off
#include "starboard/system.h"
// clang-format on

#include <locale.h>  // for setlocale and LC_*
#include <stdlib.h>  // for getenv

#include "starboard/common/string.h"

namespace {
bool IsValidId(const char* posix_id) {
  return !((posix_id == NULL) || (strncmp("C", posix_id, 1) == 0) ||
           (strncmp("POSIX", posix_id, 5) == 0));
}
}  // namespace

const char* SbSystemGetLocaleId() {
  // Adapted from ICU's putil.c:uprv_getPOSIXIDForCategory.
  const char* posix_id = setlocale(LC_MESSAGES, NULL);
  if (!IsValidId(posix_id)) {
    posix_id = getenv("LC_ALL");
    if (posix_id == NULL) {
      posix_id = getenv("LC_MESSAGES");
      if (posix_id == NULL) {
        posix_id = getenv("LANG");
      }
    }
  }

  // Contract requires non-null (see system_get_locale_id_test.cc); callers
  // crash on null (e.g. setenv() in environment.cc).
  return posix_id ? posix_id : "";
}
