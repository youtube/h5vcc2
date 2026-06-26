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

#ifndef STARBOARD_TESTING_TEST_RUNNER_H_
#define STARBOARD_TESTING_TEST_RUNNER_H_

#include <functional>

namespace starboard {

// Executes `action`. On platforms requiring main loop pumping (e.g. tvOS),
// dispatches `action` to a background thread while pumping the main loop.
void RunTestBlockingAction(std::function<void()>&& action);

// Registers global test environments required by the platform (e.g. Android /
// tvOS).
void RegisterPlatformTestEnvironments(int argc, char** argv);

// Runs the platform test suite loop. On platforms requiring a custom app loop
// launcher (e.g. tvOS UIApplicationMain), executes via the platform runner
// hook.
int RunPlatformTestSuite(int argc,
                         char** argv,
                         std::function<int(int, char**)> run_tests_fn);

}  // namespace starboard

#endif  // STARBOARD_TESTING_TEST_RUNNER_H_
