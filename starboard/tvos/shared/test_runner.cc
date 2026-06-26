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
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/test/test_support_ios.h"
#include "starboard/tvos/shared/run_in_background_thread_and_wait.h"
#include "starboard/tvos/shared/starboard_test_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {

void RunTestBlockingAction(std::function<void()> action) {
  RunInBackgroundThreadAndWait(std::move(action));
}

void RegisterPlatformTestEnvironments(int argc, char** argv) {
  ::testing::AddGlobalTestEnvironment(
      std::make_unique<StarboardTestEnvironment>(argc, argv).release());
}

namespace {
int RunTestsCallbackAdapter(RunTestsCallback run_tests_cb,
                            int argc,
                            char** argv) {
  return run_tests_cb(argc, argv);
}
}  // namespace

int RunPlatformTestSuite(int argc, char** argv, RunTestsCallback run_tests_cb) {
  CHECK(base::CommandLine::InitializedForCurrentProcess() ||
        base::CommandLine::Init(argc, argv));
  base::InitIOSRunHook(
      base::BindOnce(&RunTestsCallbackAdapter, run_tests_cb, argc, argv));
  return base::RunTestsFromIOSApp();
}

}  // namespace starboard
