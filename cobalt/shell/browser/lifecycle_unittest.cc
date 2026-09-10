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

#include "cobalt/shell/browser/shell.h"
#include "cobalt/shell/browser/shell_test_support.h"
#include "content/test/test_web_contents.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;

namespace content {

class LifecycleTest : public ShellTestBase {
 public:
  LifecycleTest() = default;

  void CreateTestShell(bool is_visible) {
    InitializeShell(is_visible);
    WebContents::CreateParams create_params(browser_context());
    create_params.desired_renderer_state =
        WebContents::CreateParams::kNoRendererProcess;
    create_params.initially_hidden = !is_visible;
    std::unique_ptr<WebContents> web_contents(
        TestWebContents::Create(create_params));

    if (is_visible) {
      EXPECT_CALL(*platform_, CreatePlatformWindow(_, _));
    }
    EXPECT_CALL(*platform_, SetContents(_));

    shell_ = new Shell(std::move(web_contents), nullptr /* splash_contents */,
                       /*should_set_delegate=*/true, /*topic=*/"",
                       /*skip_for_testing=*/true);

    if (is_visible) {
      platform_->CreatePlatformWindow(shell_, gfx::Size(1920, 1080));
    }

    Shell::FinishShellInitialization(shell_);
  }

  void TearDown() override {
    if (shell_) {
      EXPECT_CALL(*platform_, DestroyShell(shell_));
      EXPECT_CALL(*platform_, CleanUp(shell_));
      shell_->Close();
      task_environment()->RunUntilIdle();
      shell_ = nullptr;
    }
    ShellTestBase::TearDown();
  }

  Shell* shell_ = nullptr;
};

TEST_F(LifecycleTest, StartupVisible) {
  CreateTestShell(true /* is_visible */);
  EXPECT_TRUE(platform_->IsVisible());
  EXPECT_EQ(shell_->web_contents()->GetVisibility(), Visibility::VISIBLE);
}

TEST_F(LifecycleTest, StartupHidden) {
  CreateTestShell(false /* is_visible */);
  EXPECT_FALSE(platform_->IsVisible());
  EXPECT_EQ(shell_->web_contents()->GetVisibility(), Visibility::HIDDEN);
}

TEST_F(LifecycleTest, Reveal) {
  CreateTestShell(false /* is_visible */);
  EXPECT_FALSE(platform_->IsVisible());
  EXPECT_EQ(shell_->web_contents()->GetVisibility(), Visibility::HIDDEN);

  // Trigger reveal.
  EXPECT_CALL(*platform_, OnReveal()).WillOnce([this]() {
    platform_->ShellPlatformDelegate::OnReveal();
  });
  EXPECT_CALL(*platform_, RevealShell(shell_))
      .WillOnce(
          [](content::Shell* shell) { shell->web_contents()->WasShown(); });
  Shell::OnReveal();

  EXPECT_TRUE(platform_->IsVisible());
  // Visibility should now be VISIBLE because we called WasShown() in OnReveal
  // to unblock the renderer.
  EXPECT_EQ(shell_->web_contents()->GetVisibility(), Visibility::VISIBLE);

  // Simulate Reveal ACK.
  platform_->OnPageVisibilityVisible(shell_);

  // Now visibility should be VISIBLE.
  EXPECT_EQ(shell_->web_contents()->GetVisibility(), Visibility::VISIBLE);
}

TEST_F(LifecycleTest, RedundantReveal) {
  CreateTestShell(false /* is_visible */);

  // First reveal.
  EXPECT_CALL(*platform_, OnReveal()).Times(1).WillOnce([this]() {
    platform_->ShellPlatformDelegate::OnReveal();
  });
  EXPECT_CALL(*platform_, RevealShell(shell_));
  Shell::OnReveal();

  // Second reveal (should be ignored by idempotent Shell logic).
  EXPECT_CALL(*platform_, OnReveal()).Times(0);
  Shell::OnReveal();
}

TEST_F(LifecycleTest, Conceal) {
  CreateTestShell(true /* is_visible */);
  EXPECT_TRUE(platform_->IsVisible());
  EXPECT_EQ(shell_->web_contents()->GetVisibility(), Visibility::VISIBLE);

  // Trigger conceal.
  EXPECT_CALL(*platform_, OnConceal()).WillOnce([this]() {
    platform_->ShellPlatformDelegate::OnConceal();
  });
  EXPECT_CALL(*platform_, ConcealShell(shell_));
  Shell::OnConceal();

  // Symmetrical unit-test trigger: since there are no active Blink frames to
  // dispatch Mojo visibility ACKs, we manually invoke the observer callbacks
  // on the platform manager base class.
  static_cast<cobalt::CobaltLifecycleManagerObserver*>(platform_)
      ->OnWebContentsConcealed(shell_->web_contents());
  static_cast<cobalt::CobaltLifecycleManagerObserver*>(platform_)
      ->OnAllWebContentsConcealed();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(platform_->IsVisible());
  EXPECT_EQ(shell_->web_contents()->GetVisibility(), Visibility::HIDDEN);
}

TEST_F(LifecycleTest, FreezeUnfreeze) {
  CreateTestShell(false /* is_visible */);
  TestWebContents* test_web_contents =
      static_cast<TestWebContents*>(shell_->web_contents());

  // Trigger freeze.
  EXPECT_CALL(*platform_, OnFreeze()).WillOnce([this]() {
    platform_->ShellPlatformDelegate::OnFreeze();
  });
  Shell::OnFreeze();
  EXPECT_TRUE(test_web_contents->IsPageFrozen());

  // Trigger unfreeze.
  EXPECT_CALL(*platform_, OnUnfreeze()).WillOnce([this]() {
    platform_->ShellPlatformDelegate::OnUnfreeze();
  });
  Shell::OnUnfreeze();
  EXPECT_FALSE(test_web_contents->IsPageFrozen());
}

TEST_F(LifecycleTest, BlurFocus) {
  CreateTestShell(true /* is_visible */);

  // Trigger blur.
  EXPECT_CALL(*platform_, OnBlur());
  Shell::OnBlur();

  // Trigger focus.
  EXPECT_CALL(*platform_, OnFocus());
  Shell::OnFocus();
}

}  // namespace content
