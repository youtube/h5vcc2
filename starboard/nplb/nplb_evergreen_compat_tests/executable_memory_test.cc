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

#include <sys/mman.h>

#include "starboard/common/log.h"
#include "starboard/configuration_constants.h"
#include "starboard/nplb/nplb_evergreen_compat_tests/checks.h"
#include "testing/gtest/include/gtest/gtest.h"

#if !BUILDFLAG(IS_STARBOARD)
#error These tests apply only to Starboard platforms.
#endif

namespace nplb {

namespace {

const size_t kSize = 30 * 1024 * 1024;
const size_t kSmallerSize = 15 * 1024 * 1024;

class ExecutableMemoryTest : public ::testing::Test {
 protected:
  ExecutableMemoryTest() {
    SB_DCHECK(kSbCanMapExecutableMemory)
        << "Evergreen requires executable memory support!";
  }
  ~ExecutableMemoryTest() {}
};

TEST_F(ExecutableMemoryTest, VerifyMemoryProtection) {
  void* memory =
      mmap(nullptr, kSize, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  ASSERT_NE(MAP_FAILED, memory);
  memset(memory, 0, kSize);
  ASSERT_EQ(mprotect(memory, kSmallerSize, PROT_READ | PROT_WRITE), 0);
  munmap(memory, kSize);
  EXPECT_TRUE(true);
}

}  // namespace
}  // namespace nplb
