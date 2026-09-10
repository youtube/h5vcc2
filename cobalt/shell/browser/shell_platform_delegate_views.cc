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

// clang-format off
#include "cobalt/shell/browser/shell_platform_delegate.h"
// clang-format on

#include <stddef.h>

#include <algorithm>
#include <array>
#include <memory>

#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "cobalt/shell/browser/cobalt_views_delegate.h"
#include "cobalt/shell/browser/shell.h"
#include "content/public/browser/context_factory.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_tree_host.h"
#include "ui/aura/window_tree_host_platform.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/color/color_id.h"
#include "ui/compositor/compositor.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/views/background.h"
#include "ui/views/controls/webview/web_contents_set_background_color.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/wm_state.h"

#if defined(USE_AURA) && BUILDFLAG(IS_STARBOARD)
#include "ui/ozone/platform/starboard/platform_window_starboard.h"
#endif

namespace content {

namespace {
#if BUILDFLAG(IS_STARBOARD)
bool CheckAndHandleRevealState(WebContents* web_contents) {
  gfx::NativeView window = web_contents->GetNativeView();
  if (!window) {
    return false;
  }
  aura::WindowTreeHost* host = window->GetHost();
  if (!host) {
    return false;
  }
  auto* host_platform = static_cast<aura::WindowTreeHostPlatform*>(host);
  ui::PlatformWindow* platform_window = host_platform->platform_window();
  if (!platform_window) {
    return false;
  }

#if defined(USE_AURA) && BUILDFLAG(IS_STARBOARD)
  auto* pw_starboard =
      static_cast<ui::PlatformWindowStarboard*>(platform_window);
  bool is_waiting = pw_starboard->IsWaitingForRevealAck() ||
                    content::Shell::GetPlatform()->IsWaitingForRevealAck();
  if (is_waiting && !pw_starboard->IsWaitingForRevealAck()) {
    pw_starboard->SetWaitingForRevealAck(true);
  }
  return is_waiting;
#else
  return content::Shell::GetPlatform()->IsWaitingForRevealAck();
#endif
}
#endif
}  // namespace

struct ShellPlatformDelegate::ShellData {
  gfx::Size content_size;
  // Self-owned Widget, destroyed through CloseNow().
  raw_ptr<views::Widget> window_widget = nullptr;
  gfx::Size initial_size_;
};

struct ShellPlatformDelegate::PlatformData {
  std::unique_ptr<wm::WMState> wm_state;
  std::unique_ptr<display::Screen> screen;

  // TODO(danakj): This looks unused?
  std::unique_ptr<views::ViewsDelegate> views_delegate;
};

namespace {

// Maintain the web view for content shell
class ShellView : public views::BoxLayoutView {
  METADATA_HEADER(ShellView, views::BoxLayoutView)

 public:
  explicit ShellView(Shell* shell) : shell_(shell) { InitShellWindow(); }
  ShellView(const ShellView&) = delete;
  ShellView& operator=(const ShellView&) = delete;
  ~ShellView() override = default;

  Shell* ReleaseShell() { return shell_.release(); }

  void SetWebContents(WebContents* web_contents, const gfx::Size& size) {
    // If there was a previous WebView in this Shell it should be removed and
    // deleted.
    if (web_view_) {
      // ExtractAsDangling clears the underlying pointer and returns another
      // raw_ptr instance that is allowed to dangle.
      contents_view_->RemoveChildViewT(web_view_.ExtractAsDangling().get());
    }
    views::Builder<views::View>(contents_view_)
        .AddChild(views::Builder<views::WebView>()
                      .CopyAddressTo(&web_view_)
                      .SetBrowserContext(web_contents->GetBrowserContext())
                      .SetWebContents(web_contents)
                      .SetPreferredSize(size))
        .BuildChildren();

    bool should_focus = true;
#if BUILDFLAG(IS_STARBOARD)
    if (CheckAndHandleRevealState(web_contents)) {
      should_focus = false;
    }
#endif

    if (should_focus) {
      web_contents->GetPrimaryMainFrame()->GetRenderWidgetHost()->Focus();
    }
    web_view_->SizeToPreferredSize();

    // Resize the widget, keeping the same origin.
    gfx::Rect bounds = GetWidget()->GetWindowBoundsInScreen();
    bounds.set_size(GetWidget()->GetRootView()->GetPreferredSize({}));
    GetWidget()->SetBounds(bounds);
  }

 private:
  // Initialize the contents view contained in shell window
  void InitShellWindow() {
    auto builder =
        views::Builder<views::BoxLayoutView>(this)
            .SetBackground(
                views::CreateSolidBackground(ui::kColorWindowBackground))
            .SetOrientation(views::BoxLayout::Orientation::kVertical);

    builder.AddChild(views::Builder<views::View>()
                         .CopyAddressTo(&contents_view_)
                         .SetUseDefaultFillLayout(true));

    std::move(builder).BuildChildren();
    SetFlexForView(contents_view_, 1);
  }

