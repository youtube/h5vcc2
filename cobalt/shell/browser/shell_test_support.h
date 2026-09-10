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

#ifndef COBALT_SHELL_BROWSER_SHELL_TEST_SUPPORT_H_
#define COBALT_SHELL_BROWSER_SHELL_TEST_SUPPORT_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "cobalt/shell/browser/shell.h"
#include "cobalt/shell/browser/shell_platform_delegate.h"
#include "content/browser/accessibility/browser_accessibility_state_impl.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/network_service_util.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/test/test_content_browser_client.h"
#include "content/test/test_content_client.h"
#include "mojo/core/embedder/embedder.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/gfx/geometry/size.h"
#if BUILDFLAG(IS_ANDROID)
#include "base/android/jni_android.h"
#endif

#if defined(USE_AURA)
#include "ui/aura/env.h"
#include "ui/ozone/platform/starboard/platform_window_starboard.h"
#endif

namespace content {

class MockShellPlatformDelegate : public ShellPlatformDelegate {
 public:
  MOCK_METHOD(void,
              CreatePlatformWindow,
              (Shell * shell, const gfx::Size& initial_size),
              (override));
  MOCK_METHOD(void, Initialize, (const gfx::Size&, bool), (override));
  MOCK_METHOD(void, SetContents, (Shell * shell), (override));
  MOCK_METHOD(void, LoadSplashScreenContents, (Shell * shell), (override));
  MOCK_METHOD(void, UpdateContents, (Shell * shell), (override));
  MOCK_METHOD(void,
              ResizeWebContent,
              (Shell * shell, const gfx::Size& content_size),
              (override));
  MOCK_METHOD(bool, DestroyShell, (Shell * shell), (override));
  MOCK_METHOD(void, CleanUp, (Shell * shell), (override));
  MOCK_METHOD(void,
              SetTitle,
              (Shell * shell, const std::u16string& title),
              (override));
  MOCK_METHOD(void, DidCloseLastWindow, (), (override));
  MOCK_METHOD(void, RevealShell, (Shell * shell), (override));
  MOCK_METHOD(void, ConcealShell, (Shell * shell), (override));
  MOCK_METHOD(void, OnBlur, (), (override));
  MOCK_METHOD(void, OnFocus, (), (override));
  MOCK_METHOD(void, OnConceal, (), (override));
  MOCK_METHOD(void, OnReveal, (), (override));
  MOCK_METHOD(void, OnFreeze, (), (override));
  MOCK_METHOD(void, OnUnfreeze, (), (override));
  MOCK_METHOD(void, OnStop, (), (override));
  MOCK_METHOD(void, MainFrameCreated, (Shell*), (override));
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  MOCK_METHOD(bool,
              IsFullscreenForTabOrPending,
              (Shell*, const WebContents*),
              (const override));
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)

  MockShellPlatformDelegate() {
#if BUILDFLAG(IS_IOS)
    // Initialize() on tvOS creates a display::Screen instance that is required
    // by calls to Shell::CreateNewWindow().
    ON_CALL(*this, Initialize)
        .WillByDefault([this](const gfx::Size& default_window_size,
                              bool is_visible) {
          ShellPlatformDelegate::Initialize(default_window_size, is_visible);
        });
#else
    ON_CALL(*this, Initialize)
        .WillByDefault(
            [this](const gfx::Size& default_window_size, bool is_visible) {
              is_visible_ = is_visible;
              cobalt::CobaltLifecycleManager::GetInstance()->AddObserver(
                  static_cast<cobalt::CobaltLifecycleManagerObserver*>(this));
            });
#endif
  }

  void SetIsVisible(bool is_visible) { is_visible_ = is_visible; }
};

class TestBrowserAccessibilityState : public BrowserAccessibilityStateImpl {
 public:
  TestBrowserAccessibilityState() = default;
};

class MojoInitializer {
 public:
  MojoInitializer() {
#if defined(USE_AURA)
    if (!aura::Env::HasInstance()) {
      aura_env_ = aura::Env::CreateInstance();
    }
#endif
    mojo::core::Init();
  }

 private:
#if defined(USE_AURA)
  std::unique_ptr<aura::Env> aura_env_;
#endif
};

class ShellTestBase : public ::testing::Test {
 public:
  ShellTestBase()
      : task_environment_(content::BrowserTaskEnvironment::IO_MAINLOOP,
                          base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    // b/537618464: Using a separate thread for the NetworkService causes random
    // crashes in tests using ShellTestBase. This also affects multiple upstream
    // tests. It is unclear if it is related to the use of MOCK_TIME above or if
    // it is just the fact that the tests create and destroy a
    // BrowserTaskEnvironment too quickly.
    //
    // From https://chromium-review.googlesource.com/c/chromium/src/+/7656849:
    // A short-term workaround for new tests to mitigate the integration
    // issue between the dedicated `NetworkService` thread and
    // `BrowserTaskEnvironment` (see crbug.com/493322520). If disabled,
    // `NetworkService` will use IO thread instead. This is valid for these
    // tests because they do not depend on whether the network service is
    // running on a dedicated thread or the IO thread.
    scoped_feature_list_.InitAndDisableFeature(
        content::kNetworkServiceDedicatedThread);
  }
  ~ShellTestBase() override = default;

