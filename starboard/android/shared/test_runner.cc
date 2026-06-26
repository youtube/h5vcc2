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

#include "starboard/testing/test_runner.h"

#include <memory>

#include "starboard/android/shared/starboard_test_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {

void RunTestBlockingAction(std::function<void()>&& action) {
  action();
}

void RegisterPlatformTestEnvironments(int argc, char** argv) {
  ::testing::AddGlobalTestEnvironment(
      std::make_unique<starboard::StarboardTestEnvironment>().release());
}

int RunPlatformTestSuite(int argc,
                         char** argv,
                         std::function<int(int, char**)> run_tests_fn) {
  return run_tests_fn(argc, argv);
}

}  // namespace starboard
