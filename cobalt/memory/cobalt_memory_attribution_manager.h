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

#ifndef COBALT_MEMORY_COBALT_MEMORY_ATTRIBUTION_MANAGER_H_
#define COBALT_MEMORY_COBALT_MEMORY_ATTRIBUTION_MANAGER_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "base/feature_list.h"
#include "base/functional/callback_forward.h"
#include "base/memory/cobalt_memory_context.h"
#include "base/memory/singleton.h"
#include "base/power_monitor/power_observer.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/trace_event/memory_dump_provider.h"

namespace cobalt {
class MemoryAttributionBrowserTest;
namespace memory {

// CobaltMemoryAttributionManager is a singleton that tracks memory allocations
// and attributes them to different subsystems (contexts) within Cobalt.
// It reports these metrics via UMA histograms and exposes them to the tracing
// system.
//
// Lifetime and Ownership: This is a leaky singleton, and its lifetime is the
// duration of the process.
//
// Threading: The manager is created and started on the main thread. The timer
// for reporting UMA also fires on the main thread. The OnMemoryDump callback
// is also called on the main thread. The underlying observer can be called from
// any thread.
class CobaltMemoryAttributionManager
    : public base::trace_event::MemoryDumpProvider,
      public base::PowerSuspendObserver {
 public:
  static CobaltMemoryAttributionManager* Get();

  CobaltMemoryAttributionManager(const CobaltMemoryAttributionManager&) =
      delete;
  CobaltMemoryAttributionManager& operator=(
      const CobaltMemoryAttributionManager&) = delete;

  void Start();
  void Stop();  // For testing
  void RequestReportUmaForTesting(base::OnceClosure callback);

  base::memory::MemoryContext GetCurrentContext() const {
    return base::memory::GetCurrentMemoryContext();
  }

  // base::trace_event::MemoryDumpProvider implementation.
  bool OnMemoryDump(const base::trace_event::MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd) override;

  // base::PowerSuspendObserver implementation.
  void OnSuspend() override;
  void OnResume() override;

 private:
  friend struct base::DefaultSingletonTraits<CobaltMemoryAttributionManager>;
  friend struct base::LeakySingletonTraits<CobaltMemoryAttributionManager>;
  friend class CobaltMemoryAttributionManagerTest;
  friend class cobalt::MemoryAttributionBrowserTest;

  CobaltMemoryAttributionManager();
  ~CobaltMemoryAttributionManager() override;

  void ReportUma();

  std::array<uint64_t, static_cast<size_t>(base::memory::MemoryContext::kCount)>
      last_snapshots_;
  base::TimeTicks last_report_time_;
  base::RepeatingTimer timer_;
  bool is_observing_ = false;
};

}  // namespace memory
}  // namespace cobalt

#endif  // COBALT_MEMORY_COBALT_MEMORY_ATTRIBUTION_MANAGER_H_
