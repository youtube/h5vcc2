// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

#include "cobalt/browser/performance/performance_impl.h"

#include <utility>

#include "base/command_line.h"
#include "base/notreached.h"
#include "base/process/process_handle.h"
#include "base/process/process_metrics.h"
#include "base/system/sys_info.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_POSIX)
#include <unistd.h>
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID)
#include "base/debug/proc_maps_linux.h"
#endif

#if BUILDFLAG(IS_ANDROID)
#include "base/android/jni_android.h"
#include "base/memory_jni/CobaltMemoryInfoBridge_jni.h"
#include "base/memory_jni/MemoryInfoBridge_jni.h"
#endif

#if BUILDFLAG(IS_ANDROIDTV)
#include "base/android/jni_android.h"
#include "starboard/android/shared/starboard_bridge.h"

using ::starboard::StarboardBridge;
#elif BUILDFLAG(IS_STARBOARD)
#include "starboard/common/time.h"
#include "starboard/system.h"
#endif

#if BUILDFLAG(IS_STARBOARD)
#include "starboard/system.h"
#endif

namespace performance {

PerformanceImpl::PerformanceImpl(
    std::optional<int64_t> app_startup_timestamp,
    content::RenderFrameHost& render_frame_host,
    mojo::PendingReceiver<mojom::CobaltPerformance> receiver)
    : content::DocumentService<mojom::CobaltPerformance>(render_frame_host,
                                                         std::move(receiver)),
      app_startup_timestamp_(app_startup_timestamp) {}

void PerformanceImpl::Create(
    std::optional<int64_t> app_startup_timestamp,
    content::RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<mojom::CobaltPerformance> receiver) {
  new PerformanceImpl(app_startup_timestamp, *render_frame_host,
                      std::move(receiver));
}

void PerformanceImpl::MeasureSystemMemoryInfo(
    MeasureSystemMemoryInfoCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> mojom::SystemMemoryInfoPtr {
        auto info = mojom::SystemMemoryInfo::New();

        auto process_metrics = base::ProcessMetrics::CreateProcessMetrics(
            base::GetCurrentProcessHandle());
        if (process_metrics) {
          auto mem_info = process_metrics->GetMemoryInfo();
          if (mem_info.has_value()) {
            info->used_rss_memory = mem_info->resident_set_bytes;
            info->reserved_virtual_memory = mem_info->vm_size_bytes;
#if !BUILDFLAG(IS_IOS_TVOS)
            info->used_swap_memory = mem_info->vm_swap_bytes;
#endif
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID)
            info->rss_high_water_mark_memory = mem_info->vm_hwm_bytes;
            info->used_rss_anon_memory = mem_info->rss_anon_bytes;
#endif
          }
        }

        info->free_rss_memory =
            base::SysInfo::AmountOfAvailablePhysicalMemory();

#if BUILDFLAG(IS_POSIX) && defined(_SC_PHYS_PAGES)
        long pages = sysconf(_SC_PHYS_PAGES);
#if defined(_SC_PAGESIZE)
        long page_size = sysconf(_SC_PAGESIZE);
#elif defined(_SC_PAGE_SIZE)
        long page_size = sysconf(_SC_PAGE_SIZE);
#else
        long page_size = -1;
#endif
        if (pages > 0 && page_size > 0) {
          info->total_cpu_memory =
              static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
        }
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID)
        auto smaps_rollup = base::debug::ReadAndParseSmapsRollup();
        if (smaps_rollup.has_value()) {
          info->used_pss_memory = smaps_rollup->pss;
        }
#endif

#if BUILDFLAG(IS_STARBOARD)
        // TODO(b/555849706): Migrate Android/iOS to use Starboard memory APIs.
        int64_t limit = SbSystemGetTotalCPUMemory();
        if (limit > 0) {
          info->application_limit_memory = static_cast<uint64_t>(limit);
        }
        int64_t usage = SbSystemGetUsedCPUMemory();
        if (usage > 0) {
          info->application_usage_memory = static_cast<uint64_t>(usage);
        }
#endif

#if BUILDFLAG(IS_STARBOARD)
        // TODO(b/555849706): Migrate Android/iOS to use Starboard memory APIs.
        if (SbSystemHasCapability(kSbSystemCapabilityCanQueryGPUMemoryStats)) {
          info->used_gpu_memory = SbSystemGetUsedGPUMemory();
        }
#elif BUILDFLAG(IS_ANDROID)
        JNIEnv* env = base::android::AttachCurrentThread();
        base::android::ScopedJavaLocalRef<jobject> memory_info =
            Java_MemoryInfoBridge_getActivityManagerMemoryInfoForSelf(env);
        if (!memory_info.is_null()) {
          int graphics_kb =
              Java_CobaltMemoryInfoBridge_getGraphicsMemoryKb(env, memory_info);
          if (graphics_kb >= 0) {
            info->used_gpu_memory = static_cast<uint64_t>(graphics_kb) * 1024;
          }
        }
#endif

