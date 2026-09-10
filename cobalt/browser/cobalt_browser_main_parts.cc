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

#include "cobalt/browser/cobalt_browser_main_parts.h"

#include <memory>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/sequence_checker.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/trace_event/memory_dump_manager.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "cobalt/browser/global_features.h"
#include "cobalt/browser/h5vcc_native_stability/native_stability_manager.h"
#include "cobalt/browser/memory_ablation.h"
#include "cobalt/browser/metrics/cobalt_detailed_metrics_delegate.h"
#include "cobalt/browser/metrics/cobalt_metrics_service_client.h"
#include "cobalt/browser/metrics/cobalt_stability_metrics_helper.h"
#include "cobalt/browser/switches.h"
#include "cobalt/memory/cobalt_memory_attribution_manager.h"
#include "cobalt/shell/browser/migrate_storage_record/migration_manager.h"
#include "cobalt/shell/browser/shell_content_browser_client.h"
#include "cobalt/shell/common/shell_paths.h"
#include "components/metrics/metrics_service.h"
#include "components/metrics/persistent_histograms.h"
#include "components/metrics/persistent_system_profile.h"
#include "components/metrics_services_manager/metrics_services_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/resource_coordinator_service.h"
#include "content/public/common/result_codes.h"

#if BUILDFLAG(USE_EVERGREEN)
#include "starboard/extension/native_stability.h"
#include "starboard/system.h"
#endif

#include "services/resource_coordinator/public/cpp/memory_instrumentation/client_process_impl.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/memory_instrumentation.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/memory_instrumentation_features.h"
#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "components/services/heap_profiling/heap_profiling_service.h"  // nogncheck
#include "components/services/heap_profiling/public/cpp/profiling_client.h"  // nogncheck
#include "components/services/heap_profiling/public/mojom/heap_profiling_service.mojom.h"  // nogncheck
#include "mojo/public/cpp/bindings/remote.h"
#include "services/tracing/public/cpp/trace_startup.h"
#endif

#if BUILDFLAG(IS_ANDROID)
#include "base/android/build_info.h"
#include "components/crash/content/browser/process_exit_reason_from_system_android.h"
#endif

#if BUILDFLAG(IS_ANDROIDTV)
#include "base/android/memory_pressure_listener_android.h"
#include "cobalt/browser/android/mojo/cobalt_interface_registrar_android.h"
#include "starboard/android/shared/starboard_bridge.h"
#else
#include "cobalt/browser/cobalt_content_browser_client.h"
#endif

#if BUILDFLAG(IS_LINUX)
#include "components/os_crypt/sync/key_storage_config_linux.h"
#include "components/os_crypt/sync/os_crypt.h"
#endif

namespace cobalt {

namespace {

void InitializeBrowserMemoryInstrumentationClient() {
  if (memory_instrumentation::MemoryInstrumentation::GetInstance()) {
    return;
  }

  auto task_runner = base::trace_event::MemoryDumpManager::GetInstance()
                         ->GetDumpThreadTaskRunner();
  if (!task_runner->RunsTasksInCurrentSequence()) {
    task_runner->PostTask(
        FROM_HERE,
        base::BindOnce(&InitializeBrowserMemoryInstrumentationClient));
    return;
  }

  if (memory_instrumentation::MemoryInstrumentation::GetInstance()) {
    return;
  }

  // Register the browser process as a memory-instrumentation client.
  // This replicates content::InitializeBrowserMemoryInstrumentationClient()
  // while avoiding unauthorized header includes.
  mojo::PendingRemote<memory_instrumentation::mojom::Coordinator> coordinator;
  mojo::PendingRemote<memory_instrumentation::mojom::ClientProcess> process;
  auto process_receiver = process.InitWithNewPipeAndPassReceiver();
  content::GetMemoryInstrumentationRegistry()->RegisterClientProcess(
      coordinator.InitWithNewPipeAndPassReceiver(), std::move(process),
      memory_instrumentation::mojom::ProcessType::BROWSER,
      base::GetCurrentProcId(), /*service_name=*/std::nullopt);
  memory_instrumentation::ClientProcessImpl::CreateInstance(
      std::move(process_receiver), std::move(coordinator),
      /*is_browser_process=*/true);
}

#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
void RegisterCobaltHeapProfilerOnDumpThread() {
  [[maybe_unused]] static base::SequenceChecker sequence_checker;
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker);

  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  LOG(INFO) << "RegisterCobaltHeapProfilerOnDumpThread: Initializing heap "
               "profiling service on Dump Thread...";

