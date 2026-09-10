// Copyright 2021 The Cobalt Authors. All Rights Reserved.
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

#include <sys/stat.h>

#include <string>
#include <vector>

#include "starboard/configuration_constants.h"
#include "starboard/extension/crash_handler.h"
#include "starboard/nplb/nplb_evergreen_compat_tests/checks.h"
#include "starboard/system.h"
#include "testing/gtest/include/gtest/gtest.h"

#if !BUILDFLAG(IS_STARBOARD)
#error These tests apply only to Starboard platforms.
#endif

namespace nplb {

namespace {

// These tests are not applicable to AOSP
#if !defined(ANDROID)

TEST(CrashpadConfigTest, VerifyUploadCert) {
  std::vector<char> buffer(kSbFileMaxPath);
  ASSERT_TRUE(SbSystemGetPath(kSbSystemPathContentDirectory, buffer.data(),
                              buffer.size()));
  ASSERT_LE(kSbFileMaxPath, buffer.size());

  std::string cert_location(buffer.data());
  cert_location.append(std::string(kSbFileSepString) + "app" +
                       kSbFileSepString + "cobalt" + kSbFileSepString +
                       "content" + kSbFileSepString + "ssl" + kSbFileSepString +
                       "certs");

  struct stat info;
  ASSERT_TRUE(stat(cert_location.c_str(), &info) == 0) << cert_location;
}

#endif  // !defined(ANDROID)

TEST(CrashpadConfigTest, VerifyCrashHandlerExtension) {
  auto* crash_handler_extension =
      static_cast<const CobaltExtensionCrashHandlerApi*>(
          SbSystemGetExtension(kCobaltExtensionCrashHandlerName));
  ASSERT_TRUE(crash_handler_extension != nullptr);
  EXPECT_STREQ(crash_handler_extension->name, kCobaltExtensionCrashHandlerName);
  EXPECT_GE(crash_handler_extension->version, 4u);
  EXPECT_TRUE(crash_handler_extension->SetString != nullptr);
  EXPECT_TRUE(crash_handler_extension->RegisterSetStringCallback != nullptr);
  EXPECT_TRUE(crash_handler_extension->DumpWithoutCrashing != nullptr);
  EXPECT_TRUE(crash_handler_extension->RegisterDumpWithoutCrashingCallback !=
              nullptr);
}

}  // namespace
}  // namespace nplb