  void SetUp() override {
#if BUILDFLAG(IS_ANDROID)
    JNIEnv* env = base::android::AttachCurrentThread();
    jclass looper_clazz = env->FindClass("android/os/Looper");
    jmethodID my_looper_method = env->GetStaticMethodID(
        looper_clazz, "myLooper", "()Landroid/os/Looper;");
    jobject looper =
        env->CallStaticObjectMethod(looper_clazz, my_looper_method);
    if (!looper) {
      jmethodID prepare_method =
          env->GetStaticMethodID(looper_clazz, "prepare", "()V");
      env->CallStaticVoidMethod(looper_clazz, prepare_method);
      looper = env->CallStaticObjectMethod(looper_clazz, my_looper_method);
    }
    jclass thread_utils_clazz = env->FindClass("org/chromium/base/ThreadUtils");
    jmethodID set_ui_thread_method = env->GetStaticMethodID(
        thread_utils_clazz, "setUiThread", "(Landroid/os/Looper;)V");
    env->CallStaticVoidMethod(thread_utils_clazz, set_ui_thread_method, looper);
#endif

    ForceInProcessNetworkService();
    mojo::core::Init();
    ui::DeviceDataManager::CreateInstance();

    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    if (!command_line->HasSwitch(switches::kSingleProcess)) {
      command_line->AppendSwitch(switches::kSingleProcess);
    }

#if defined(USE_AURA)
    if (!aura::Env::HasInstance()) {
      env_ = aura::Env::CreateInstance();
    }
#endif

    browser_accessibility_state_ =
        std::make_unique<TestBrowserAccessibilityState>();

    SetContentClient(&test_content_client_);
    SetBrowserClientForTesting(&test_content_browser_client_);

    rvh_enabler_ = std::make_unique<RenderViewHostTestEnabler>();

    browser_context_ = std::make_unique<TestBrowserContext>();

    if (!ui::ResourceBundle::HasSharedInstance()) {
      ui::ResourceBundle::InitSharedInstanceWithPakPath(base::FilePath());
    }
  }

  void TearDown() override {
    ClearPlatform();
    Shell::Shutdown();
    browser_context_.reset();
    rvh_enabler_.reset();
    SetBrowserClientForTesting(nullptr);
    SetContentClient(nullptr);
    browser_accessibility_state_.reset();
#if defined(USE_AURA)
    env_.reset();
    ui::PlatformWindowStarboard::ClearWindowCreatedCallback();
    ui::PlatformWindowStarboard::ClearWindowDestroyedCallback();
#endif
    ui::DeviceDataManager::DeleteInstance();
  }

  void ClearPlatform() { platform_ = nullptr; }

  void InitializeShell(bool is_visible) {
    ClearPlatform();
    auto platform =
        std::make_unique<::testing::NiceMock<MockShellPlatformDelegate>>();
    platform_ = platform.get();
    ON_CALL(*platform_, OnBlur()).WillByDefault([this]() {
      platform_->ShellPlatformDelegate::OnBlur();
    });
    ON_CALL(*platform_, OnFocus()).WillByDefault([this]() {
      platform_->ShellPlatformDelegate::OnFocus();
    });
    ON_CALL(*platform_, OnConceal()).WillByDefault([this]() {
      platform_->ShellPlatformDelegate::OnConceal();
    });
    ON_CALL(*platform_, OnReveal()).WillByDefault([this]() {
      platform_->ShellPlatformDelegate::OnReveal();
    });
    ON_CALL(*platform_, OnFreeze()).WillByDefault([this]() {
      platform_->ShellPlatformDelegate::OnFreeze();
    });
    ON_CALL(*platform_, OnUnfreeze()).WillByDefault([this]() {
      platform_->ShellPlatformDelegate::OnUnfreeze();
    });
    ON_CALL(*platform_, OnStop()).WillByDefault([this]() {
      platform_->ShellPlatformDelegate::OnStop();
    });
    ON_CALL(*platform_, DidCloseLastWindow()).WillByDefault([this]() {
      ClearPlatform();
      Shell::Shutdown();
    });

    platform->is_visible_ = is_visible;

    Shell::Initialize(std::move(platform), is_visible);
  }

  TestBrowserContext* browser_context() { return browser_context_.get(); }
  content::BrowserTaskEnvironment* task_environment() {
    return &task_environment_;
  }

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;
  content::BrowserTaskEnvironment task_environment_;
  TestContentClient test_content_client_;
  TestContentBrowserClient test_content_browser_client_;
#if defined(USE_AURA)
  std::unique_ptr<aura::Env> env_;
#endif
  std::unique_ptr<TestBrowserAccessibilityState> browser_accessibility_state_;
  std::unique_ptr<RenderViewHostTestEnabler> rvh_enabler_;
  raw_ptr<MockShellPlatformDelegate> platform_ = nullptr;
  std::unique_ptr<TestBrowserContext> browser_context_;
};

}  // namespace content

#endif  // COBALT_SHELL_BROWSER_SHELL_TEST_SUPPORT_H_
