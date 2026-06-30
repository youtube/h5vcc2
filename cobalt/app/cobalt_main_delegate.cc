// Copyright 2024 The Cobalt Authors. All Rights Reserved.
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

#include "cobalt/app/cobalt_main_delegate.h"

#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "base/check_op.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/process/current_process.h"
#include "base/threading/hang_watcher.h"
#include "build/buildflag.h"
#include "cobalt/browser/cobalt_content_browser_client.h"
#include "cobalt/browser/features.h"
#include "cobalt/common/cobalt_thread_checker.h"
#include "cobalt/shell/app/shell_main_delegate.h"
#include "cobalt/utility/cobalt_content_utility_client.h"
#include "content/public/browser/browser_main_runner.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/main_function_params.h"
#include "content/public/gpu/content_gpu_client.h"
#include "content/public/renderer/content_renderer_client.h"

#if BUILDFLAG(IS_STARBOARD) || BUILDFLAG(IS_ANDROIDTV)
#include "cobalt/browser/hang_watcher_delegate_impl.h"
#endif
#include "cobalt/app/cobalt_crash_reporter_client.h"
#include "cobalt/gpu/cobalt_content_gpu_client.h"
#include "cobalt/renderer/cobalt_content_renderer_client.h"
#include "components/crash/core/app/crashpad.h"
#include "components/memory_system/initializer.h"
#include "components/memory_system/parameters.h"
#include "content/public/app/initialize_mojo_core.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/common/content_switches.h"
#include "gpu/command_buffer/service/gpu_switches.h"
#if BUILDFLAG(IS_ANDROIDTV)
#include "starboard/android/shared/starboard_bridge.h"
#endif

#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_ANDROID)
#include "base/base_switches.h"
#include "v8/include/v8-wasm-trap-handler-posix.h"
#endif

#if BUILDFLAG(IS_STARBOARD)
#include "base/debug/dump_without_crashing.h"
#include "starboard/extension/crash_handler.h"
#include "starboard/system.h"
#endif

namespace cobalt {

CobaltMainDelegate::CobaltMainDelegate(
    absl::optional<int64_t> startup_timestamp,
    const char* initial_deep_link,
    bool is_content_browsertests,
    bool is_visible)
    : content::ShellMainDelegate(),
      startup_timestamp_(startup_timestamp),
      is_visible_(is_visible),
      deep_link_(initial_deep_link ? initial_deep_link : "") {
  is_content_browsertests_ = is_content_browsertests;
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

CobaltMainDelegate::~CobaltMainDelegate() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

std::optional<int> CobaltMainDelegate::BasicStartupComplete() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
#if BUILDFLAG(IS_ANDROIDTV)
  starboard::StarboardBridge::GetInstance()->SetStartupMilestone(14);
#endif
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  cl->AppendSwitch(switches::kEnableAggressiveDOMStorageFlushing);
  cl->AppendSwitch(switches::kDisableGpuShaderDiskCache);
  return content::ShellMainDelegate::BasicStartupComplete();
}

content::ContentBrowserClient*
CobaltMainDelegate::CreateContentBrowserClient() {
  browser_client_ = std::make_unique<CobaltContentBrowserClient>(
      startup_timestamp_, deep_link_, is_visible_);
  return browser_client_.get();
}

content::ContentGpuClient* CobaltMainDelegate::CreateContentGpuClient() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  gpu_client_ = std::make_unique<CobaltContentGpuClient>();
  return gpu_client_.get();
}

content::ContentRendererClient*
CobaltMainDelegate::CreateContentRendererClient() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  renderer_client_ = std::make_unique<CobaltContentRendererClient>();
  return renderer_client_.get();
}

content::ContentUtilityClient*
CobaltMainDelegate::CreateContentUtilityClient() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  utility_client_ = std::make_unique<CobaltContentUtilityClient>();
  return utility_client_.get();
}

std::optional<int> CobaltMainDelegate::PostEarlyInitialization(
    InvokedIn invoked_in) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
#if BUILDFLAG(IS_ANDROIDTV)
  starboard::StarboardBridge::GetInstance()->SetStartupMilestone(15);
#endif
  content::RenderFrameHost::AllowInjectingJavaScript();

  if (!ShouldCreateFeatureList(invoked_in)) {
    // Apply field trial testing configuration since content did not.
    browser_client_->CreateFeatureListAndFieldTrials();
  }
  if (!ShouldInitializeMojo(invoked_in)) {
    content::InitializeMojoCore();
  }

#if BUILDFLAG(IS_STARBOARD) || BUILDFLAG(IS_ANDROIDTV)
  // This delegate is for reading the flag value.
  cobalt::browser::CobaltHangWatcherDelegate::Initialize();
#endif

  InitializeHangWatcher();

  InitializeMemorySystem();

  return std::nullopt;
}

