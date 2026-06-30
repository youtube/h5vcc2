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

#include "cobalt/app/app_event_runner.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "base/allocator/partition_allocator/src/partition_alloc/memory_reclaimer.h"
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/no_destructor.h"
#include "base/run_loop.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "cobalt/app/app_event_delegate.h"

#if BUILDFLAG(USE_EVERGREEN)
#include "cobalt/updater/updater_module.h"
#endif
#include "cobalt/browser/cobalt_content_browser_client.h"
#include "cobalt/browser/h5vcc_accessibility/h5vcc_accessibility_manager.h"
#include "cobalt/browser/h5vcc_runtime/deep_link_manager.h"
#include "cobalt/shell/browser/shell.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/network_service_instance.h"
#include "net/base/network_change_notifier_passive.h"

#if !BUILDFLAG(IS_ANDROID)
#include "content/public/app/content_main.h"
#include "content/public/app/content_main_runner.h"
#endif

#if BUILDFLAG(IS_STARBOARD)
#include "cobalt/app/cobalt_switch_defaults.h"
#include "services/device/time_zone_monitor/time_zone_monitor_starboard.h"
#include "ui/ozone/platform/starboard/platform_event_source_starboard.h"
#endif

#if BUILDFLAG(USE_EVERGREEN)
#include "cobalt/browser/loader_app_metrics.h"
#endif

