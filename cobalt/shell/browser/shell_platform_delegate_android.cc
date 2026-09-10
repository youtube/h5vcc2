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

#include "cobalt/shell/browser/shell_platform_delegate.h"

#include <jni.h>

#include "base/android/scoped_java_ref.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/notreached.h"
#include "cobalt/shell/android/cobalt_shell_jni_headers/Shell_jni.h"
#include "cobalt/shell/android/shell_manager.h"
#include "cobalt/shell/browser/shell.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"

using base::android::AttachCurrentThread;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

namespace content {

struct ShellPlatformDelegate::ShellData {
  base::android::ScopedJavaGlobalRef<jobject> java_object;
};

struct ShellPlatformDelegate::PlatformData {};

ShellPlatformDelegate::ShellPlatformDelegate() = default;

void ShellPlatformDelegate::Initialize(const gfx::Size& default_window_size,
                                       bool is_visible) {
  is_visible_ = is_visible;
  // |platform_| is not used on this platform.
  cobalt::CobaltLifecycleManager::GetInstance()->AddObserver(
      static_cast<cobalt::CobaltLifecycleManagerObserver*>(this));
}

ShellPlatformDelegate::~ShellPlatformDelegate() {
  cobalt::CobaltLifecycleManager::GetInstance()->RemoveObserver(
      static_cast<cobalt::CobaltLifecycleManagerObserver*>(this));
  if (!skip_for_testing_) {
    DestroyShellManager();
  }
}

void ShellPlatformDelegate::CreatePlatformWindow(
    Shell* shell,
    const gfx::Size& initial_size) {
  DCHECK(!base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  shell_data.java_object.Reset(CreateShellView(shell));
}

void ShellPlatformDelegate::CleanUp(Shell* shell) {
  JNIEnv* env = AttachCurrentThread();
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  RemoveShellView(shell_data.java_object);

  if (!shell_data.java_object.is_null()) {
    Java_Shell_onNativeDestroyed(env, shell_data.java_object);
  }

  shell_data_map_.erase(shell);
}

void ShellPlatformDelegate::SetContents(Shell* shell) {
  JNIEnv* env = AttachCurrentThread();
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  Java_Shell_initFromNativeTabContents(
      env, shell_data.java_object, shell->web_contents()->GetJavaWebContents());
}

void ShellPlatformDelegate::LoadSplashScreenContents(Shell* shell) {
  JNIEnv* env = AttachCurrentThread();
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  Java_Shell_loadSplashScreenNativeTabContents(
      env, shell_data.java_object,
      shell->splash_screen_web_contents()->GetJavaWebContents());
}

void ShellPlatformDelegate::UpdateContents(Shell* shell) {
  JNIEnv* env = AttachCurrentThread();
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  Java_Shell_updateNativeTabContents(
      env, shell_data.java_object, shell->web_contents()->GetJavaWebContents());
}

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

void ShellPlatformDelegate::CreatePlatformWindowInternal(
    Shell* shell,
    const gfx::Size& initial_size) {}

void ShellPlatformDelegate::RevealShell(Shell* shell) {
  if (shell && shell->web_contents()) {
    shell->web_contents()->WasShown();
  }
}

void ShellPlatformDelegate::ConcealShell(Shell* shell) {
  if (shell && shell->web_contents()) {
    shell->web_contents()->WasHidden();
  }
}

void ShellPlatformDelegate::ToggleFullscreenModeForTab(
    Shell* shell,
    WebContents* web_contents,
    bool enter_fullscreen) {
  JNIEnv* env = AttachCurrentThread();
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  Java_Shell_toggleFullscreenModeForTab(env, shell_data.java_object,
                                        enter_fullscreen);
}

bool ShellPlatformDelegate::IsFullscreenForTabOrPending(
    Shell* shell,
    const WebContents* web_contents) const {
  JNIEnv* env = AttachCurrentThread();
  DCHECK(base::Contains(shell_data_map_, shell));
  const ShellData& shell_data = shell_data_map_.find(shell)->second;

  return Java_Shell_isFullscreenForTabOrPending(env, shell_data.java_object);
}

void ShellPlatformDelegate::SetOverlayMode(Shell* shell,
                                           bool use_overlay_mode) {
  JNIEnv* env = base::android::AttachCurrentThread();
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  Java_Shell_setOverlayMode(env, shell_data.java_object, use_overlay_mode);
}

void ShellPlatformDelegate::LoadProgressChanged(Shell* shell, double progress) {
  JNIEnv* env = AttachCurrentThread();
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  Java_Shell_onLoadProgressChanged(env, shell_data.java_object, progress);
}

// static
void JNI_Shell_LoadSplashScreenWebContents(JNIEnv* env, jlong shellPtr) {
  Shell* shell = reinterpret_cast<Shell*>(shellPtr);
  shell->LoadSplashScreenWebContents();
}

// static
void JNI_Shell_CloseShell(JNIEnv* env, jlong shellPtr) {
  Shell* shell = reinterpret_cast<Shell*>(shellPtr);
  shell->Close();
}

}  // namespace content
