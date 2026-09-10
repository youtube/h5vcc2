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

#include <utility>

#include "base/check.h"
#include "base/no_destructor.h"

namespace cobalt {
namespace browser {

LowMemoryManager::LowMemoryManager() {
  COBALT_DETACH_FROM_THREAD(thread_checker_);
}

LowMemoryManager::~LowMemoryManager() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

// static
LowMemoryManager* LowMemoryManager::GetInstance() {
  static base::NoDestructor<LowMemoryManager> instance;
  return instance.get();
}

void LowMemoryManager::AddListener(
    mojo::PendingRemote<LowMemoryListener> listener) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  listeners_.Add(std::move(listener));
  if (on_listener_added_callback_) {
    on_listener_added_callback_.Run();
  }
}

void LowMemoryManager::OnLowMemory() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  for (auto& listener : listeners_) {
    listener->OnLowMemory();
  }
}

size_t LowMemoryManager::num_listeners() const {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return listeners_.size();
}

void LowMemoryManager::SetOnListenerAddedCallbackForTesting(
    base::RepeatingClosure callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  on_listener_added_callback_ = std::move(callback);
}

}  // namespace browser
}  // namespace cobalt