namespace cobalt {

namespace {
// A timeout of 2 seconds was chosen to be long enough to allow for any
// normal operations to complete, but short enough to avoid unnecessary
// user-perceived delays.
constexpr base::TimeDelta kTransitionTimeout = base::Seconds(2);
}  // namespace

#if !BUILDFLAG(IS_ANDROID)
namespace {
content::ContentMainRunner* GetContentMainRunner() {
  static base::NoDestructor<std::unique_ptr<content::ContentMainRunner>>
      main_runner{content::ContentMainRunner::Create()};
  return main_runner->get();
}
}  // namespace
#endif

class AppEventRunnerImpl : public AppEventRunner,
                           public CobaltLifecycleManagerObserver {
 public:
  AppEventRunnerImpl() {
    CobaltLifecycleManager::GetInstance()->AddObserver(this);
  }
  ~AppEventRunnerImpl() override {
    CobaltLifecycleManager::GetInstance()->RemoveObserver(this);
  }

  PendingAck pending_ack() const override {
    base::AutoLock lock(lock_);
    return pending_ack_;
  }

  void InitializeSystem() override {
#if !BUILDFLAG(IS_ANDROID)
    exit_manager_ = std::make_unique<base::AtExitManager>();
#endif
  }

  void CreateMainDelegate(absl::optional<int64_t> startup_timestamp,
                          bool is_visible,
                          const char* initial_deep_link) override {
#if !BUILDFLAG(IS_ANDROID)
    content_main_delegate_ = std::make_unique<cobalt::CobaltMainDelegate>(
        startup_timestamp, initial_deep_link,
        false /* is_content_browsertests */, is_visible);
#endif
  }

  std::vector<content::WebContents*> GetWebContents() override {
    std::vector<content::WebContents*> result;
    for (auto* shell : content::Shell::windows()) {
      result.push_back(shell->web_contents());
    }
    return result;
  }

  cobalt::CobaltMainDelegate* GetMainDelegate() override {
    return content_main_delegate_.get();
  }

  void DoStart(const SbEvent* event) override {
    SbEventStartData* data = static_cast<SbEventStartData*>(event->data);
    InitializeSystem();
#if BUILDFLAG(IS_STARBOARD)
    platform_event_source_ =
        std::make_unique<ui::PlatformEventSourceStarboard>();
#endif

    if (data) {
      Run(event->timestamp, is_visible(), data->argument_count,
          const_cast<const char**>(data->argument_values), data->link);
    } else {
      Run(event->timestamp, is_visible(), 0, nullptr, nullptr);
    }

#if BUILDFLAG(USE_EVERGREEN)
    // Log Loader App Metrics.
    cobalt::browser::RecordLoaderAppMetrics();
#endif
  }

  void DoStop() override {
    content::Shell::OnStop();

    content::Shell::Shutdown();

    base::RunLoop().RunUntilIdle();

    if (content_main_delegate_) {
      content_main_delegate_->Shutdown();
    }

#if !BUILDFLAG(IS_ANDROID)
    // Must be called after content_main_delegate_->Shutdown() to prevent
    // unregistering the main thread from the SequenceManager which
    // happens with the destruction of the BrowserTaskExecutor. If the order
    // is reversed the SequenceManager complains (fails DCHECK) that the
    // ContentMainRunnerImpl::Shutdown() is happening on the wrong thread as
    // no longer recognizes the main thread as a valid TaskEnvironment.
    if (main_runner_) {
      main_runner_->Shutdown();
    }
#endif

    // Flush all open stdio streams before the process exits.
    std::fflush(nullptr);

    // Destroy only after main_runner_/ContentMainRunnerImpl is shutdown
    // as the delegate is used internally.
    content_main_delegate_.reset();
    exit_manager_.reset();

#if BUILDFLAG(IS_STARBOARD)
    platform_event_source_.reset();
#endif
  }

  void DoBlur() override {
    content::Shell::OnBlur();
    auto* client = cobalt::CobaltContentBrowserClient::Get();
    if (client) {
      client->FlushCookiesAndLocalStorage(base::DoNothing());
    }
#if BUILDFLAG(IS_STARBOARD)
    if (platform_event_source_) {
      platform_event_source_->DispatchFocusEvent(false);
    }
#else
    // Other platforms route events differently, e.g. on AndroidTV
    // focus is handled natively via Activity and View lifecycle callbacks
    // (e.g. CobaltActivity.onResume) which propagate directly to Chromium's
    // WindowAndroid.
#endif
    WaitForAck(PendingAck::kBlur);
  }

  void DoFocus() override {
#if BUILDFLAG(IS_STARBOARD)
    if (platform_event_source_) {
      platform_event_source_->DispatchFocusEvent(true);
    }
#else
    // On Android, focus is handled natively via Activity and View lifecycle
    // callbacks (e.g. CobaltActivity.onResume) which propagate directly to
    // Chromium's WindowAndroid.
#endif
    content::Shell::OnFocus();
  }

  void DoConceal() override {
    content::Shell::OnConceal();
    WaitForAck(PendingAck::kConceal);
  }

  void DoReveal() override {
    content::Shell::OnReveal();
    WaitForAck(PendingAck::kReveal);
  }

  void DoFreeze(base::OnceClosure callback) override {
    content::Shell::OnFreeze();
    WaitForAck(PendingAck::kCookieFlush);
    std::move(callback).Run();
#if BUILDFLAG(USE_EVERGREEN)
    cobalt::updater::UpdaterModule* updater_module =
        cobalt::updater::UpdaterModule::GetInstance();
    if (updater_module) {
      updater_module->Suspend();
    }
#endif
  }

  void DoUnfreeze() override {
    content::Shell::OnUnfreeze();
    WaitForAck(PendingAck::kUnfreeze);
#if BUILDFLAG(USE_EVERGREEN)
    cobalt::updater::UpdaterModule* updater_module =
        cobalt::updater::UpdaterModule::GetInstance();
    if (updater_module) {
      updater_module->Resume();
    }
#endif
  }

  void OnInput(const SbEvent* event) override {
    CHECK(is_running());
#if BUILDFLAG(IS_STARBOARD)
    if (platform_event_source_) {
      platform_event_source_->HandleEvent(event);
    }
#else
    // On Android, input is handled natively via standard View event listeners
    // (e.g. CobaltActivity.onKeyDown) which are converted directly to
    // Chromium Motion/KeyEvents.
#endif
  }

  void OnLink(const SbEvent* event) override {
    CHECK(is_running());
    auto link = static_cast<const char*>(event->data);
    if (link) {
      cobalt::browser::DeepLinkManager::GetInstance()->OnDeepLink(link);
    }
  }

  void OnLowMemory(const SbEvent* event) override {
    CHECK(is_running());
    base::MemoryPressureListener::NotifyMemoryPressure(
        base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);

    // Chromium internally calls Reclaim/ReclaimNormal at regular interval
    // to claim free memory. Using ReclaimAll is more aggressive.
    ::partition_alloc::MemoryReclaimer::Instance()->ReclaimAll();

    if (event->data) {
      auto mem_cb = reinterpret_cast<SbEventCallback>(event->data);
      mem_cb(nullptr);
    }
  }

  void OnAccessibilityTextToSpeechSettingsChanged(
      const SbEvent* event) override {
    CHECK(is_running());
    if (event->data) {
      auto* enabled = static_cast<const bool*>(event->data);
      cobalt::browser::H5vccAccessibilityManager::GetInstance()
          ->OnTextToSpeechStateChanged(*enabled);
    }
  }

  void OnWindowSizeChanged(const SbEvent* event) override {
    CHECK(is_running());
#if BUILDFLAG(IS_STARBOARD)
    if (platform_event_source_) {
      platform_event_source_->HandleWindowSizeChangedEvent(event);
    }
#else
    // On Android, window size changes are handled natively via View
    // onSizeChanged/onConfigurationChanged which update Chromium's
    // WindowAndroid and viewport directly.
#endif
  }

  void OnOsNetworkConnectedDisconnected(const SbEvent* event) override {
    CHECK(is_running());
#if BUILDFLAG(IS_STARBOARD)
    auto* notifier = content::GetNetworkChangeNotifier();
    if (notifier) {
      auto* passive_notifier =
          static_cast<net::NetworkChangeNotifierPassive*>(notifier);
      net::NetworkChangeNotifier::ConnectionType type =
          event->type == kSbEventTypeOsNetworkConnected
              ? net::NetworkChangeNotifier::CONNECTION_UNKNOWN
              : net::NetworkChangeNotifier::CONNECTION_NONE;
      net::NetworkChangeNotifier::ConnectionSubtype subtype =
          event->type == kSbEventTypeOsNetworkConnected
              ? net::NetworkChangeNotifier::SUBTYPE_UNKNOWN
              : net::NetworkChangeNotifier::SUBTYPE_NONE;
      passive_notifier->OnConnectionChanged(type);
      passive_notifier->OnConnectionSubtypeChanged(type, subtype);
      if (event->type == kSbEventTypeOsNetworkConnected) {
        passive_notifier->OnIPAddressChanged();
      }
    }
#endif
  }

  void OnDateTimeConfigurationChanged(const SbEvent* event) override {
    CHECK(is_running());
#if BUILDFLAG(IS_STARBOARD)
    device::NotifyTimeZoneChangeStarboard();
#else
    // Android handles network and time zone changes natively via
    // ConnectivityManager and TimeZoneChangeReceiver.
#endif
  }

 private:
  int Run(absl::optional<int64_t> startup_timestamp,
          bool is_visible,
          int argc,
          const char** argv,
          const char* initial_deep_link) {
    CreateMainDelegate(startup_timestamp, is_visible, initial_deep_link);
    content::ContentMainParams params(GetMainDelegate());
#if BUILDFLAG(IS_STARBOARD)
    cobalt::CommandLinePreprocessor init_cmd_line(argc, argv);
    const auto& init_argv = init_cmd_line.argv();
#if BUILDFLAG(COBALT_IS_RELEASE_BUILD)
    logging::SetMinLogLevel(logging::LOGGING_FATAL);
#endif
    std::vector<const char*> args;
    for (const auto& arg : init_argv) {
      args.push_back(arg.c_str());
    }
#endif
    if (initial_deep_link) {
      auto* manager = cobalt::browser::DeepLinkManager::GetInstance();
      manager->set_deep_link(initial_deep_link);
    }

    // This expression exists to ensure that we apply the argument overrides
    // only on the main process, not on spawned processes such as the zygote.
#if !BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(IS_STARBOARD)
    if ((argc >= 1 && !strcmp(argv[0], "/proc/self/exe")) ||
        ((argc >= 2) && !strcmp(argv[1], "--type=zygote"))) {
      params.argc = argc;
      params.argv = argv;
    } else {
      params.argc = args.size();
      params.argv = args.data();
    }
#else
    params.argc = argc;
    params.argv = argv;
#endif

    main_runner_ = GetContentMainRunner();
    return content::RunContentProcess(std::move(params), main_runner_);
#else
    return 0;
#endif
  }

  std::unique_ptr<base::AtExitManager> exit_manager_;
#if !BUILDFLAG(IS_ANDROID)
  content::ContentMainRunner* main_runner_ = nullptr;
#endif
  std::unique_ptr<cobalt::CobaltMainDelegate> content_main_delegate_;

#if BUILDFLAG(IS_STARBOARD)
  std::unique_ptr<ui::PlatformEventSourceStarboard> platform_event_source_;
#endif

  void OnCookieFlushComplete() {
    base::AutoLock lock(lock_);
    if (quit_closure_) {
      std::move(quit_closure_).Run();
    }
  }

  void WaitForAck(PendingAck ack_type) {
    base::ScopedAllowBaseSyncPrimitivesOutsideBlockingScope allow_blocking;
    base::AutoLock lock(lock_);
    pending_ack_ = ack_type;

    if (ack_type == PendingAck::kCookieFlush) {
      auto* client = cobalt::CobaltContentBrowserClient::Get();
      if (client) {
        client->FlushCookiesAndLocalStorage(
            base::BindOnce(&AppEventRunnerImpl::OnCookieFlushComplete,
                           base::Unretained(this)));
      } else {
        pending_ack_ = PendingAck::kNone;
        return;
      }
    } else {
      for (auto* web_contents : GetWebContents()) {
        CobaltLifecycleManager::GetInstance()->StartWaitingForAck(web_contents,
                                                                  ack_type);
      }
    }

    base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
    quit_closure_ = run_loop.QuitClosure();

    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), kTransitionTimeout);

