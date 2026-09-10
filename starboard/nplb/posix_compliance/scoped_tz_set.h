// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_NPLB_POSIX_COMPLIANCE_SCOPED_TZ_SET_H_
#define STARBOARD_NPLB_POSIX_COMPLIANCE_SCOPED_TZ_SET_H_

#include <time.h>

#include "starboard/nplb/posix_compliance/posix_compliance_icu.h"
#include "starboard/nplb/posix_compliance/scoped_tz_environment.h"

namespace nplb {

// Helper class to manage the TZ environment variable for test isolation.
// Sets TZ in constructor, restores original TZ in destructor.
// Calls tzset() after each change to TZ.
class ScopedTzSet : public ScopedTzEnvironment {
 public:
  explicit ScopedTzSet(const char* new_tz_value)
      : ScopedTzEnvironment(new_tz_value) {
    starboard::nplb::InitializePosixIcuOnce();
    tzset();
  }

  ~ScopedTzSet() { tzset(); }
};

}  // namespace nplb

#endif  // STARBOARD_NPLB_POSIX_COMPLIANCE_SCOPED_TZ_SET_H_
