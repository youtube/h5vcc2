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

#include "base/containers/contains.h"
#include "cobalt/shell/browser/shell.h"
#include "cobalt/shell/browser/shell_platform_data_aura.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_tree_host_platform.h"
#include "ui/compositor/compositor.h"
#include "ui/platform_window/platform_window.h"

namespace content {

struct ShellPlatformDelegate::ShellData {
  gfx::NativeWindow window;
  gfx::Size initial_size_;
};

struct ShellPlatformDelegate::PlatformData {
  std::unique_ptr<ShellPlatformDataAura> aura;
  gfx::Size default_window_size;
};

ShellPlatformDelegate::ShellPlatformDelegate() = default;
ShellPlatformDelegate::~ShellPlatformDelegate() {
  cobalt::CobaltLifecycleManager::GetInstance()->RemoveObserver(
      static_cast<cobalt::CobaltLifecycleManagerObserver*>(this));
}

void ShellPlatformDelegate::Initialize(const gfx::Size& default_window_size,
                                       bool is_visible) {
  is_visible_ = is_visible;
  platform_ = std::make_unique<PlatformData>();
  platform_->default_window_size = default_window_size;
  platform_->aura = nullptr;
  cobalt::CobaltLifecycleManager::GetInstance()->AddObserver(
      static_cast<cobalt::CobaltLifecycleManagerObserver*>(this));
}

void ShellPlatformDelegate::CreatePlatformWindow(
    Shell* shell,
    const gfx::Size& initial_size) {
  DCHECK(!base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  shell_data.initial_size_ = initial_size;

  if (IsVisible()) {
    CreatePlatformWindowInternal(shell, initial_size);
  } else {
    shell_data.window = nullptr;
  }
}

void ShellPlatformDelegate::CreatePlatformWindowInternal(
    Shell* shell,
    const gfx::Size& initial_size) {
  if (!platform_->aura) {
    platform_->aura =
        std::make_unique<ShellPlatformDataAura>(platform_->default_window_size);
  }
  platform_->aura->ResizeWindow(initial_size);

  ShellData& shell_data = shell_data_map_.at(shell);
  shell_data.window = platform_->aura->host()->window();
}

gfx::NativeWindow ShellPlatformDelegate::GetNativeWindow(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];
  return shell_data.window;
}

void ShellPlatformDelegate::CleanUp(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  shell_data_map_.erase(shell);
}

void ShellPlatformDelegate::SetContents(Shell* shell) {
  aura::Window* content = shell->web_contents()->GetNativeView();
  if (!platform_ || !platform_->aura) {
    return;
  }
  aura::Window* parent = platform_->aura->host()->window();
  if (parent && !parent->Contains(content)) {
    parent->AddChild(content);
  }

  content->Show();
}

void ShellPlatformDelegate::RevealShell(Shell* shell) {
  ShellData& shell_data = shell_data_map_.at(shell);
  if (!shell_data.window) {
    CreatePlatformWindowInternal(shell, shell_data.initial_size_);
    SetContents(shell);
  }
  if (shell && shell->web_contents()) {
    shell->web_contents()->WasShown();
  }
}
void ShellPlatformDelegate::MapWindowShell(Shell* shell) {
  if (!platform_ || !platform_->aura || !platform_->aura->host()) {
    return;
  }
  auto* host =
      static_cast<aura::WindowTreeHostPlatform*>(platform_->aura->host());
  if (host && host->platform_window()) {
    host->platform_window()->Show(true);
  }
}

void ShellPlatformDelegate::ConcealShell(Shell* shell) {
  if (shell && shell->web_contents()) {
    shell->web_contents()->WasHidden();
  }
  if (!platform_ || !platform_->aura || !platform_->aura->host()) {
    return;
  }
  if (platform_->aura->host()->compositor()) {
    platform_->aura->host()->compositor()->SetVisible(false);
  }
  platform_->aura->host()->Hide();
}
void ShellPlatformDelegate::DidCreateOrAttachWebContents(
    Shell* shell,
    WebContents* web_contents) {}

void ShellPlatformDelegate::LoadSplashScreenContents(Shell* shell) {}

void ShellPlatformDelegate::UpdateContents(Shell* shell) {}

void ShellPlatformDelegate::ResizeWebContent(Shell* shell,
                                             const gfx::Size& content_size) {
  shell->web_contents()->GetRenderWidgetHostView()->SetSize(content_size);
}

void ShellPlatformDelegate::SetTitle(Shell* shell,
                                     const std::u16string& title) {}

void ShellPlatformDelegate::MainFrameCreated(Shell* shell) {}

bool ShellPlatformDelegate::DestroyShell(Shell* shell) {
  return false;  // Shell destroys itself.
}

}  // namespace content
