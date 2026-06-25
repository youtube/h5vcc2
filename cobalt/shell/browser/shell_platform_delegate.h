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

#ifndef COBALT_SHELL_BROWSER_SHELL_PLATFORM_DELEGATE_H_
#define COBALT_SHELL_BROWSER_SHELL_PLATFORM_DELEGATE_H_

#include <memory>
#include <set>
#include <string>

#include "base/containers/flat_map.h"
#include "build/build_config.h"
#include "cobalt/browser/lifecycle/cobalt_lifecycle_manager.h"
#include "cobalt/build/configs/buildflags.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_widget_types.h"

#if defined(USE_AURA) && defined(SHELL_USE_TOOLKIT_VIEWS)
namespace views {
class ViewsDelegate;
}
#endif

#if BUILDFLAG(IS_APPLE)
#include "ui/display/screen.h"
#endif

#if BUILDFLAG(ENABLE_NATIVE_ON_SCREEN_KEYBOARD)
#include "base/memory/weak_ptr.h"

namespace on_screen_keyboard {
class PlatformOnScreenKeyboard;
}  // namespace on_screen_keyboard
#endif  // BUILDFLAG(ENABLE_NATIVE_ON_SCREEN_KEYBOARD)

class GURL;

namespace content {
class JavaScriptDialogManager;
class Shell;
class ShellPlatformDataAura;
class ShellTestBase;
class RenderFrameHost;
class WebContents;

class ShellPlatformDelegate : public cobalt::CobaltLifecycleManagerObserver {
 public:
  enum UIControl { BACK_BUTTON, FORWARD_BUTTON, STOP_BUTTON };

  ShellPlatformDelegate();
  virtual ~ShellPlatformDelegate();

  // Helper for one time initialization of application.
  virtual void Initialize(const gfx::Size& default_window_size,
                          bool is_visible);

  // Returns true if the application is in a visible state.
  bool IsVisible() const;

  // Lifecycle signals called from the application.
  virtual void OnBlur();
  virtual void OnFocus();
  virtual void OnConceal();
  virtual void OnReveal();
  virtual void OnFreeze();
  virtual void OnUnfreeze();
  virtual void OnStop();

  virtual void RevealShell(Shell* shell);
#if defined(USE_AURA) && BUILDFLAG(IS_STARBOARD)
  virtual void MapWindowShell(Shell* shell);
#endif
  virtual void ConcealShell(Shell* shell);

  // Called after creating a Shell instance, with its initial size.
  virtual void CreatePlatformWindow(Shell* shell,
                                    const gfx::Size& initial_size);

  // Notifies of a top-level or nested web contents being created for, or
  // attached to, the Shell.
  virtual void DidCreateOrAttachWebContents(Shell* shell,
                                            WebContents* web_contents);

  // Called from the Shell destructor to let each platform do any necessary
  // cleanup.
  virtual void CleanUp(Shell* shell);

  // Called from the Shell destructor after destroying the last one. This is
  // usually a good time to call Shell::Shutdown().
  virtual void DidCloseLastWindow();

  // Links the WebContents into the newly created window.
  virtual void SetContents(Shell* shell);

  // Load the splash screen WebContents.
  virtual void LoadSplashScreenContents(Shell* shell);

  // Update WebContents into the newly created window.
  virtual void UpdateContents(Shell* shell);

  // Resize the web contents in the shell window to the given size.
  virtual void ResizeWebContent(Shell* shell, const gfx::Size& content_size);

  // Enable/disable a button.
  virtual void EnableUIControl(Shell* shell,
                               UIControl control,
                               bool is_enabled);

  // Updates the url in the url bar.
  virtual void SetAddressBarURL(Shell* shell, const GURL& url);

  // Sets whether the spinner is spinning.
  virtual void SetIsLoading(Shell* shell, bool loading);

  // Set the title of shell window
  virtual void SetTitle(Shell* shell, const std::u16string& title);

  // Called when the main frame is created in the renderer process; forwarded
  // from WebContentsObserver. If navigation creates a new main frame, this may
  // occur more than once.
  virtual void MainFrameCreated(Shell* shell);
  virtual void OnPageVisibilityVisible(Shell* shell);
  bool IsWaitingForRevealAck() const;
  void ClearWaitingForRevealAck();

  // Allows platforms to override the JavascriptDialogManager. By default
  // returns null, which signals that the Shell should use its own instance.
  virtual std::unique_ptr<JavaScriptDialogManager>
  CreateJavaScriptDialogManager(Shell* shell);

  // Requests handling of locking the mouse pointer. This returns true if the
  // request has been handled, otherwise false.
  virtual bool HandlePointerLockRequest(Shell* shell,
                                        WebContents* web_contents,
                                        bool user_gesture,
                                        bool last_unlocked_by_target);

  // Allows platforms to prevent running insecure content. By default returns
  // false, only allowing what Shell allows on its own.
  virtual bool ShouldAllowRunningInsecureContent(Shell* shell);

