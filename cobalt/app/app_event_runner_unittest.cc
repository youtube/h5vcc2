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

#include "cobalt/browser/h5vcc_runtime/deep_link_manager.h"
#include "cobalt/browser/lifecycle/cobalt_lifecycle_manager.h"
#include "cobalt/shell/browser/shell.h"
#include "cobalt/shell/browser/shell_test_support.h"
#include "content/public/browser/visibility.h"
#include "content/test/test_web_contents.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;

namespace cobalt {

class AppEventRunnerTest : public content::ShellTestBase {
 public:
  AppEventRunnerTest() { runner_ = AppEventRunner::Create(); }

 protected:
  void CreateTestShell(bool is_visible,
                       bool is_focused = false,
                       bool is_frozen = false) {
    InitializeShell(is_visible);
    content::WebContents::CreateParams create_params(browser_context());
    create_params.desired_renderer_state =
        content::WebContents::CreateParams::kNoRendererProcess;
    create_params.initially_hidden = !is_visible;
    std::unique_ptr<content::WebContents> web_contents(
        content::TestWebContents::Create(create_params));

    shell_ = new content::Shell(std::move(web_contents),
                                nullptr /* splash_contents */,
                                /*should_set_delegate=*/true, /*topic=*/"",
                                /*skip_for_testing=*/true);

    // Setup expectations for shell initialization.
    EXPECT_CALL(*platform_, CreatePlatformWindow(shell_, _)).Times(AnyNumber());
    EXPECT_CALL(*platform_, SetContents(shell_)).Times(AnyNumber());

    if (is_visible) {
      platform_->CreatePlatformWindow(shell_, gfx::Size(1920, 1080));
    }
    platform_->SetContents(shell_);

    content::Shell::FinishShellInitialization(shell_);

    runner_->set_is_running(true);
    runner_->set_is_visible(is_visible);
    runner_->set_is_focused(is_focused);
    runner_->set_is_frozen(is_frozen);

    if (is_frozen) {
      content::Shell::OnFreeze();
    }
  }

  void TearDown() override {
    if (shell_) {
      // These are called during shell destruction or Close().
      EXPECT_CALL(*platform_, DestroyShell(shell_)).Times(AnyNumber());
      EXPECT_CALL(*platform_, CleanUp(shell_)).Times(AnyNumber());
      shell_->Close();
      task_environment_.RunUntilIdle();
      shell_ = nullptr;
    }
    content::ShellTestBase::TearDown();
  }