    {
      base::AutoUnlock unlock(lock_);
      run_loop.Run();
    }
    pending_ack_ = PendingAck::kNone;
  }

  // CobaltLifecycleManagerObserver implementation.
  void OnAllFramesVisible(content::WebContents* web_contents) override {
    base::AutoLock lock(lock_);
    if (pending_ack_ == PendingAck::kReveal) {
      if (quit_closure_) {
        std::move(quit_closure_).Run();
      }
    }
  }

  void OnAllFramesConcealed(content::WebContents* web_contents) override {
    base::AutoLock lock(lock_);
    if (pending_ack_ == PendingAck::kConceal) {
      if (quit_closure_) {
        std::move(quit_closure_).Run();
      }
    }
  }

  void OnAllFramesBlurred(content::WebContents* web_contents) override {
    base::AutoLock lock(lock_);
    if (pending_ack_ == PendingAck::kBlur) {
      if (quit_closure_) {
        std::move(quit_closure_).Run();
      }
    }
  }

  void OnAllFramesResumed(content::WebContents* web_contents) override {
    base::AutoLock lock(lock_);
    if (pending_ack_ == PendingAck::kUnfreeze) {
      if (quit_closure_) {
        std::move(quit_closure_).Run();
      }
    }
  }

  mutable base::Lock lock_;
  base::OnceClosure quit_closure_;
  PendingAck pending_ack_ = PendingAck::kNone;
};