  // 1. Register the heap profiler with the memory instrumentation registry
  // (safely on Dump Thread).
  mojo::PendingRemote<memory_instrumentation::mojom::HeapProfilerHelper> helper;
  mojo::PendingRemote<memory_instrumentation::mojom::HeapProfiler> profiler;
  auto profiler_receiver = profiler.InitWithNewPipeAndPassReceiver();
  content::GetMemoryInstrumentationRegistry()->RegisterHeapProfiler(
      std::move(profiler), helper.InitWithNewPipeAndPassReceiver());

  // 2. Launch the heap profiling service, passing the receiver and helper.
  static base::NoDestructor<
      mojo::Remote<heap_profiling::mojom::ProfilingService>>
      g_profiling_service;
  g_profiling_service->Bind(heap_profiling::LaunchService(
      std::move(profiler_receiver), std::move(helper)));

  // 3. Create and bind our local ProfilingClient.
  static base::NoDestructor<heap_profiling::ProfilingClient> g_profiling_client;
  mojo::PendingRemote<heap_profiling::mojom::ProfilingClient> client_remote;
  g_profiling_client->BindToInterface(
      client_remote.InitWithNewPipeAndPassReceiver());

  // 4. Add our process as a profiling client to the profiling service.
  auto params = heap_profiling::mojom::ProfilingParams::New();
  params->sampling_rate = 128 * 1024;  // 128KB sampling rate
  params->stack_mode =
      heap_profiling::mojom::StackMode::NATIVE_WITH_THREAD_NAMES;

  (*g_profiling_service)
      ->AddProfilingClient(
          base::GetCurrentProcId(), std::move(client_remote),
          heap_profiling::mojom::ProcessType::BROWSER, std::move(params),
          base::BindOnce([](bool success) {
            LOG(INFO) << "Cobalt Heap Profiler Client added successfully: "
                      << success;
          }));
}

void InitializeCobaltHeapProfiler() {
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          "enable-heap-profiling")) {
    return;
  }
  LOG(INFO) << "InitializeCobaltHeapProfiler: Posting initialization task to "
               "Dump Thread...";
  auto task_runner = base::trace_event::MemoryDumpManager::GetInstance()
                         ->GetDumpThreadTaskRunner();
  task_runner->PostTask(
      FROM_HERE, base::BindOnce(&RegisterCobaltHeapProfilerOnDumpThread));
}
#endif  // !BUILDFLAG(COBALT_IS_RELEASE_BUILD)

constexpr char kBrowserStabilityMetricsName[] = "BrowserStabilityMetrics";
constexpr size_t kStabilityMetricsAllocSize = 512 * 1024;  // 512 KiB limit
constexpr uint32_t kStabilityMetricsAllocId = 0x53544142;  // "STAB"

