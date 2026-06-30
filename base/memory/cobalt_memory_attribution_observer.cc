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

#include "base/memory/cobalt_memory_attribution_observer.h"

#include "base/compiler_specific.h"
#if defined(OS_LINUX) || defined(OS_ANDROID)
#include <sys/prctl.h>
#endif
#include <unistd.h>


#include "base/logging.h"

#include "base/no_destructor.h"
#include "base/threading/platform_thread.h"


namespace base {
namespace memory {


// static
CobaltMemoryAttributionObserver* CobaltMemoryAttributionObserver::Get() {
  static base::NoDestructor<CobaltMemoryAttributionObserver> observer;
  return observer.get();
}

CobaltMemoryAttributionObserver::CobaltMemoryAttributionObserver() {
  for (size_t i = 0; i < static_cast<size_t>(MemoryContext::kCount); ++i) {
    UNSAFE_BUFFERS(counters_[i].value.store(0, std::memory_order_relaxed));
  }
  // Ensure ThreadLocalStorage is fully initialized and warmed up before the
  // observer hooks into Dispatcher, preventing reentrancy crashes during the
  // first allocation.
  GetCurrentMemoryContext();
}

void CobaltMemoryAttributionObserver::OnAllocation(
    const base::allocator::dispatcher::AllocationNotificationData& notification_data) {

  MemoryContext current_context = GetCurrentMemoryContext();
  if (current_context >= MemoryContext::kCount) {
    current_context = MemoryContext::kUnknown;
  }
  UNSAFE_BUFFERS(counters_[static_cast<size_t>(current_context)].value.fetch_add(
      notification_data.size(), std::memory_order_relaxed));
}

}  // namespace memory
}  // namespace base
