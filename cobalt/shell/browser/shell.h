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

#ifndef COBALT_SHELL_BROWSER_SHELL_H_
#define COBALT_SHELL_BROWSER_SHELL_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "build/build_config.h"
#include "cobalt/shell/browser/shell_platform_delegate.h"
#include "cobalt/shell/browser/splash_screen_web_contents_delegate.h"
#include "cobalt/shell/browser/splash_screen_web_contents_observer.h"
#include "components/js_injection/browser/js_communication_host.h"
#include "content/public/browser/session_storage_namespace.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "ipc/ipc_channel.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_widget_types.h"

class GURL;

namespace cobalt {
class AppEventDelegateTest;
class AppEventRunnerTest;
}  // namespace cobalt

namespace content {
class BrowserContext;
class JavaScriptDialogManager;
class ShellDevToolsFrontend;
class SiteInstance;
class WebContents;
class RenderFrameHost;

// This represents one window of Cobalt, i.e. all the UI including
// buttons and url bar, as well as the web content area.
class Shell : public WebContentsDelegate, public WebContentsObserver {
 public:
  ~Shell() override;

  void LoadURL(const GURL& url);
  void LoadURLForFrame(const GURL& url,
                       const std::string& frame_name,
                       ui::PageTransition);
  void LoadDataWithBaseURL(const GURL& url,
                           const std::string& data,
                           const GURL& base_url);

#if BUILDFLAG(IS_ANDROID)
  // Android-only path to allow loading long data strings.
  void LoadDataAsStringWithBaseURL(const GURL& url,
                                   const std::string& data,
                                   const GURL& base_url);
#endif
  void GoBackOrForward(int offset);
  void Reload();
  void ReloadBypassingCache();
  void Stop();
  void UpdateNavigationControls(bool should_show_loading_ui);
  void Close();
  void ShowDevTools();
  void CloseDevTools();
  // Resizes the web content view to the given dimensions.
  void ResizeWebContentForTests(const gfx::Size& content_size);

  void LoadSplashScreenWebContents();

  // Do one-time initialization at application startup. This must be matched
  // with a Shell::Shutdown() at application termination, where |platform|
  // will be released.
  static void Initialize(std::unique_ptr<ShellPlatformDelegate> platform,
                         bool is_visible);

  // Closes all windows, pumps teardown tasks and signal the main message loop
  // to quit.
  static void Shutdown();  // Idempotent, can be called twice.

  static ShellPlatformDelegate* GetPlatform();

  static void OnBlur();
  static void OnFocus();
  static void OnConceal();
  static void OnReveal();
  static void OnFreeze();
  static void OnUnfreeze();
  static void OnStop();

  static Shell* CreateNewWindow(
      BrowserContext* browser_context,
      const GURL& url,
      const scoped_refptr<SiteInstance>& site_instance,
      const gfx::Size& initial_size,
      const bool create_splash_screen_web_contents = false,
      const std::string& deep_link = "");

  // Returns the Shell object corresponding to the given WebContents.
  static Shell* FromWebContents(WebContents* web_contents);

  // Returns the currently open windows.
  static std::vector<Shell*>& windows() { return windows_; }

  // Stores the supplied |quit_closure|, to be run when the last Shell instance
  // is destroyed.
  static void SetMainMessageLoopQuitClosure(base::OnceClosure quit_closure);

  // Used by the WebTestControlHost to stop the message loop before closing all
  // windows, for specific tests. Has no effect if the loop is already quitting.
  static void QuitMainMessageLoopForTesting();

  // Used for content_browsertests. Called once.
  static void SetShellCreatedCallback(
      base::OnceCallback<void(Shell*)> shell_created_callback);

  static bool ShouldHideToolbar();

  WebContents* web_contents() const { return web_contents_.get(); }

  void Focus();
  bool pending_focus() const { return pending_focus_; }

  WebContents* splash_screen_web_contents() const {
    return splash_screen_web_contents_.get();
  }

  bool skip_for_testing() const { return skip_for_testing_; }

#if !BUILDFLAG(IS_ANDROID)
  gfx::NativeWindow window();
#endif