void LogStabilityMetricsCapacity(const char* stage_label) {
  auto* allocator = base::GlobalHistogramAllocator::Get();
  if (!allocator) {
    return;
  }
  auto* mem_allocator = allocator->memory_allocator();
  if (!mem_allocator) {
    return;
  }

  size_t used = mem_allocator->used();
  size_t total = mem_allocator->size();
  int percent_full = total > 0 ? static_cast<int>((used * 100) / total) : 0;

  base::UmaHistogramPercentage("Cobalt.StabilityMetrics.PercentFull",
                               percent_full);
  base::UmaHistogramCounts1000("Cobalt.StabilityMetrics.UsedKilobytes",
                               static_cast<int>(used / 1024));

  bool is_near_capacity = mem_allocator->IsFull() || percent_full >= 90;
  base::UmaHistogramBoolean("Cobalt.StabilityMetrics.IsNearCapacity",
                            is_near_capacity);

  if (is_near_capacity) {
    LOG(WARNING) << "Cobalt Stability Metrics PMA approaching capacity at "
                 << stage_label << "! Used: " << (used / 1024) << "KB / "
                 << (total / 1024) << "KB (" << percent_full << "% full)";
  }
}

#if BUILDFLAG(IS_ANDROID)
void RecordPriorSessionExitReasons() {
  base::FilePath base_dir;
  if (!base::PathService::Get(base::DIR_ANDROID_APP_DATA, &base_dir)) {
    return;
  }
  base::FilePath metrics_dir =
      base_dir.AppendASCII(kBrowserStabilityMetricsName);
  for (base::ProcessId pid :
       ExtractPriorSessionPids(metrics_dir, kBrowserStabilityMetricsName,
                               base::GetCurrentProcId())) {
    crash_reporter::ProcessExitReasonFromSystem::RecordExitReasonToUma(
        pid, "Cobalt.Stability.Android.SystemExitReason");
  }
}
#endif

}  // namespace

int CobaltBrowserMainParts::PreEarlyInitialization() {
  if (!base::GlobalHistogramAllocator::Get()) {
    base::FilePath base_dir;
    bool path_ok = false;
#if BUILDFLAG(IS_ANDROID)
    path_ok = base::PathService::Get(base::DIR_ANDROID_APP_DATA, &base_dir);
#else
    path_ok = base::PathService::Get(base::DIR_TEMP, &base_dir);
#endif
    if (!path_ok) {
      LOG(WARNING) << "Failed to get base directory for "
                   << kBrowserStabilityMetricsName
                   << ", falling back to 512 KB local memory.";
      base::GlobalHistogramAllocator::CreateWithLocalMemory(
          kStabilityMetricsAllocSize, kStabilityMetricsAllocId,
          kBrowserStabilityMetricsName);
    } else {
      base::FilePath metrics_dir =
          base_dir.AppendASCII(kBrowserStabilityMetricsName);

      base::CreateDirectory(metrics_dir);

      base::FilePath active_file =
          base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
              metrics_dir, kBrowserStabilityMetricsName, base::Time::Now(),
              base::GetCurrentProcId());

      // Instantiate 512 KB memory-mapped PMA
      if (base::GlobalHistogramAllocator::CreateWithFile(
              active_file, kStabilityMetricsAllocSize, kStabilityMetricsAllocId,
              kBrowserStabilityMetricsName, /*exclusive_write=*/true)) {
        LOG(INFO) << "Cobalt Persistent Histogram Allocator ("
                  << kBrowserStabilityMetricsName
                  << ", 512 KB) initialized at: " << active_file.value();
      } else {
        LOG(WARNING) << "Failed to initialize file-backed PMA for "
                     << kBrowserStabilityMetricsName
                     << ", falling back to 512 KB local memory.";
        base::GlobalHistogramAllocator::CreateWithLocalMemory(
            kStabilityMetricsAllocSize, kStabilityMetricsAllocId,
            kBrowserStabilityMetricsName);
      }
    }

    auto* allocator = base::GlobalHistogramAllocator::Get();
    if (allocator) {
      metrics::GlobalPersistentSystemProfile::GetInstance()
          ->RegisterPersistentAllocator(allocator->memory_allocator());
      allocator->CreateTrackingHistograms(kBrowserStabilityMetricsName);
    }
  }

  return ShellBrowserMainParts::PreEarlyInitialization();
}

