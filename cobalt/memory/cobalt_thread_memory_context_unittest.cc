// Copyright 2026 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <atomic>

#include "starboard/common/thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cobalt {
namespace memory {

using base::memory::GetCurrentMemoryContext;
using base::memory::MemoryContext;

class TestThread : public starboard::Thread {
 public:
  TestThread(std::string_view name, const starboard::ThreadOptions& options)
      : starboard::Thread(name, options) {}

  void Run() override {
    context_in_thread_ = GetCurrentMemoryContext();
    executed_.store(true);
  }

  MemoryContext context_in_thread() const { return context_in_thread_; }
  bool executed() const { return executed_.load(); }

 private:
  MemoryContext context_in_thread_ = MemoryContext::kUnknown;
  std::atomic_bool executed_{false};
};

TEST(CobaltThreadMemoryContextTest, StarboardThreadPropagatesContext) {
  starboard::ThreadOptions options;
  options.SetMemoryContext(MemoryContext::kMedia);

  TestThread thread("TestThread", options);
  thread.Start();
  thread.Join();

  EXPECT_TRUE(thread.executed());
  EXPECT_EQ(thread.context_in_thread(), MemoryContext::kMedia);
}

TEST(CobaltThreadMemoryContextTest, StarboardThreadDefaultContextIsUnknown) {
  starboard::ThreadOptions options;
  // No context set, should be kUnknown

  TestThread thread("TestThread", options);
  thread.Start();
  thread.Join();

  EXPECT_TRUE(thread.executed());
  EXPECT_EQ(thread.context_in_thread(), MemoryContext::kUnknown);
}

}  // namespace memory
}  // namespace cobalt
