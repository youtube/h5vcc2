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

#include "cobalt/browser/cobalt_web_contents_observer.h"

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "cobalt/browser/lifecycle/cobalt_lifecycle_manager.h"
#include "cobalt/browser/lifecycle/public/mojom/cobalt_lifecycle.mojom.h"
#include "cobalt/build/configs/buildflags.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/net_errors.h"
#include "services/service_manager/public/cpp/interface_provider.h"

#if BUILDFLAG(IS_ANDROIDTV)
#include "starboard/android/shared/starboard_bridge.h"
#endif  // BUILDFLAG(IS_ANDROIDTV)

#if BUILDFLAG(IS_IOS_TVOS)
#include "cobalt/browser/tvos/network_error_handler.h"
#endif  // BUILDFLAG(IS_IOS_TVOS)

#if BUILDFLAG(IS_STARBOARD)
#include <atomic>
#endif

#if BUILDFLAG(ENABLE_IN_APP_DIAL)
#include "cobalt/browser/dial/dial_service.h"
#endif  // BUILDFLAG(ENABLE_IN_APP_DIAL)

namespace cobalt {

namespace {
const int kNavigationTimeoutSeconds = 30;
#if BUILDFLAG(IS_ANDROIDTV)
const int kJniErrorTypeConnectionError = 0;
#endif  // BUILDFLAG(IS_ANDROIDTV)
}  // namespace

#if BUILDFLAG(IS_STARBOARD)
class CobaltWebContentsObserver::PlatformErrorBridge {
 public:
  PlatformErrorBridge(base::WeakPtr<CobaltWebContentsObserver> observer,
                      int64_t navigation_id)
      : observer_(observer),
        navigation_id_(navigation_id),
        task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {}
  ~PlatformErrorBridge() = default;

  void Run(SbSystemPlatformErrorResponse response) {
    if (has_run_.exchange(true)) {
      return;
    }
    if (task_runner_->RunsTasksInCurrentSequence()) {
      RunOnSequence(response);
    } else {
      task_runner_->PostTask(FROM_HERE,
                             base::BindOnce(&PlatformErrorBridge::RunOnSequence,
                                            base::Unretained(this), response));
    }
  }

  void set_navigation_id(int64_t navigation_id) {
    DCHECK(task_runner_->RunsTasksInCurrentSequence());
    navigation_id_ = navigation_id;
  }

 private:
  void RunOnSequence(SbSystemPlatformErrorResponse response) {
    DCHECK(task_runner_->RunsTasksInCurrentSequence());
    if (observer_) {
      observer_->OnPlatformErrorResponse(response, navigation_id_);
    }
    delete this;
  }

  base::WeakPtr<CobaltWebContentsObserver> observer_;
  int64_t navigation_id_;
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;
  std::atomic<bool> has_run_{false};
};
#endif  // BUILDFLAG(IS_STARBOARD)

CobaltWebContentsObserver::CobaltWebContentsObserver(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {
  timeout_timer_ = std::make_unique<base::OneShotTimer>();

  // Check if main frame is already created.
  CHECK(web_contents);
  content::RenderFrameHost* main_frame = web_contents->GetPrimaryMainFrame();
  if (main_frame && main_frame->IsRenderFrameLive()) {
    mojo::Remote<cobalt::mojom::CobaltLifecycleController> controller;
    main_frame->GetRemoteInterfaces()->GetInterface(
        controller.BindNewPipeAndPassReceiver());

    // Create observer and pass to renderer.
    mojo::PendingRemote<cobalt::mojom::CobaltLifecycleObserver> observer_remote;
    CobaltLifecycleManager::GetInstance()->BindReceiver(
        main_frame, observer_remote.InitWithNewPipeAndPassReceiver());
    controller->SetObserver(std::move(observer_remote));

    controllers_[main_frame] = std::move(controller);
  }
}

CobaltWebContentsObserver::~CobaltWebContentsObserver() = default;

void CobaltWebContentsObserver::SetTimerForTestInternal(
    std::unique_ptr<base::OneShotTimer> timer) {
  timeout_timer_ = std::move(timer);
}

void CobaltWebContentsObserver::DidStartNavigation(
    content::NavigationHandle* handle) {
  // M138 refinement: Ensure we don't restart timers for subframes or
  // background prerenders that haven't been activated yet.
  if (!handle->IsInPrimaryMainFrame() ||
      handle->IsServedFromBackForwardCache()) {
    LOG(INFO) << "DidStartNavigation: Skipping timer for " << handle->GetURL()
              << " (Not primary mainframe or served from BFCache)";
    return;
  }

  latest_navigation_id_ = handle->GetNavigationId();

  // Start a navigation timer with a timeout callback to raise a
  // network error dialog
  timeout_timer_->Stop();
  timeout_timer_->Start(
      FROM_HERE, base::Seconds(kNavigationTimeoutSeconds),
      base::BindOnce(&CobaltWebContentsObserver::OnNavigationTimeout,
                     weak_factory_.GetWeakPtr(), latest_navigation_id_,
                     handle->GetURL().spec()));
}

// Opting for WebContentsObserver::DidFinishNavigation() over
// WebContentsObserver::PrimaryPageChanged as the network check can't
// assume HasCommitted() is true. Doing so would not catch network
// errors that are thrown before a navigation commits such as
// net::ERR_CONNECTION_TIMED_OUT and net::ERR_NAME_NOT_RESOLVED.
void CobaltWebContentsObserver::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }
  if (navigation_handle->GetNavigationId() != latest_navigation_id_) {
    return;
  }