  // WebContentsDelegate
  WebContents* OpenURLFromTab(
      WebContents* source,
      const OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  WebContents* AddNewContents(
      WebContents* source,
      std::unique_ptr<WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;
  void LoadingStateChanged(WebContents* source,
                           bool should_show_loading_ui) override;
  void EnterFullscreenModeForTab(
      RenderFrameHost* requesting_frame,
      const blink::mojom::FullscreenOptions& options) override;
  void ExitFullscreenModeForTab(WebContents* web_contents) override;
  bool IsFullscreenForTabOrPending(const WebContents* web_contents) override;
  blink::mojom::DisplayMode GetDisplayMode(
      const WebContents* web_contents) override;
  void RequestPointerLock(WebContents* web_contents,
                          bool user_gesture,
                          bool last_unlocked_by_target) override;
  void CloseContents(WebContents* source) override;
  bool CanOverscrollContent() override;
  void NavigationStateChanged(WebContents* source,
                              InvalidateTypes changed_flags) override;
  JavaScriptDialogManager* GetJavaScriptDialogManager(
      WebContents* source) override;
  bool DidAddMessageToConsole(WebContents* source,
                              blink::mojom::ConsoleMessageLevel log_level,
                              const std::u16string& message,
                              int32_t line_no,
                              const std::u16string& source_id) override;
  void RendererUnresponsive(
      WebContents* source,
      RenderWidgetHost* render_widget_host,
      base::RepeatingClosure hang_monitor_restarter) override;
  void ActivateContents(WebContents* contents) override;
  bool IsBackForwardCacheSupported(WebContents& contents) override;
  PreloadingEligibility IsPrerender2Supported(
      WebContents& web_contents,
      PreloadingTriggerType trigger_type) override;
  bool ShouldAllowRunningInsecureContent(WebContents* web_contents,
                                         bool allowed_per_prefs,
                                         const url::Origin& origin,
                                         const GURL& resource_url) override;
  PictureInPictureResult EnterPictureInPicture(
      WebContents* web_contents) override;
  void ExitPictureInPicture() override;
  bool ShouldResumeRequestsForCreatedWindow() override;
  void SetContentsBounds(WebContents* source, const gfx::Rect& bounds) override;
  void RequestMediaAccessPermission(WebContents*,
                                    const MediaStreamRequest&,
                                    MediaResponseCallback) override;
  bool CheckMediaAccessPermission(RenderFrameHost*,
                                  const url::Origin&,
                                  blink::mojom::MediaStreamType) override;
  bool ShouldFocusPageAfterCrash(WebContents* source) override;

  static gfx::Size GetShellDefaultSize();

  void set_delay_popup_contents_delegate_for_testing(bool delay) {
    delay_popup_contents_delegate_for_testing_ = delay;
  }

 private:
  class DevToolsWebContentsObserver;

  friend class TestShell;
  friend class SplashScreenTest;
  friend class LifecycleTest;
  friend class cobalt::AppEventDelegateTest;
  friend class cobalt::AppEventRunnerTest;

  enum State {
    STATE_SPLASH_SCREEN_UNINITIALIZED,
    STATE_SPLASH_SCREEN_INITIALIZED,  // Initialize Splash Screen WebContents.
    STATE_SPLASH_SCREEN_STARTED,      // Start Splash Screen WebContents.
    STATE_SPLASH_SCREEN_ENDED         // End Splash Screen WebContents.
  };

  // Helper to create a new Shell given a newly created WebContents.
  static Shell* CreateShell(
      std::unique_ptr<WebContents> web_contents,
      std::unique_ptr<WebContents> splash_screen_web_contents,
      const gfx::Size& initial_size,
      bool should_set_delegate,
      const std::string& topic = "");

  // Adjust the size when Blink sends 0 for width and/or height.
  // This happens when Blink requests a default-sized window.
  static gfx::Size AdjustWindowSize(const gfx::Size& initial_size);

  // Helper method for the two public LoadData methods.
  void LoadDataWithBaseURLInternal(const GURL& url,
                                   const std::string& data,
                                   const GURL& base_url,
                                   bool load_as_string);

  gfx::NativeView GetContentView();

  void ToggleFullscreenModeForTab(WebContents* web_contents,
                                  bool enter_fullscreen);

  Shell(std::unique_ptr<WebContents> web_contents,
        std::unique_ptr<WebContents> splash_screen_web_contents,
        bool should_set_delegate,
        const std::string& topic = "",
        bool skip_for_testing = false);

  static void FinishShellInitialization(Shell* shell);

  // WebContentsObserver
  void OnVisibilityChanged(Visibility visibility) override;
  void LoadProgressChanged(double progress) override;
  void TitleWasSet(NavigationEntry* entry) override;
  void RenderFrameCreated(RenderFrameHost* frame_host) override;
  void PrimaryMainDocumentElementAvailable() override;
  void DidFinishLoad(RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override;
  void DidStartNavigation(NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(NavigationHandle* navigation_handle) override;
  void DidStartLoading() override;
  void DidStopLoading() override;

  void RegisterInjectedJavaScript();
  void SwitchToMainWebContents();
  void ScheduleSwitchToMainWebContents();
  void ClosingSplashScreenWebContents();
  void OnSplashScreenLoadComplete();

  std::unique_ptr<JavaScriptDialogManager> dialog_manager_;

  std::unique_ptr<WebContents> web_contents_;
  std::unique_ptr<WebContents> splash_screen_web_contents_;
  State splash_state_;
  const std::string splash_topic_;
  bool skip_for_testing_;
  bool is_video_splash_screen_;
  bool is_main_frame_loaded_ = false;
  bool has_switched_to_main_frame_ = false;
  base::TimeTicks splash_screen_start_time_;

  base::WeakPtr<ShellDevToolsFrontend> devtools_frontend_;

  bool is_fullscreen_ = false;
  gfx::Size content_size_;

  // Set to true if Focus() is requested while the WebContents is not yet
  // visible. This handles a race condition in the Cobalt Reveal -> Focus
  // sequence where Aura ignores focus requests for hidden windows. The focus
  // will be applied as soon as the visibility changes to VISIBLE.
  bool pending_focus_ = false;

  bool delay_popup_contents_delegate_for_testing_ = false;

  std::unique_ptr<js_injection::JsCommunicationHost> js_communication_host_;

  // TODO: (cobalt b/468059482) each shell holds a single WebContents.
  std::unique_ptr<SplashScreenWebContentsObserver>
      splash_screen_web_contents_observer_;
  std::unique_ptr<SplashScreenWebContentsDelegate>
      splash_screen_web_contents_delegate_;

  // A container of all the open windows. We use a vector so we can keep track
  // of ordering.
  static std::vector<Shell*> windows_;

  static base::OnceCallback<void(Shell*)> shell_created_callback_;

  // NOTE: Do not add member variables after weak_factory_
  // It should be the first one destroyed among all members.
  // See base/memory/weak_ptr.h.
  base::WeakPtrFactory<Shell> weak_factory_{this};
};

}  // namespace content

#endif  // COBALT_SHELL_BROWSER_SHELL_H_