void CobaltBrowserMainParts::InitializeMessageLoopContext() {
  // On Android, we completely defer WebContents creation until the Java layer
  // invokes ShellManager.launchShell() which reaches C++ via JNI.
  // On Linux, we might need the base behavior.
  // go/chrobalt-pre-initialization-storage-migration-pipeline
#if !BUILDFLAG(IS_ANDROID)
  ShellBrowserMainParts::InitializeMessageLoopContext();
#endif
}

CobaltBrowserMainParts::CobaltBrowserMainParts(const std::string& deep_link,
                                               bool is_visible)
    : ShellBrowserMainParts(deep_link, is_visible) {}

int CobaltBrowserMainParts::PreCreateThreads() {
#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  LOG(INFO) << "Native CommandLine: "
            << base::CommandLine::ForCurrentProcess()->GetCommandLineString();
#endif
#if BUILDFLAG(IS_ANDROIDTV)
  starboard::StarboardBridge::GetInstance()->SetStartupMilestone(17);
#endif
  base::UmaHistogramSparse("Cobalt.Startup.MilestoneReached", 17);
  LogStabilityMetricsCapacity("PreCreateThreads");
  SetupMetrics();

  InitializeBrowserMemoryInstrumentationClient();

#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  InitializeCobaltHeapProfiler();
#endif

#if BUILDFLAG(IS_ANDROIDTV)
  base::android::MemoryPressureListenerAndroid::Initialize(
      base::android::AttachCurrentThread());
#endif
  return ShellBrowserMainParts::PreCreateThreads();
}

int CobaltBrowserMainParts::PreMainMessageLoopRun() {
  StartMetricsRecording();
  LogStabilityMetricsCapacity("PreMainMessageLoopRun");

#if BUILDFLAG(IS_ANDROID)
  if (base::android::BuildInfo::GetInstance()->sdk_int() >=
      base::android::SDK_VERSION_R) {
    base::ThreadPool::PostTask(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
         base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
        base::BindOnce(&RecordPriorSessionExitReasons));
  }
#endif

#if BUILDFLAG(COBALT_DETAILED_MEMORY_METRICS)
  static base::NoDestructor<CobaltDetailedMetricsDelegate> delegate;
  if (memory_instrumentation::MemoryInstrumentation::GetInstance()) {
    memory_instrumentation::MemoryInstrumentation::GetInstance()
        ->SetDetailedMetricsDelegate(delegate.get());
  }
#endif

  cobalt::memory::CobaltMemoryAttributionManager::Get()->Start();

  MaybeApplyMemoryAblation();

#if !BUILDFLAG(IS_ANDROIDTV)
  auto* client = CobaltContentBrowserClient::Get();
  CHECK(client) << "CobaltContentBrowserClient::Get() returned NULL in "
                << "PreMainMessageLoopRun!";
  client->SetUserAgentCrashAnnotation();
#endif  // !BUILDFLAG(IS_ANDROIDTV)

#if BUILDFLAG(USE_EVERGREEN)
  auto* native_stability_manager =
      h5vcc_native_stability::NativeStabilityManager::GetInstance();

  // TODO: b/528362453 - Consider moving this to an earlier startup stage (e.g.
  // PreEarlyInitialization) to ensure early startup crashes are also tagged.
  native_stability_manager->ArmCrashUuidAnnotation();
  native_stability_manager->PruneStorage();
#endif  // BUILDFLAG(USE_EVERGREEN)

  int result = ShellBrowserMainParts::PreMainMessageLoopRun();

  if (result != 0) {
    LOG(ERROR) << "PreMainMessageLoopRun failed with result: " << result
               << ". Aborting storage migration.";
    return result;
  }

  StartStorageMigration();

  return result;
}

