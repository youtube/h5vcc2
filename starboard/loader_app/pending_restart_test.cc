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

#include "starboard/loader_app/pending_restart.h"

#include "starboard/configuration.h"
#include "starboard/loader_app/installation_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_STARBOARD)
namespace loader_app {
namespace {

class PendingRestartTest : public testing::Test {
 protected:
  void SetUp() override { ImInitialize(3, "app_key"); }

  void TearDown() override { ImUninitialize(); }
};

TEST_F(PendingRestartTest, PendingRestartIfNeeded) {
  ASSERT_FALSE(IsPendingRestart());
  ImRequestRollForwardToInstallation(1);
  ASSERT_TRUE(IsPendingRestart());
  ImRollForwardIfNeeded();
  ASSERT_FALSE(IsPendingRestart());
}

TEST_F(PendingRestartTest, PendingRestart) {
  ASSERT_FALSE(IsPendingRestart());
  ImRequestRollForwardToInstallation(1);
  ASSERT_TRUE(IsPendingRestart());
  ImRollForward(2);
  ASSERT_FALSE(IsPendingRestart());
}

}  // namespace
}  // namespace loader_app
#endif  // BUILDFLAG(IS_STARBOARD)