        return info;
      }),
      std::move(callback));
}

void PerformanceImpl::MeasureFreeRssMemory(
    MeasureFreeRssMemoryCallback callback) {
  // Use lambda to resolve overload resolution ambiguity on platforms like
  // Android.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [] { return base::SysInfo::AmountOfAvailablePhysicalMemory(); }),
      std::move(callback));
}

void PerformanceImpl::MeasureAvailableCpuMemory(
    MeasureAvailableCpuMemoryCallback callback) {
  MeasureFreeRssMemory(std::move(callback));
}

void PerformanceImpl::MeasureUsedRssMemory(
    MeasureUsedRssMemoryCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> uint64_t {
        auto process_metrics = base::ProcessMetrics::CreateProcessMetrics(
            base::GetCurrentProcessHandle());
        if (!process_metrics) {
          return 0;
        }
        auto info = process_metrics->GetMemoryInfo();
        return info.has_value() ? info->resident_set_bytes : 0;
      }),
      std::move(callback));
}

void PerformanceImpl::MeasureUsedCpuMemory(
    MeasureUsedCpuMemoryCallback callback) {
  MeasureUsedRssMemory(std::move(callback));
}

void PerformanceImpl::MeasureUsedSwapMemory(
    MeasureUsedSwapMemoryCallback callback) {
#if BUILDFLAG(IS_IOS_TVOS)
  // vm_swap_bytes does not exist on tvOS.
  std::move(callback).Run(std::nullopt);
#else
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> std::optional<uint64_t> {
        auto process_metrics = base::ProcessMetrics::CreateProcessMetrics(
            base::GetCurrentProcessHandle());
        if (!process_metrics) {
          return std::nullopt;
        }
        auto info = process_metrics->GetMemoryInfo();
        return info.has_value() ? std::make_optional(info->vm_swap_bytes)
                                : std::nullopt;
      }),
      std::move(callback));
#endif  // BUILDFLAG(IS_IOS_TVOS)
}

void PerformanceImpl::MeasureReservedVirtualMemory(
    MeasureReservedVirtualMemoryCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> uint64_t {
        auto process_metrics = base::ProcessMetrics::CreateProcessMetrics(
            base::GetCurrentProcessHandle());
        if (!process_metrics) {
          return 0;
        }
        auto info = process_metrics->GetMemoryInfo();
        return info.has_value() ? info->vm_size_bytes : 0;
      }),
      std::move(callback));
}

void PerformanceImpl::MeasureRssHighWaterMarkMemory(
    MeasureRssHighWaterMarkMemoryCallback callback) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID)
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> uint64_t {
        auto process_metrics = base::ProcessMetrics::CreateProcessMetrics(
            base::GetCurrentProcessHandle());
        if (!process_metrics) {
          return 0;
        }
        auto info = process_metrics->GetMemoryInfo();
        return info.has_value() ? info->vm_hwm_bytes : 0;
      }),
      std::move(callback));
#else
  std::move(callback).Run(0);
#endif
}

void PerformanceImpl::MeasureUsedRssAnonMemory(
    MeasureUsedRssAnonMemoryCallback callback) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID)
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> uint64_t {
        auto process_metrics = base::ProcessMetrics::CreateProcessMetrics(
            base::GetCurrentProcessHandle());
        if (!process_metrics) {
          return 0;
        }
        auto info = process_metrics->GetMemoryInfo();
        return info.has_value() ? info->rss_anon_bytes : 0;
      }),
      std::move(callback));
#else
  std::move(callback).Run(0);
#endif
}

