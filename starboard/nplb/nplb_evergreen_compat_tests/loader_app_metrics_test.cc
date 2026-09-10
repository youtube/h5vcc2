// Copyright 2024 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/extension/loader_app_metrics.h"

#include "starboard/nplb/nplb_evergreen_compat_tests/checks.h"
#include "starboard/system.h"
#include "testing/gtest/include/gtest/gtest.h"

#if !BUILDFLAG(IS_STARBOARD)
#error These tests apply only to Starboard platforms.
#endif

namespace nplb {

namespace {

TEST(LoaderAppMetricsTest, VerifyLoaderAppMetricsExtension) {
  auto extension = static_cast<const StarboardExtensionLoaderAppMetricsApi*>(
      SbSystemGetExtension(kStarboardExtensionLoaderAppMetricsName));

  ASSERT_TRUE(extension != nullptr)
      << "Please update your platform's SbSystemGetExtension() implementation "
      << "to return the shared instance of this extension. The linux reference "
      << "platform can be used as an example.";

  // Since Evergreen-compatible platforms are expected to just wire up the
  // shared implementation of the extension, it's reasonable to expect them to
  // be on the latest version of the extension API available at the commit from
  // which this test is built.
  ASSERT_EQ(extension->version, 3u);
}

}  // namespace
}  // namespace nplb