  void InitAccelerators() {
    // This function must be called when part of the widget hierarchy.
    DCHECK(GetWidget());
    static const auto keys = std::to_array<ui::KeyboardCode>({
        ui::VKEY_F5,
        ui::VKEY_BROWSER_BACK,
        ui::VKEY_BROWSER_FORWARD,
    });
    for (size_t i = 0; i < std::size(keys); ++i) {
      GetFocusManager()->RegisterAccelerator(
          ui::Accelerator(keys[i], ui::EF_NONE),
          ui::AcceleratorManager::kNormalPriority, this);
    }
  }

  // Overridden from View
  gfx::Size GetMinimumSize() const override {
    // We want to be able to make the window smaller than its initial
    // (preferred) size.
    return gfx::Size();
  }
  void AddedToWidget() override { InitAccelerators(); }

  // Overridden from AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    switch (accelerator.key_code()) {
      case ui::VKEY_F5:
        shell_->Reload();
        return true;
      case ui::VKEY_BROWSER_BACK:
        shell_->GoBackOrForward(-1);
        return true;
      case ui::VKEY_BROWSER_FORWARD:
        shell_->GoBackOrForward(1);
        return true;
      default:
        return views::View::AcceleratorPressed(accelerator);
    }
  }

 private:
  std::unique_ptr<Shell> shell_;

  // Window title
  std::u16string title_;

  // Contents view contains the web contents view
  raw_ptr<views::View> contents_view_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
};

BEGIN_METADATA(ShellView)
END_METADATA

ShellView* ShellViewForWidget(views::Widget* widget) {
  return static_cast<ShellView*>(widget->widget_delegate()->GetContentsView());
}

}  // namespace

ShellPlatformDelegate::ShellPlatformDelegate() = default;
ShellPlatformDelegate::~ShellPlatformDelegate() {
  cobalt::CobaltLifecycleManager::GetInstance()->RemoveObserver(
      static_cast<cobalt::CobaltLifecycleManagerObserver*>(this));
}

std::unique_ptr<views::ViewsDelegate>
ShellPlatformDelegate::CreateViewsDelegate() {
  return std::make_unique<views::CobaltViewsDelegate>();
}

void ShellPlatformDelegate::Initialize(const gfx::Size& default_window_size,
                                       bool is_visible) {
  is_visible_ = is_visible;
  platform_ = std::make_unique<PlatformData>();

  platform_->wm_state = std::make_unique<wm::WMState>();
  // FakeScreen tests create their own screen.
  if (!display::Screen::HasScreen()) {
    platform_->screen = views::CreateDesktopScreen();
  }

  platform_->views_delegate = CreateViewsDelegate();
  cobalt::CobaltLifecycleManager::GetInstance()->AddObserver(
      static_cast<cobalt::CobaltLifecycleManagerObserver*>(this));
}

