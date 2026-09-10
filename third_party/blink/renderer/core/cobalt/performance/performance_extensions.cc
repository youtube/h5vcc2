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

#include "third_party/blink/renderer/core/cobalt/performance/performance_extensions.h"

#include "build/build_config.h"
#include "cobalt/browser/performance/public/mojom/performance.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_system_memory_info.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/timing/performance.h"
#include "v8/include/v8.h"

namespace blink {

namespace {

mojo::Remote<performance::mojom::CobaltPerformance> BindRemotePerformance(
    ScriptState* script_state) {
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  DCHECK(execution_context);

  mojo::Remote<performance::mojom::CobaltPerformance> remote_performance_system;
  auto task_runner =
      execution_context->GetTaskRunner(TaskType::kMiscPlatformAPI);
  execution_context->GetBrowserInterfaceBroker().GetInterface(
      remote_performance_system.BindNewPipeAndPassReceiver(task_runner));
  return remote_performance_system;
}

}  // namespace

SystemMemoryInfo* PerformanceExtensions::measureSystemMemoryInfo(
    ScriptState* script_state,
    const Performance&) {
  auto* result = SystemMemoryInfo::Create();

  performance::mojom::SystemMemoryInfoPtr info;
  BindRemotePerformance(script_state)->MeasureSystemMemoryInfo(&info);

  if (info) {
    result->setFreeRssMemory(info->free_rss_memory);
    result->setUsedRssMemory(info->used_rss_memory);
    result->setUsedSwapMemory(info->used_swap_memory);
    result->setReservedVirtualMemory(info->reserved_virtual_memory);
    result->setRssHighWaterMarkMemory(info->rss_high_water_mark_memory);
    result->setUsedRssAnonMemory(info->used_rss_anon_memory);
    result->setTotalCpuMemory(info->total_cpu_memory);
    result->setUsedPssMemory(info->used_pss_memory);
    result->setApplicationLimitMemory(info->application_limit_memory);
    result->setApplicationUsageMemory(info->application_usage_memory);
    if (info->used_gpu_memory.has_value()) {
      result->setUsedGpuMemory(info->used_gpu_memory.value());
    }
  }

  if (script_state && script_state->GetIsolate()) {
    v8::HeapStatistics heap_statistics;
    script_state->GetIsolate()->GetHeapStatistics(&heap_statistics);
    result->setUsedJSHeapSize(heap_statistics.used_heap_size());
    result->setTotalJSHeapSize(heap_statistics.total_physical_size());
    result->setJsHeapSizeLimit(heap_statistics.heap_size_limit());
  }

#if BUILDFLAG(USE_STARBOARD_MEDIA)
  if (Platform::Current()) {
    result->setMediaSourceTotalAllocatedMemory(
        Platform::Current()->GetMediaSourceTotalAllocatedMemory());
    result->setMediaSourceCurrentMemoryCapacity(
        Platform::Current()->GetMediaSourceCurrentMemoryCapacity());
    result->setMediaSourceMaximumMemoryCapacity(
        Platform::Current()->GetMediaSourceMaximumMemoryCapacity());
  }
#endif

  return result;
}

uint64_t PerformanceExtensions::measureFreeRssMemory(ScriptState* script_state,
                                                     const Performance&) {
  uint64_t free_memory = 0;
  BindRemotePerformance(script_state)->MeasureFreeRssMemory(&free_memory);
  return free_memory;
}

uint64_t PerformanceExtensions::measureAvailableCpuMemory(
    ScriptState* script_state,
    const Performance& performance) {
  return measureFreeRssMemory(script_state, performance);
}

uint64_t PerformanceExtensions::measureUsedRssMemory(ScriptState* script_state,
                                                     const Performance&) {
  uint64_t used_memory = 0;
  BindRemotePerformance(script_state)->MeasureUsedRssMemory(&used_memory);
  return used_memory;
}

uint64_t PerformanceExtensions::measureUsedCpuMemory(
    ScriptState* script_state,
    const Performance& performance) {
  return measureUsedRssMemory(script_state, performance);
}

uint64_t PerformanceExtensions::measureUsedSwapMemory(ScriptState* script_state,
                                                      const Performance&) {
  std::optional<uint64_t> used_swap_memory = std::nullopt;
  BindRemotePerformance(script_state)->MeasureUsedSwapMemory(&used_swap_memory);
  return used_swap_memory.value_or(0);
}

uint64_t PerformanceExtensions::measureReservedVirtualMemory(
    ScriptState* script_state,
    const Performance&) {
  uint64_t virtual_memory_size = 0;
  BindRemotePerformance(script_state)
      ->MeasureReservedVirtualMemory(&virtual_memory_size);
  return virtual_memory_size;
}

uint64_t PerformanceExtensions::measureRssHighWaterMarkMemory(
    ScriptState* script_state,
    const Performance&) {
  uint64_t rss_hwm_memory = 0;
  BindRemotePerformance(script_state)
      ->MeasureRssHighWaterMarkMemory(&rss_hwm_memory);
  return rss_hwm_memory;
}

uint64_t PerformanceExtensions::measureUsedRssAnonMemory(
    ScriptState* script_state,
    const Performance&) {
  uint64_t rss_anon_memory = 0;
  BindRemotePerformance(script_state)
      ->MeasureUsedRssAnonMemory(&rss_anon_memory);
  return rss_anon_memory;
}

uint64_t PerformanceExtensions::measureTotalCpuMemory(ScriptState* script_state,
                                                      const Performance&) {
  uint64_t total_cpu_memory = 0;
  BindRemotePerformance(script_state)->MeasureTotalCpuMemory(&total_cpu_memory);
  return total_cpu_memory;
}

uint64_t PerformanceExtensions::measureUsedPssMemory(ScriptState* script_state,
                                                     const Performance&) {
  uint64_t pss_memory = 0;
  BindRemotePerformance(script_state)->MeasureUsedPssMemory(&pss_memory);
  return pss_memory;
}

uint64_t PerformanceExtensions::measureApplicationLimitMemory(
    ScriptState* script_state,
    const Performance&) {
  uint64_t app_limit_memory = 0;
  BindRemotePerformance(script_state)
      ->MeasureApplicationLimitMemory(&app_limit_memory);
  return app_limit_memory;
}

uint64_t PerformanceExtensions::measureApplicationUsageMemory(
    ScriptState* script_state,
    const Performance&) {
  uint64_t usage_memory = 0;
  BindRemotePerformance(script_state)
      ->MeasureApplicationUsageMemory(&usage_memory);
  return usage_memory;
}

uint64_t PerformanceExtensions::measureUsedGpuMemory(
    ScriptState* script_state,
    const Performance&,
    ExceptionState& exception_state) {
  bool is_supported = false;
  uint64_t used_gpu_memory = 0;
  BindRemotePerformance(script_state)
      ->MeasureUsedGpuMemory(&is_supported, &used_gpu_memory);
  if (!is_supported) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "GPU memory measurement is not supported on this platform.");
    return 0;
  }
  return used_gpu_memory;
}

ScriptPromise<IDLDouble> PerformanceExtensions::getAppStartupTimeStamp(
    ScriptState* script_state,
    const Performance& performance_obj,
    ExceptionState& exception_state) {
  ExecutionContext* context = performance_obj.GetExecutionContext();
  if (!context) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "Context is missing.");
    return ScriptPromise<IDLDouble>();
  }

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLDouble>>(
      script_state, exception_state.GetContext());
  ScriptPromise<IDLDouble> promise = resolver->Promise();

  int64_t startup_timestamp = 0;
  BindRemotePerformance(script_state)
      ->GetAppStartupTimeStamp(&startup_timestamp);

  resolver->Resolve(Performance::MonotonicTimeToDOMHighResTimeStamp(
      performance_obj.GetTimeOriginInternal(),
      base::TimeTicks::FromInternalValue(startup_timestamp),
      true /* allow_negative_value */,
      context->CrossOriginIsolatedCapability()));

  return promise;
}

}  // namespace blink