  timeout_timer_->Stop();
  const auto net_error_code = navigation_handle->GetNetErrorCode();
  if (net_error_code != net::OK && net_error_code != net::ERR_ABORTED) {
    base::UmaHistogramBoolean("Cobalt.WebContentsObserver.FailedNavigation",
                              true);
    base::UmaHistogramSparse("Cobalt.WebContentsObserver.FailedNavigationError",
                             -net_error_code);
    LOG(INFO) << "DidFinishNavigation: Raising platform error with code: "
              << net::ErrorToString(net_error_code);
    SetStartupDiagnosisInfo("navigation_error",
                            net::ErrorToString(net_error_code).c_str());
    RaisePlatformError(navigation_handle->GetNavigationId(),
                       navigation_handle->GetURL().spec());
  } else if (net_error_code == net::OK) {
    base::UmaHistogramBoolean("Cobalt.WebContentsObserver.FailedNavigation",
                              false);
#if BUILDFLAG(IS_ANDROIDTV) || BUILDFLAG(IS_STARBOARD)
    platform_error_raised_count_ = 0;
#endif
  }
}

void CobaltWebContentsObserver::OnVisibilityChanged(
    content::Visibility visibility) {
#if BUILDFLAG(ENABLE_IN_APP_DIAL)
  // This is similar to the C25 behavior of restarting the DIAL servers when an
  // Unfreeze event was sent by Starboard.
  if (visibility == content::Visibility::HIDDEN) {
    in_app_dial::DialService::GetInstance()->Stop();
  } else {
    in_app_dial::DialService::GetInstance()->Start();
  }
#endif
}

void CobaltWebContentsObserver::SetStartupDiagnosisInfo(const char* key,
                                                        const char* value) {
#if BUILDFLAG(IS_ANDROID)
  starboard::StarboardBridge::GetInstance()->SetStartupDiagnosisInfo(key,
                                                                     value);
#endif
}

void CobaltWebContentsObserver::OnNavigationTimeout(int64_t navigation_id,
                                                    const std::string& url) {
  base::UmaHistogramBoolean("Cobalt.Network.NavigationTimeout", true);
  RaisePlatformError(navigation_id, url);
}

void CobaltWebContentsObserver::RaisePlatformError(int64_t navigation_id,
                                                   const std::string& url) {
  if (navigation_id != latest_navigation_id_) {
    LOG(INFO) << "Ignoring stale platform error request for navigation "
              << navigation_id;
    return;
  }
#if BUILDFLAG(IS_ANDROIDTV)
  JNIEnv* env = base::android::AttachCurrentThread();
  auto* starboard_bridge = starboard::StarboardBridge::GetInstance();

  // Don't raise a new platform error if one is already showing
  if (starboard_bridge->IsPlatformErrorShowing(env)) {
    return;
  }
  platform_error_raised_count_++;
  base::UmaHistogramCounts100("Cobalt.Network.PlatformErrorCount",
                              platform_error_raised_count_);
  starboard_bridge->RaisePlatformError(env, kJniErrorTypeConnectionError, 0,
                                       url);
#elif BUILDFLAG(IS_IOS_TVOS)
  ShowPlatformErrorDialog(web_contents());
#elif BUILDFLAG(IS_STARBOARD)
  if (is_platform_error_showing_) {
    if (pending_platform_error_bridge_) {
      pending_platform_error_bridge_->set_navigation_id(navigation_id);
    }
    return;
  }
  is_platform_error_showing_ = true;
  platform_error_raised_count_++;
  base::UmaHistogramCounts100("Cobalt.Network.PlatformErrorCount",
                              platform_error_raised_count_);
  auto* bridge =
      new PlatformErrorBridge(weak_factory_.GetWeakPtr(), navigation_id);
  pending_platform_error_bridge_ = bridge;
  if (!SbSystemRaisePlatformError(
          kSbSystemPlatformErrorTypeConnectionError,
          &CobaltWebContentsObserver::HandlePlatformErrorResponse, bridge)) {
    LOG(WARNING) << "Did not handle platform error";
    is_platform_error_showing_ = false;
    if (pending_platform_error_bridge_ == bridge) {
      delete bridge;
      pending_platform_error_bridge_ = nullptr;
    }
  }
#else
  NOTIMPLEMENTED();
#endif  // BUILDFLAG(IS_ANDROIDTV)
}

#if BUILDFLAG(IS_STARBOARD)
// static
void CobaltWebContentsObserver::HandlePlatformErrorResponse(
    SbSystemPlatformErrorResponse response,
    void* user_data) {
  auto* bridge = static_cast<PlatformErrorBridge*>(user_data);
  bridge->Run(response);
}

void CobaltWebContentsObserver::OnPlatformErrorResponse(
    SbSystemPlatformErrorResponse response,
    int64_t navigation_id) {
  pending_platform_error_bridge_ = nullptr;
  is_platform_error_showing_ = false;
  if (navigation_id != latest_navigation_id_) {
    LOG(INFO) << "Ignoring stale platform error response for navigation "
              << navigation_id;
    return;
  }
  if (response == kSbSystemPlatformErrorResponsePositive) {
    LOG(INFO) << "Platform error response is POSITIVE. Reloading...";
    if (web_contents()) {
      web_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                             /*check_for_repost=*/false);
    }
  } else {
    LOG(INFO) << "Platform error response is NEGATIVE/CANCEL. Stopping app...";
    SbSystemRequestStop(0);
  }
}
#endif  // BUILDFLAG(IS_STARBOARD)

}  // namespace cobalt