  // Destroy the Shell. Returns true if the ShellPlatformDelegate did the
  // destruction. Returns false if the Shell should destroy itself.
  virtual bool DestroyShell(Shell* shell);

  void AddPreviouslyVisibleWebContentsForTesting(
      content::WebContents* web_contents);

#if !BUILDFLAG(IS_ANDROID)
  // Returns the native window. Valid after calling CreatePlatformWindow().
  virtual gfx::NativeWindow GetNativeWindow(Shell* shell);
#endif

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  void ToggleFullscreenModeForTab(Shell* shell,
                                  WebContents* web_contents,
                                  bool enter_fullscreen);

  bool IsFullscreenForTabOrPending(Shell* shell,
                                   const WebContents* web_contents) const;
#endif

#if BUILDFLAG(IS_ANDROID)
  // Forwarded from WebContentsDelegate.
  void SetOverlayMode(Shell* shell, bool use_overlay_mode);

  // Forwarded from WebContentsObserver.
  void LoadProgressChanged(Shell* shell, double progress);

  void SetSkipForTesting(bool skip_for_testing) {
    skip_for_testing_ = skip_for_testing;
  }
#endif

#if BUILDFLAG(ENABLE_NATIVE_ON_SCREEN_KEYBOARD)
  base::WeakPtr<on_screen_keyboard::PlatformOnScreenKeyboard>
  GetOrCreatePlatformOnScreenKeyboard(Shell* shell);
#endif  // BUILDFLAG(ENABLE_NATIVE_ON_SCREEN_KEYBOARD)

 protected:
  void CreatePlatformWindowInternal(Shell* shell,
                                    const gfx::Size& initial_size);

#if defined(USE_AURA) && defined(SHELL_USE_TOOLKIT_VIEWS)
  // Allows the test subclasses to override the ViewsDelegate.
  virtual std::unique_ptr<views::ViewsDelegate> CreateViewsDelegate();
#endif
#if defined(USE_AURA) && !defined(SHELL_USE_TOOLKIT_VIEWS)
  // Helper to avoid duplicating aura's ShellPlatformDelegate in web tests. If
  // this hack gets expanded to become more expansive then we should just
  // duplicate the aura ShellPlatformDelegate code to the web test code impl in
  // WebTestShellPlatformDelegate.
  ShellPlatformDataAura* GetShellPlatformDataAura();
#endif

 private:
  // CobaltLifecycleManagerObserver implementation.
#if defined(USE_AURA) && BUILDFLAG(IS_STARBOARD)
  void OnProactiveMapWindow(content::WebContents* web_contents) override;
#endif
  void OnAllFramesVisible(content::WebContents* web_contents) override;
  void OnAllFramesConcealed(content::WebContents* web_contents) override;

  // Flag to remember that an OS-initiated focus event arrived while we were
  // waiting for Reveal ACK. If true, focus will be applied to the window
  // as soon as OnAllFramesVisible is called.
  bool deferred_focus_ = false;
  friend class ShellTestBase;
  friend class MockShellPlatformDelegate;
#if BUILDFLAG(IS_APPLE)
  std::unique_ptr<display::ScopedNativeScreen> screen_;
#endif
  // Data held for each Shell instance, since there is one ShellPlatformDelegate
  // for the whole browser process (shared across Shells). This is defined for
  // each platform implementation.
  struct ShellData;
  // Holds an instance of ShellData for each Shell.
  base::flat_map<Shell*, ShellData> shell_data_map_;

  bool is_visible_ = true;
  // This flag tracks whether we are waiting for the web app to acknowledge
  // becoming visible (Reveal ACK). We need this local copy because the
  // source of truth in PlatformWindowStarboard is not accessible during
  // early resume stages (root_window is null).
  bool waiting_for_reveal_ack_ = false;

  class WebContentsTracker;
  struct WebContentsTrackerDeleter {
    void operator()(WebContentsTracker* ptr) const;
  };

  // Set of WebContents that were visible before the application was concealed.
  // This is used on reveal to decide which WebContents we should wait for
  // Reveal ACK from. We only wait for those that were active before.
  // The map stores self-unregistering observers to guarantee clean container
  // state upon WebContents destruction.
  base::flat_map<content::WebContents*,
                 std::unique_ptr<WebContentsTracker, WebContentsTrackerDeleter>>
      previously_visible_web_contents_;

  void TrackPreviouslyVisibleWebContents(content::WebContents* web_contents);
  void RemovePreviouslyVisibleWebContents(content::WebContents* web_contents);
  friend class WebContentsTracker;

  // Data held in ShellPlatformDelegate that is shared between all Shells. This
  // is created in Initialize(), and is defined for each platform
  // implementation.
  struct PlatformData;
  std::unique_ptr<PlatformData> platform_;

  bool skip_for_testing_ = false;
};

}  // namespace content

#endif  // COBALT_SHELL_BROWSER_SHELL_PLATFORM_DELEGATE_H_
