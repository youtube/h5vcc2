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

#ifndef COBALT_COMMON_ICU_INIT_INIT_H_
#define COBALT_COMMON_ICU_INIT_INIT_H_

// These macro definitions are cloned natively from Chromium's
// base/i18n/icu_util.h. They are strictly required because the C-preprocessor
// silently evaluates any undefined macros as 0. Without these explicitly
// defined, the architectural compiler directives inside init.cc evaluating
// ICU_UTIL_DATA_IMPL will silently collapse into `#if (0 == 0)` and physically
// strip the file loader from decoupled binaries.
#ifndef ICU_UTIL_DATA_FILE
#define ICU_UTIL_DATA_FILE 0
#endif
#ifndef ICU_UTIL_DATA_STATIC
#define ICU_UTIL_DATA_STATIC 1
#endif

namespace cobalt {
namespace common {
namespace icu_init {

// This will ensure that ICU is initialized. This only needs to be called
// from functions that may be called from static initializers.
void EnsureInitialized();

}  // namespace icu_init
}  // namespace common
}  // namespace cobalt

#endif  // COBALT_COMMON_ICU_INIT_INIT_H_