  std::unique_ptr<AppEventRunner> runner_;
  content::Shell* shell_ = nullptr;
};

TEST_F(AppEventRunnerTest, OnBlur) {
  CreateTestShell(true /* is_visible */, true /* is_focused */);

  EXPECT_CALL(*platform_, OnBlur());
  runner_->OnBlur();

  EXPECT_FALSE(runner_->is_focused());
}

TEST_F(AppEventRunnerTest, OnFocus) {
  CreateTestShell(true /* is_visible */, false /* is_focused */);

  EXPECT_CALL(*platform_, OnFocus());
  runner_->OnFocus();

  EXPECT_TRUE(runner_->is_focused());
}

TEST_F(AppEventRunnerTest, OnConceal) {
  CreateTestShell(true /* is_visible */);

  EXPECT_CALL(*platform_, OnConceal()).WillOnce([this]() {
    platform_->ShellPlatformDelegate::OnConceal();
  });
  EXPECT_CALL(*platform_, ConcealShell(shell_));
  runner_->OnConceal();

  EXPECT_FALSE(runner_->is_visible());
  EXPECT_EQ(shell_->web_contents()->GetVisibility(),
            content::Visibility::HIDDEN);
}

TEST_F(AppEventRunnerTest, OnReveal) {
  CreateTestShell(false /* is_visible */);

  // Bind a mock renderer to CobaltLifecycleManager.
  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote;
  CobaltLifecycleManager::GetInstance()->BindReceiver(
      shell_->web_contents()->GetPrimaryMainFrame(),
      remote.BindNewPipeAndPassReceiver());

  EXPECT_CALL(*platform_, OnReveal());
  EXPECT_CALL(*platform_, RevealShell(shell_))
      .WillOnce(
          [](content::Shell* shell) { shell->web_contents()->WasShown(); });
  runner_->OnReveal();

  EXPECT_TRUE(runner_->is_visible());
  // Visibility should be VISIBLE because we called WasShown() in RevealShell
  // to unblock the renderer.
  EXPECT_EQ(shell_->web_contents()->GetVisibility(),
            content::Visibility::VISIBLE);
  // Simulate Reveal ACK from the frame via Mojo to complete transition.
  remote->OnPageVisibilityChanged(true);
  base::RunLoop().RunUntilIdle();

  remote.reset();
  base::RunLoop().RunUntilIdle();
}

TEST_F(AppEventRunnerTest, OnFreeze) {
  CreateTestShell(false /* is_visible */, false /* is_focused */,
                  false /* is_frozen */);
  content::TestWebContents* test_web_contents =
      static_cast<content::TestWebContents*>(shell_->web_contents());

  EXPECT_CALL(*platform_, OnFreeze());
  runner_->OnFreeze();
  EXPECT_TRUE(test_web_contents->IsPageFrozen());
}

TEST_F(AppEventRunnerTest, OnUnfreeze) {
  CreateTestShell(false /* is_visible */, false /* is_focused */,
                  true /* is_frozen */);
  content::TestWebContents* test_web_contents =
      static_cast<content::TestWebContents*>(shell_->web_contents());

  EXPECT_CALL(*platform_, OnUnfreeze());
  runner_->OnUnfreeze();
  EXPECT_FALSE(test_web_contents->IsPageFrozen());
}

TEST_F(AppEventRunnerTest, OnLink) {
  CreateTestShell(true /* is_visible */);
  const char* kLink = "https://example.com/test";
  SbEvent event = {kSbEventTypeLink, 0, const_cast<char*>(kLink)};

  runner_->OnLink(&event);

  EXPECT_EQ(
      cobalt::browser::DeepLinkManager::GetInstance()->GetAndClearDeepLink(),
      kLink);
}

TEST_F(AppEventRunnerTest, OnLowMemory) {
  CreateTestShell(true /* is_visible */);
  static bool callback_called = false;
  callback_called = false;
  SbEventCallback callback = [](void*) { callback_called = true; };

  SbEvent event = {kSbEventTypeLowMemory, 0, reinterpret_cast<void*>(callback)};
  runner_->OnLowMemory(&event);

  EXPECT_TRUE(callback_called);
}

TEST_F(AppEventRunnerTest, OnAccessibilityTextToSpeechSettingsChanged) {
  CreateTestShell(true /* is_visible */);
  bool enabled = true;
  SbEvent event = {kSbEventTypeAccessibilityTextToSpeechSettingsChanged, 0,
                   &enabled};
  // Verify no crash.
  runner_->OnAccessibilityTextToSpeechSettingsChanged(&event);
}

TEST_F(AppEventRunnerTest, OnInput) {
  CreateTestShell(true /* is_visible */);
  SbEvent event = {kSbEventTypeInput, 0, nullptr};
  // Verify no crash.
  runner_->OnInput(&event);
}

TEST_F(AppEventRunnerTest, OnWindowSizeChanged) {
  CreateTestShell(true /* is_visible */);
  SbEvent event = {kSbEventTypeWindowSizeChanged, 0, nullptr};
  // Verify no crash.
  runner_->OnWindowSizeChanged(&event);
}

TEST_F(AppEventRunnerTest, OnDateTimeConfigurationChanged) {
  CreateTestShell(true /* is_visible */);
  SbEvent event = {kSbEventDateTimeConfigurationChanged, 0, nullptr};
  // Verify no crash.
  runner_->OnDateTimeConfigurationChanged(&event);
}

TEST_F(AppEventRunnerTest, StateGetters) {
  runner_->set_is_running(true);
  runner_->set_is_visible(false);
  EXPECT_TRUE(runner_->is_running());
  EXPECT_FALSE(runner_->is_visible());

  runner_->set_is_running(false);
  runner_->set_is_visible(true);
  EXPECT_FALSE(runner_->is_running());
  EXPECT_TRUE(runner_->is_visible());
}

}  // namespace cobalt
