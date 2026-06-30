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

#include "cobalt/memory/cobalt_memory_attribution_manager.h"

#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/cobalt_memory_attribution_observer.h"
#include "base/metrics/histogram_functions.h"
#include "base/power_monitor/power_monitor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/process_memory_dump.h"
#include "cobalt/browser/features.h"

namespace cobalt {
namespace memory {

// static
CobaltMemoryAttributionManager* CobaltMemoryAttributionManager::Get() {
  return base::Singleton<
      CobaltMemoryAttributionManager,
      base::LeakySingletonTraits<CobaltMemoryAttributionManager>>::get();
}

CobaltMemoryAttributionManager::CobaltMemoryAttributionManager() {
  last_snapshots_.fill(0);
}

CobaltMemoryAttributionManager::~CobaltMemoryAttributionManager() = default;

void CobaltMemoryAttributionManager::Start() {
  if (!base::FeatureList::IsEnabled(
          cobalt::features::kCobaltMemoryAttributionManager)) {
    return;
  }

  if (is_observing_) {
    return;
  }

  is_observing_ = true;

  base::trace_event::MemoryDumpManager::GetInstance()->RegisterDumpProvider(
      this, "CobaltMemoryAttributionManager",
      base::SingleThreadTaskRunner::GetCurrentDefault());

  // Register for power suspend events so we can discard partial allocation
  // intervals prior to sleep and establish a fresh baseline upon waking up.
  // This ensures our UMA reports cover clean, uninterrupted time intervals.
  base::PowerMonitor::GetInstance()->AddPowerSuspendObserver(this);

  last_report_time_ = base::TimeTicks::Now();
  timer_.Start(
      FROM_HERE,
      base::Seconds(
          cobalt::features::kCobaltMemoryAttributionReportIntervalParam.Get()),
      this, &CobaltMemoryAttributionManager::ReportUma);
}

void CobaltMemoryAttributionManager::Stop() {
  if (!is_observing_) {
    return;
  }

  is_observing_ = false;

  base::trace_event::MemoryDumpManager::GetInstance()->UnregisterDumpProvider(
      this);

  base::PowerMonitor::GetInstance()->RemovePowerSuspendObserver(this);

  timer_.Stop();
}

void CobaltMemoryAttributionManager::RequestReportUmaForTesting(
    base::OnceClosure callback) {
  last_report_time_ = base::TimeTicks::Now();
  ReportUma();
  std::move(callback).Run();
}

void CobaltMemoryAttributionManager::ReportUma() {
  base::TimeTicks now = base::TimeTicks::Now();
  auto* observer = base::memory::CobaltMemoryAttributionObserver::Get();
  // Skip reporting if timer was significantly delayed (e.g. device suspension).
  if ((now - last_report_time_) >
      base::Seconds(
          cobalt::features::kCobaltMemoryAttributionReportIntervalParam.Get()) *
          2) {
    last_report_time_ = now;
    for (size_t i = 0;
         i < static_cast<size_t>(base::memory::MemoryContext::kCount); ++i) {
      last_snapshots_[i] =
          observer->GetCounters()[i].value.load(std::memory_order_relaxed);
    }
    return;
  }
  last_report_time_ = now;

  auto counters = observer->GetCounters();
  for (size_t i = 0; i < counters.size(); ++i) {
    uint64_t current = counters[i].value.load(std::memory_order_relaxed);
    uint64_t delta = current - last_snapshots_[i];
    uint64_t emitted_kb = delta / 1024;
    last_snapshots_[i] += emitted_kb * 1024;

    base::UmaHistogramCustomCounts(
        base::StrCat({"Memory.Cobalt.AllocationVolume.",
                      base::memory::ContextToString(
                          static_cast<base::memory::MemoryContext>(i))}),
        emitted_kb,
        /*minimum=*/1,
        /*maximum=*/67108864,
        /*bucket_count=*/100);
  }
}

void CobaltMemoryAttributionManager::OnSuspend() {
  // Capture current snapshots upon suspension so that when we resume,
  // the first report only covers allocations that happened after resume.
  auto* observer = base::memory::CobaltMemoryAttributionObserver::Get();
  auto counters = observer->GetCounters();
  for (size_t i = 0; i < counters.size(); ++i) {
    last_snapshots_[i] = counters[i].value.load(std::memory_order_relaxed);
  }
}

void CobaltMemoryAttributionManager::OnResume() {
  last_report_time_ = base::TimeTicks::Now();
  // Restart the timer to align with resume time.
  timer_.Reset();
}

bool CobaltMemoryAttributionManager::OnMemoryDump(
    const base::trace_event::MemoryDumpArgs& args,
    base::trace_event::ProcessMemoryDump* pmd) {
  auto* observer = base::memory::CobaltMemoryAttributionObserver::Get();
  auto counters = observer->GetCounters();
  for (size_t i = 0; i < counters.size(); ++i) {
    uint64_t current = counters[i].value.load(std::memory_order_relaxed);
    std::string dump_name =
        base::StrCat({"cobalt/memory_attribution/",
                      base::memory::ContextToString(
                          static_cast<base::memory::MemoryContext>(i))});
    auto* dump = pmd->CreateAllocatorDump(dump_name);
    dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameSize,
                    base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                    current);
  }
  return true;
}

}  // namespace memory
}  // namespace cobalt