void ShellPlatformDelegate::CreatePlatformWindow(
    Shell* shell,
    const gfx::Size& initial_size) {
  DCHECK(!base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  shell_data.content_size = initial_size;
  shell_data.initial_size_ = initial_size;

  if (IsVisible()) {
    CreatePlatformWindowInternal(shell, initial_size);
  } else {
    shell_data.window_widget = nullptr;
  }
}

void ShellPlatformDelegate::CreatePlatformWindowInternal(
    Shell* shell,
    const gfx::Size& initial_size) {
  ShellData& shell_data = shell_data_map_.at(shell);

  auto delegate = std::make_unique<views::WidgetDelegate>();
  delegate->SetContentsView(std::make_unique<ShellView>(shell));
  delegate->SetHasWindowSizeControls(true);
  delegate->SetOwnedByWidget(views::WidgetDelegate::OwnedByWidgetPassKey());

  shell_data.window_widget = new views::Widget();
  views::Widget::InitParams params(
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  params.bounds = gfx::Rect(initial_size);
  params.delegate = delegate.release();
#if BUILDFLAG(IS_LINUX)
  params.wm_class_class = "chromium-content_shell";
  params.wm_class_name = params.wm_class_class;
#endif  // BUILDFLAG(IS_LINUX)
  shell_data.window_widget->Init(std::move(params));

  // |window_widget| is made visible in PlatformSetContents(), so that the
  // platform-window size does not need to change due to layout again.
}

gfx::NativeWindow ShellPlatformDelegate::GetNativeWindow(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  return shell_data.window_widget->GetNativeWindow();
}

void ShellPlatformDelegate::CleanUp(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  shell_data_map_.erase(shell);
}

void ShellPlatformDelegate::SetContents(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  if (shell_data.window_widget) {
    auto* shell_view = ShellViewForWidget(shell_data.window_widget);
    if (shell_view) {
      shell_view->SetWebContents(shell->web_contents(),
                                 shell_data.content_size);
    }

    views::WebContentsSetBackgroundColor::CreateForWebContentsWithColor(
        shell->web_contents(), SK_ColorTRANSPARENT);
  }
}
void ShellPlatformDelegate::DidCreateOrAttachWebContents(
    Shell* shell,
    WebContents* web_contents) {
  auto it = shell_data_map_.find(shell);
  if (it == shell_data_map_.end()) {
    return;
  }
  ShellData& shell_data = it->second;
  if (shell_data.window_widget) {
    // Safely map native views window Show and Restore on initial startup!
    shell_data.window_widget->GetNativeWindow()->Show();
    shell_data.window_widget->Restore();
  }
}
void ShellPlatformDelegate::RevealShell(Shell* shell) {
  // Dynamically re-enable Chromium's thread watchdog on resume!
  auto it = shell_data_map_.find(shell);
  if (it == shell_data_map_.end()) {
    LOG(ERROR) << "RevealShell called for untracked shell!";
    return;
  }
  ShellData& shell_data = it->second;
  if (!shell_data.window_widget) {
    CreatePlatformWindowInternal(shell, shell_data.initial_size_);
    SetContents(shell);
  } else {
    auto* native_window = shell_data.window_widget->GetNativeWindow();
    if (native_window && native_window->GetHost()) {
      auto* host_platform =
          static_cast<aura::WindowTreeHostPlatform*>(native_window->GetHost());
      auto* platform_window = static_cast<ui::PlatformWindowStarboard*>(
          host_platform->platform_window());
      if (platform_window) {
        platform_window->Restore();
      }
    }
  }
}
void ShellPlatformDelegate::MapWindowShell(Shell* shell) {
  auto it = shell_data_map_.find(shell);
  if (it == shell_data_map_.end()) {
    LOG(ERROR) << "MapWindowShell called for untracked shell!";
    return;
  }
  ShellData& shell_data = it->second;
  if (shell_data.window_widget) {
    auto* native_window = shell_data.window_widget->GetNativeWindow();
    if (native_window && native_window->GetHost() &&
        native_window->GetHost()->compositor()) {
      native_window->GetHost()->compositor()->SetVisible(true);
    }
    if (native_window) {
      native_window->Show();
      shell_data.window_widget->Restore();
      shell_data.window_widget->LayoutRootViewIfNecessary();
    }
  }
}

void ShellPlatformDelegate::ConcealShell(Shell* shell) {
  // Dynamically disable Chromium's thread watchdog during deactivation/freeze!
  auto it = shell_data_map_.find(shell);
  if (it == shell_data_map_.end()) {
    LOG(ERROR) << "ConcealShell called for untracked shell!";
    return;
  }
  ShellData& shell_data = it->second;
  if (shell_data.window_widget) {
    // Forcefully set compositor invisible to satisfy accelerated widget release
    // assertions natively.
    if (shell_data.window_widget->GetNativeWindow() &&
        shell_data.window_widget->GetNativeWindow()->GetHost() &&
        shell_data.window_widget->GetNativeWindow()->GetHost()->compositor()) {
      auto* compositor =
          shell_data.window_widget->GetNativeWindow()->GetHost()->compositor();
      compositor->SetVisible(false);
    }
    // Restore spec-compliant Minimize and Hide window widget deactivation!

    shell_data.window_widget->Minimize();
    shell_data.window_widget->GetNativeWindow()->Hide();
    shell_data.window_widget->Hide();
  }
}

void ShellPlatformDelegate::LoadSplashScreenContents(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  if (shell_data.window_widget) {
    ShellViewForWidget(shell_data.window_widget)
        ->SetWebContents(shell->splash_screen_web_contents(),
                         shell_data.content_size);
    shell_data.window_widget->GetNativeWindow()->GetHost()->Show();
    shell_data.window_widget->Show();
  }
}

void ShellPlatformDelegate::UpdateContents(Shell* shell) {
  SetContents(shell);
}

void ShellPlatformDelegate::ResizeWebContent(Shell* shell,
                                             const gfx::Size& content_size) {
  shell->web_contents()->Resize(gfx::Rect(content_size));
}

void ShellPlatformDelegate::SetTitle(Shell* shell,
                                     const std::u16string& title) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  if (shell_data.window_widget) {
    shell_data.window_widget->widget_delegate()->SetTitle(title);
  }
}

void ShellPlatformDelegate::MainFrameCreated(Shell* shell) {}

bool ShellPlatformDelegate::DestroyShell(Shell* shell) {
  VLOG(1) << "ShellPlatformDelegate::DestroyShell() called";
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  if (shell_data.window_widget) {
    shell_data.window_widget->CloseNow();
    return true;  // The CloseNow() will do the destruction of Shell.
  }

  return false;
}

}  // namespace content