void CobaltBrowserMainParts::StartStorageMigration() {
  LOG(INFO) << "CobaltBrowserMainParts::StartStorageMigration started.";

  // Ensure we are on the UI thread/Expected sequence before accessing the
  // partition.
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(sequence_checker_.CalledOnValidSequence());

  content::StoragePartition* partition =
      browser_context()->GetDefaultStoragePartition();

  DCHECK(partition);

  // M138 Change: Use base::BindPostTask to wrap the completion callback.
  // This guarantees that regardless of which thread the MigrationManager
  // finishes on, OnMigrationComplete will execute back on this UI sequence.
  auto completion_callback = base::BindPostTask(
      base::SequencedTaskRunner::GetCurrentDefault(),
      base::BindOnce(&CobaltBrowserMainParts::OnMigrationComplete,
                     weak_ptr_factory_.GetWeakPtr()));

  cobalt::migrate_storage_record::MigrationManager::RunMigration(
      partition, std::move(completion_callback));
}

void CobaltBrowserMainParts::OnMigrationComplete() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(sequence_checker_.CalledOnValidSequence());
  LOG(INFO) << "Migration complete. Proceeding with deferred launchShell.";
  migration_finished_ = true;
  if (pending_task_) {
    std::move(pending_task_).Run();
  }
}

void CobaltBrowserMainParts::PostOrRunIfStorageMigrationFinished(
    base::OnceClosure task) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(sequence_checker_.CalledOnValidSequence());
  if (!migration_finished_) {
    LOG(INFO) << "Deferring launchShell until migration finishes.";
    CHECK(!pending_task_) << "Only one storage migration task is supported.";
    pending_task_ = std::move(task);
  } else {
    LOG(INFO) << "Migration already finished, running launchShell "
                 "immediately.";
    std::move(task).Run();
  }
}

void CobaltBrowserMainParts::PostDestroyThreads() {
  GlobalFeatures::GetInstance()->Shutdown();
  ShellBrowserMainParts::PostDestroyThreads();
}

void CobaltBrowserMainParts::SetupMetrics() {
  metrics::MetricsService* metrics =
      GlobalFeatures::GetInstance()->metrics_service();
  metrics->InitializeMetricsRecordingState();
  LOG(INFO) << "Cobalt Metrics Service initialized.";
}

void CobaltBrowserMainParts::StartMetricsRecording() {
  // This call kicks off the whole metric recording flow. It sets a timer and
  // periodically triggers a UMA payload to be handled by the logs uploader.
  // Note, "may_upload" is always true but reporting itself can be disabled and
  // no uploads are sent.
  GlobalFeatures::GetInstance()
      ->metrics_services_manager()
      ->UpdateUploadPermissions(true);
  LOG(INFO) << "Metrics Service is now running/recording.";
}

#if BUILDFLAG(IS_ANDROIDTV)
void CobaltBrowserMainParts::PostCreateThreads() {
  // TODO(cobalt, b/383301493): this looks like a reasonable stage at which to
  // register these interfaces and it seems to work. But we may want to
  // consider if there's a more suitable stage.
  RegisterCobaltJavaMojoInterfaces();
  ShellBrowserMainParts::PostCreateThreads();
}
#endif  // BUILDFLAG(IS_ANDROIDTV)

#if BUILDFLAG(IS_LINUX)
void CobaltBrowserMainParts::PostCreateMainMessageLoop() {
  // Set up crypt config. This needs to be done before anything starts the
  // network service, as the raw encryption key needs to be shared with the
  // network service for encrypted cookie storage.
  // Chrome OS does not need a crypt config as its user data directories are
  // already encrypted and none of the true encryption backends used by
  // desktop Linux are available on Chrome OS anyway.
  std::unique_ptr<os_crypt::Config> config =
      std::make_unique<os_crypt::Config>();
  // Forward the product name
  config->product_name = "Cobalt";
  // OSCrypt can be disabled in a special settings file.
  config->should_use_preference = false;
  base::PathService::Get(content::SHELL_DIR_USER_DATA, &config->user_data_path);
  OSCrypt::SetConfig(std::move(config));
  ShellBrowserMainParts::PostCreateMainMessageLoop();
}
#endif  // BUILDFLAG(IS_LINUX)

}  // namespace cobalt
