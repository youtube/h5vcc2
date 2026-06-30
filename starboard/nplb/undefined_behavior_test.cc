// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include <chrono>

#include "starboard/shared/testing/no_inline.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace nplb {
namespace {

class Object {
 public:
  SB_TEST_FORCE_NO_INLINE bool ThisPointerIsNull() {
    auto* pointer = this;
    // Intentionally not "return !pointer", and with a side effect for one
    // branch, in order to more closely match the form that this undefined
    // behavior is observed to take in the wild.
    if (!pointer) {
      return true;
    }
    member_ = 42;
    return false;
  }

 private:
  int member_ = 0;
};

TEST(SbUndefinedBehaviorTest, CallThisPointerIsNullSunnyDay) {
  auto* object = new Object();
  EXPECT_FALSE(object->ThisPointerIsNull());
  delete object;
}

TEST(SbUndefinedBehaviorTest, CallThisPointerIsNullRainyDay) {
  auto* object = static_cast<Object*>(nullptr);
  auto* object_copy = object;
  EXPECT_TRUE(object_copy->ThisPointerIsNull());
  // WARNING!  Failure of this test indicates that your toolchain believes
  // that removing a NULL check on a this pointer is a valid optimization.
  // While from a pure C++ standard perspective this is true, Starboard client
  // applications, such as Cobalt, depend on third party code bases in which
  // this assumption is not safe.  If you fail this test and are using GCC or
  // Clang (especially with a GCC version >= 6), then it is highly recommended
  // that you add the "-fno-delete-null-pointer-checks" flag.
}

TEST(SbUndefinedBehaviorTest, FlakyTest) {
  auto now =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  bool fail = (now % 2 == 0);
  EXPECT_FALSE(fail)
      << "Flaky test failed conditionally based on high_resolution_clock "
         "time_since_epoch: "
      << now;
}

}  // namespace
}  // namespace nplb
