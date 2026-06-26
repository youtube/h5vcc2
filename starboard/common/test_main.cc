// Copyright 2015 The Cobalt Authors. All Rights Reserved.
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

#include "build/build_config.h"
#include "starboard/client_porting/wrap_main/wrap_main.h"
#include "starboard/configuration.h"
#include "starboard/event.h"
#include "starboard/system.h"
#include "starboard/testing/test_runner.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

int RunTests(int argc, char** argv) {
  starboard::RegisterPlatformTestEnvironments(argc, argv);
  return RUN_ALL_TESTS();
}

int InitAndRunAllTests(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return starboard::RunPlatformTestSuite(argc, argv, &RunTests);
}
}  // namespace

#if BUILDFLAG(IS_STARBOARD)
// For the Starboard OS define SbEventHandle as the entry point
SB_EXPORT STARBOARD_WRAP_SIMPLE_MAIN(InitAndRunAllTests)

#if !SB_IS(EVERGREEN)
// Define main() for non-Evergreen Starboard OS.
int main(int argc, char** argv) {
  return SbRunStarboardMain(argc, argv, SbEventHandle);
}
#endif  // !SB_IS(EVERGREEN)
#else   // BUILDFLAG(IS_STARBOARD)
// If the OS is not Starboard use the regular main e.g. ATV.
int main(int argc, char** argv) {
  return InitAndRunAllTests(argc, argv);
}
#endif  // BUILDFLAG(IS_STARBOARD)
