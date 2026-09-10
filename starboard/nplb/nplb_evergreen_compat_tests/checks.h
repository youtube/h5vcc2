// Copyright 2020 The Cobalt Authors. All Rights Reserved.
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

#ifndef STARBOARD_NPLB_NPLB_EVERGREEN_COMPAT_TESTS_CHECKS_H_
#define STARBOARD_NPLB_NPLB_EVERGREEN_COMPAT_TESTS_CHECKS_H_

#include "starboard/configuration.h"

#if BUILDFLAG(IS_STARBOARD)
#if SB_API_VERSION < 16
#if !SB_CAN(MAP_EXECUTABLE_MEMORY)
#error "Evergreen requires executable memory support!"
#endif
#endif  // SB_API_VERSION < 16
#endif  // BUILDFLAG(IS_STARBOARD)

#endif  // STARBOARD_NPLB_NPLB_EVERGREEN_COMPAT_TESTS_CHECKS_H_