void AppEventRunner::OnStart(const SbEvent* event) {
  CHECK(!is_running());
  CHECK(!is_visible());
  CHECK(!is_focused());
  CHECK(is_frozen());
  set_is_running(true);
  set_is_visible(event->type == kSbEventTypeStart);
  set_is_focused(event->type == kSbEventTypeStart);
  set_is_frozen(false);
  DoStart(event);
}

void AppEventRunner::OnStop() {
  CHECK(is_running());
  CHECK(!is_visible());
  CHECK(!is_focused());
  CHECK(is_frozen());

  DoStop();

  set_is_running(false);
}

void AppEventRunner::OnBlur() {
  CHECK(is_running());
  CHECK(is_visible());
  CHECK(is_focused());

  DoBlur();

  set_is_focused(false);
}

void AppEventRunner::OnFocus() {
  CHECK(is_running());
  CHECK(is_visible());
  CHECK(!is_focused());

  DoFocus();

  set_is_focused(true);
}

void AppEventRunner::OnConceal() {
  CHECK(is_running());
  CHECK(is_visible());
  CHECK(!is_focused());

  DoConceal();

  set_is_visible(false);
}

void AppEventRunner::OnReveal() {
  CHECK(is_running());
  CHECK(!is_visible());
  CHECK(!is_focused());

  DoReveal();

  set_is_visible(true);
}

void AppEventRunner::OnFreeze(base::OnceClosure callback) {
  CHECK(is_running());
  CHECK(!is_visible());
  CHECK(!is_focused());
  CHECK(!is_frozen());

  DoFreeze(base::BindOnce(
      [](base::OnceClosure cb, AppEventRunner* runner) {
        runner->set_is_frozen(true);
        std::move(cb).Run();
      },
      std::move(callback), base::Unretained(this)));
}

void AppEventRunner::OnUnfreeze() {
  CHECK(is_running());
  CHECK(!is_visible());
  CHECK(!is_focused());
  CHECK(is_frozen());

  DoUnfreeze();
  set_is_frozen(false);
}

std::unique_ptr<AppEventRunner> AppEventRunner::Create() {
  return std::make_unique<AppEventRunnerImpl>();
}

}  // namespace cobalt
