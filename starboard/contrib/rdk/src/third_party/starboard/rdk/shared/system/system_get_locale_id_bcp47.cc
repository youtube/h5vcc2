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

#include "starboard/system.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "starboard/common/log.h"
#include "third_party/starboard/rdk/shared/rdkservices.h"

namespace {

bool IsValidLocaleIdBcp47(const char* id) {
  if (!id || *id == '\0') {
    return false;
  }
  return strcmp(id, "C") != 0 && strcmp(id, "POSIX") != 0;
}

// Normalise a POSIX locale string to a BCP-47 language tag in place.
// e.g. "fr_FR.UTF-8" → "fr-FR", "en_US@euro" → "en-US".
void NormalizePosixLocaleToBcp47(std::string& locale) {
  size_t at = locale.find('@');
  if (at != std::string::npos) {
    locale.resize(at);
  }
  size_t dot = locale.find('.');
  if (dot != std::string::npos) {
    locale.resize(dot);
  }
  for (char& c : locale) {
    if (c == '_') {
      c = '-';
    }
  }
}

}  // namespace

// Cache the BCP-47 locale string so SbSystemGetLocaleId() returns a stable
// pointer. An empty string is returned when nothing is found to honor the
// non-null contract (callers like setenv() in environment.cc crash on null).
// Thread-safe initialization (and avoidance of fixed-size buffer truncation)
// is guaranteed by C++11 magic statics.
const char* SbSystemGetLocaleId() {
  static const std::string cached_locale = []() -> std::string {
    // 0. Primary source: the device's durable presentation language setting,
    //    fetched from UserSettings over Thunder JSON-RPC. Read here (rather
    //    than by the plugin wrapper) so SbSystemGetLocaleId() stays the
    //    single, self-contained owner of locale resolution on RDK.
    std::string presentation_language;
    if (starboard::UserSettings::GetPresentationLanguage(
            presentation_language) &&
        IsValidLocaleIdBcp47(presentation_language.c_str())) {
      NormalizePosixLocaleToBcp47(presentation_language);
      return presentation_language;
    }

    // 1. Check setlocale, then $LC_ALL / $LC_MESSAGES / $LANG, in priority
    //    order. setlocale() reflects any explicit setlocale(LC_ALL, "") call
    //    made earlier during startup, which may differ from the raw env vars.
    const char* candidates[] = {
      setlocale(LC_MESSAGES, NULL),
      getenv("LC_ALL"),
      getenv("LC_MESSAGES"),
      getenv("LANG"),
    };
    for (const char* id : candidates) {
      if (IsValidLocaleIdBcp47(id)) {
        std::string locale(id);
        NormalizePosixLocaleToBcp47(locale);
        return locale;
      }
    }

    SB_LOG(WARNING) << "SbSystemGetLocaleId: no valid locale found";
    return "";
  }();

  return cached_locale.c_str();
}
