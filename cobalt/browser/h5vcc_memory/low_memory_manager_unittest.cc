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

#include "cobalt/browser/h5vcc_memory/low_memory_manager.h"

#include <memory>

#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cobalt {
namespace browser {

namespace {

class FakeLowMemoryListener : public h5vcc_memory::mojom::LowMemoryListener {
 public:
  FakeLowMemoryListener() : receiver_(this) {}

  mojo::PendingRemote<h5vcc_memory::mojom::LowMemoryListener>
  BindNewPipeAndPassRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void OnLowMemory() override {
    notifications_count_++;
    if (quit_closure_) {
      std::move(quit_closure_).Run();
    }
  }

  void WaitForNotification() {
    if (notifications_count_ > 0) {
      return;
    }
    base::RunLoop run_loop;
    quit_closure_ = run_loop.QuitClosure();
    run_loop.Run();
  }

  int notifications_count() const { return notifications_count_; }

 private:
  mojo::Receiver<h5vcc_memory::mojom::LowMemoryListener> receiver_;
  int notifications_count_ = 0;
  base::OnceClosure quit_closure_;
};

}  // namespace

TEST(LowMemoryManagerTest, GetInstanceReturnsNonNull) {
  LowMemoryManager* instance = LowMemoryManager::GetInstance();
  ASSERT_NE(nullptr, instance);
}

TEST(LowMemoryManagerTest, BroadcastsLowMemoryToListener) {
  base::test::SingleThreadTaskEnvironment task_environment;
  auto* manager = LowMemoryManager::GetInstance();

  FakeLowMemoryListener listener;
  manager->AddListener(listener.BindNewPipeAndPassRemote());

  manager->OnLowMemory();
  listener.WaitForNotification();

  EXPECT_EQ(1, listener.notifications_count());
}

TEST(LowMemoryManagerTest, InvokesListenerAddedCallback) {
  base::test::SingleThreadTaskEnvironment task_environment;
  auto* manager = LowMemoryManager::GetInstance();

  base::RunLoop run_loop;
  manager->SetOnListenerAddedCallbackForTesting(run_loop.QuitClosure());

  FakeLowMemoryListener listener;
  manager->AddListener(listener.BindNewPipeAndPassRemote());
  run_loop.Run();

  EXPECT_GE(manager->num_listeners(), 1u);
  manager->SetOnListenerAddedCallbackForTesting(base::RepeatingClosure());
}

}  // namespace browser
}  // namespace cobalt
