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

#ifndef COBALT_BROWSER_COBALT_WEB_CONTENTS_OBSERVER_H_
#define COBALT_BROWSER_COBALT_WEB_CONTENTS_OBSERVER_H_

#include <map>

#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "cobalt/browser/lifecycle/public/mojom/cobalt_lifecycle.mojom.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/remote.h"

#if BUILDFLAG(IS_STARBOARD)
#include "starboard/system.h"
#endif

namespace cobalt {

// This class is used to observe WebContents lifecycles within
// CobaltContentBrowserClient. Its primary purpose is to dispatch web events.
// Most WebContentsObserver functionalities should be implemented in
// cobalt/shell/browser/shell.{h,cc} instead. This class should only contain
// logic essential for the browser client's core responsibilities.
class CobaltWebContentsObserver : public content::WebContentsObserver {
 public:
  CobaltWebContentsObserver(content::WebContents* web_contents);

  CobaltWebContentsObserver(const CobaltWebContentsObserver&) = delete;
  CobaltWebContentsObserver& operator=(const CobaltWebContentsObserver&) =
      delete;

  ~CobaltWebContentsObserver() override;

 public:
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  virtual void RaisePlatformError(int64_t navigation_id,
                                  const std::string& url);
  virtual void SetStartupDiagnosisInfo(const char* key, const char* value);

 protected:
  void SetTimerForTestInternal(std::unique_ptr<base::OneShotTimer> timer);

 private:
  void OnNavigationTimeout(int64_t navigation_id, const std::string& url);
  std::unique_ptr<base::OneShotTimer> timeout_timer_;
  base::WeakPtrFactory<CobaltWebContentsObserver> weak_factory_{this};
  std::map<content::RenderFrameHost*,
           mojo::Remote<cobalt::mojom::CobaltLifecycleController>>
      controllers_;
  int64_t latest_navigation_id_ = 0;
#if BUILDFLAG(IS_ANDROIDTV) || BUILDFLAG(IS_STARBOARD)
  int platform_error_raised_count_ = 0;
#endif  // BUILDFLAG(IS_ANDROIDTV)
#if BUILDFLAG(IS_STARBOARD)
  class PlatformErrorBridge;
  PlatformErrorBridge* pending_platform_error_bridge_ = nullptr;
  static void HandlePlatformErrorResponse(
      SbSystemPlatformErrorResponse response,
      void* user_data);
  void OnPlatformErrorResponse(SbSystemPlatformErrorResponse response,
                               int64_t navigation_id);
  bool is_platform_error_showing_ = false;
#endif  // BUILDFLAG(IS_STARBOARD)
};

}  // namespace cobalt

#endif  // COBALT_BROWSER_COBALT_WEB_CONTENTS_OBSERVER_H_