void CobaltMainDelegate::InitializeMemorySystem() {
  const std::string process_type =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kProcessType);

  // ShellMainDelegate has GWP-ASan as well as Profiling Client disabled.
  // Consequently, we provide no parameters for these two. The memory_system
  // includes the PoissonAllocationSampler dynamically only if the Profiling
  // Client is enabled. However, we are not sure if this is the only user of
  // PoissonAllocationSampler in the ContentShell. Therefore, enforce inclusion
  // at the moment.
  //
  // TODO(https://crbug.com/1411454): Clarify which users of
  // PoissonAllocationSampler we have in the ContentShell. Do we really need to
  // enforce it?
  memory_system::Initializer()
      .SetDispatcherParameters(
          memory_system::DispatcherParameters::
              PoissonAllocationSamplerInclusion::kEnforce,
          memory_system::DispatcherParameters::
              AllocationTraceRecorderInclusion::kIgnore,
          process_type,
          base::FeatureList::IsEnabled(
              cobalt::features::kCobaltMemoryAttributionManager)
              ? memory_system::CobaltMemoryAttributionInclusion::kInclude
              : memory_system::CobaltMemoryAttributionInclusion::kDoNotInclude)
      .Initialize(memory_system_);

  return;
}

std::variant<int, content::MainFunctionParams> CobaltMainDelegate::RunProcess(
    const std::string& process_type,
    content::MainFunctionParams main_function_params) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
#if BUILDFLAG(IS_ANDROIDTV)
  starboard::StarboardBridge::GetInstance()->SetStartupMilestone(16);
#endif
  // For non-browser process, return and have the caller run the main loop.
  if (!process_type.empty()) {
    return std::move(main_function_params);
  }

  base::CurrentProcess::GetInstance().SetProcessType(
      base::CurrentProcessType::PROCESS_BROWSER);

  main_runner_ = content::BrowserMainRunner::Create();

  // In browser tests, the |main_function_params| contains a |ui_task| which
  // will execute the testing. The task will be executed synchronously inside
  // Initialize() so we don't depend on the BrowserMainRunner being Run().
  int initialize_exit_code =
      main_runner_->Initialize(std::move(main_function_params));
  DCHECK_LT(initialize_exit_code, 0)
      << "BrowserMainRunner::Initialize failed in ShellMainDelegate";

  // Return 0 as BrowserMain() should not be called after this, bounce up to
  // the system message loop for ContentShell, and we're already done thanks
  // to the |ui_task| for browser tests.
  return 0;
}

void CobaltMainDelegate::PreSandboxStartup() {
#if BUILDFLAG(IS_ANDROIDTV) || BUILDFLAG(IS_IOS_TVOS)
  const bool initialize_crashpad = true;
#else
  const bool initialize_crashpad =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableCrashReporter);
#endif  // BUILDFLAG(IS_ANDROIDTV) || BUILDFLAG(IS_IOS_TVOS)

  if (initialize_crashpad) {
    const std::string process_type =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            switches::kProcessType);
    CobaltCrashReporterClient::Create();
    if (process_type != switches::kZygoteProcess) {
      crash_reporter::InitializeCrashpad(process_type.empty(), process_type);
      crash_reporter::SetUploadConsent(true);
#if BUILDFLAG(IS_LINUX)
      crash_reporter::SetFirstChanceExceptionHandler(
          v8::TryHandleWebAssemblyTrapPosix);
#endif
    }
  }

  ShellMainDelegate::PreSandboxStartup();
}

void CobaltMainDelegate::Shutdown() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  main_runner_->Shutdown();
}

void CobaltMainDelegate::InitializeHangWatcher() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  const base::CommandLine* const command_line =
      base::CommandLine::ForCurrentProcess();
  std::string process_type =
      command_line->GetSwitchValueASCII(switches::kProcessType);

  // In single-process mode it's always kBrowserProcess.
  base::HangWatcher::ProcessType hang_watcher_process_type;
  if (process_type.empty()) {
    hang_watcher_process_type = base::HangWatcher::ProcessType::kBrowserProcess;
  } else if (process_type == switches::kGpuProcess) {
    hang_watcher_process_type = base::HangWatcher::ProcessType::kGPUProcess;
  } else if (process_type == switches::kRendererProcess) {
    hang_watcher_process_type =
        base::HangWatcher::ProcessType::kRendererProcess;
  } else if (process_type == switches::kUtilityProcess) {
    hang_watcher_process_type = base::HangWatcher::ProcessType::kUtilityProcess;
  } else {
    hang_watcher_process_type = base::HangWatcher::ProcessType::kUnknownProcess;
  }
  const bool emit_crashes = false;

  base::HangWatcher::InitializeOnMainThread(hang_watcher_process_type,
                                            emit_crashes);

#if BUILDFLAG(IS_STARBOARD)
  auto* crash_handler_extension =
      static_cast<const CobaltExtensionCrashHandlerApi*>(
          SbSystemGetExtension(kCobaltExtensionCrashHandlerName));
  if (crash_handler_extension && crash_handler_extension->version >= 4 &&
      crash_handler_extension->DumpWithoutCrashing) {
    // For Evergreen builds we do not currently link the Crashpad client
    // library into the hermetic libcobalt shared library. So, we leverage a
    // Starboard extension to tunnel the request for Crashpad to dump without
    // crashing.
    base::debug::SetDumpWithoutCrashingFunction(
        crash_handler_extension->DumpWithoutCrashing);
  }
#endif
}
}  // namespace cobalt