void PerformanceImpl::MeasureTotalCpuMemory(
    MeasureTotalCpuMemoryCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> uint64_t {
#if BUILDFLAG(IS_POSIX) && defined(_SC_PHYS_PAGES)
        long pages = sysconf(_SC_PHYS_PAGES);
#if defined(_SC_PAGESIZE)
        long page_size = sysconf(_SC_PAGESIZE);
#elif defined(_SC_PAGE_SIZE)
        long page_size = sysconf(_SC_PAGE_SIZE);
#else
        long page_size = -1;
#endif
        if (pages > 0 && page_size > 0) {
          return static_cast<uint64_t>(pages) *
                 static_cast<uint64_t>(page_size);
        }
#endif
        return 0;
      }),
      std::move(callback));
}

void PerformanceImpl::MeasureUsedPssMemory(
    MeasureUsedPssMemoryCallback callback) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID)
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> uint64_t {
        auto smaps_rollup = base::debug::ReadAndParseSmapsRollup();
        return smaps_rollup.has_value() ? smaps_rollup->pss : 0;
      }),
      std::move(callback));
#else
  std::move(callback).Run(0);
#endif
}

void PerformanceImpl::MeasureApplicationLimitMemory(
    MeasureApplicationLimitMemoryCallback callback) {
#if BUILDFLAG(IS_STARBOARD)
  // TODO(b/555849706): Migrate Android/iOS to use Starboard memory APIs.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> uint64_t {
        int64_t limit = SbSystemGetTotalCPUMemory();
        return limit > 0 ? static_cast<uint64_t>(limit) : 0;
      }),
      std::move(callback));
#else
  std::move(callback).Run(0);
#endif
}

void PerformanceImpl::MeasureApplicationUsageMemory(
    MeasureApplicationUsageMemoryCallback callback) {
#if BUILDFLAG(IS_STARBOARD)
  // TODO(b/555849706): Migrate Android/iOS to use Starboard memory APIs.
  int64_t usage = SbSystemGetUsedCPUMemory();
  std::move(callback).Run(usage > 0 ? static_cast<uint64_t>(usage) : 0);
#else
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> uint64_t {
        auto process_metrics = base::ProcessMetrics::CreateProcessMetrics(
            base::GetCurrentProcessHandle());
        if (!process_metrics) {
          return 0;
        }
        auto info = process_metrics->GetMemoryInfo();
        return info.has_value() ? info->resident_set_bytes : 0;
      }),
      std::move(callback));
#endif
}

void PerformanceImpl::MeasureUsedGpuMemory(
    MeasureUsedGpuMemoryCallback callback) {
#if BUILDFLAG(IS_STARBOARD)
  // TODO(b/555849706): Migrate Android/iOS to use Starboard memory APIs.
  if (!SbSystemHasCapability(kSbSystemCapabilityCanQueryGPUMemoryStats)) {
    std::move(callback).Run(false, 0);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> uint64_t { return SbSystemGetUsedGPUMemory(); }),
      base::BindOnce([](MeasureUsedGpuMemoryCallback cb,
                        uint64_t bytes) { std::move(cb).Run(true, bytes); },
                     std::move(callback)));
#elif BUILDFLAG(IS_ANDROID)
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() -> std::pair<bool, uint64_t> {
        JNIEnv* env = base::android::AttachCurrentThread();
        base::android::ScopedJavaLocalRef<jobject> memory_info =
            Java_MemoryInfoBridge_getActivityManagerMemoryInfoForSelf(env);
        if (memory_info.is_null()) {
          return {false, 0};
        }
        int graphics_kb =
            Java_CobaltMemoryInfoBridge_getGraphicsMemoryKb(env, memory_info);
        DCHECK_GE(graphics_kb, 0);
        return {true, static_cast<uint64_t>(graphics_kb) * 1024};
      }),
      base::BindOnce(
          [](MeasureUsedGpuMemoryCallback cb,
             std::pair<bool, uint64_t> result) {
            std::move(cb).Run(result.first, result.second);
          },
          std::move(callback)));
#elif BUILDFLAG(IS_IOS_TVOS)
  NOTIMPLEMENTED();
  std::move(callback).Run(false, 0);
#else
  std::move(callback).Run(false, 0);
#endif
}

void PerformanceImpl::GetAppStartupTimeStamp(
    GetAppStartupTimeStampCallback callback) {
#if BUILDFLAG(IS_ANDROIDTV)
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          "use-starboard-lifecycle") &&
      !app_startup_timestamp_.has_value()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    app_startup_timestamp_ =
        StarboardBridge::GetInstance()->GetAppStartTimestamp(env);
  }
#endif
  std::move(callback).Run(app_startup_timestamp_.value_or(0));
}

}  // namespace performance
