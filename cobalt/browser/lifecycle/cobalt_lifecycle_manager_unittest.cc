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

#include "cobalt/browser/lifecycle/cobalt_lifecycle_manager.h"

#include "cobalt/browser/h5vcc_runtime/h5vcc_runtime_impl.h"
#include "cobalt/browser/h5vcc_runtime/public/mojom/h5vcc_runtime.mojom.h"
#include "cobalt/shell/browser/shell_test_support.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "content/test/test_web_contents.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cobalt {

class MockCobaltLifecycleObserver : public CobaltLifecycleManagerObserver {
 public:
  MOCK_METHOD(void, OnWebContentsVisible, (content::WebContents*), (override));
  MOCK_METHOD(void,
              OnWebContentsConcealed,
              (content::WebContents*),
              (override));
  MOCK_METHOD(void, OnWebContentsBlurred, (content::WebContents*), (override));
  MOCK_METHOD(void, OnWebContentsResumed, (content::WebContents*), (override));
  MOCK_METHOD(void,
              OnStartWaitingForReveal,
              (content::WebContents*),
              (override));
  MOCK_METHOD(void, OnAllWebContentsVisible, (), (override));
  MOCK_METHOD(void, OnAllWebContentsConcealed, (), (override));
  MOCK_METHOD(void, OnAllWebContentsBlurred, (), (override));
  MOCK_METHOD(void, OnAllWebContentsResumed, (), (override));
  MOCK_METHOD(void, OnConcealCompleted, (), (override));
};

class CobaltLifecycleManagerTest : public content::ShellTestBase {
 protected:
  void SetUp() override {
    content::ShellTestBase::SetUp();
    InitializeShell(true /* is_visible */);
    manager_ = CobaltLifecycleManager::GetInstance();
  }

  std::unique_ptr<content::WebContents> CreateScopedTestWebContents() {
    content::WebContents::CreateParams create_params(browser_context());
    return std::unique_ptr<content::WebContents>(
        content::TestWebContents::Create(create_params));
  }

  content::RenderFrameHost* AddChildFrame(content::RenderFrameHost* rfh) {
    content::RenderFrameHost* child_rfh =
        content::RenderFrameHostTester::For(rfh)->AppendChild("");
    content::RenderFrameHostTester::For(child_rfh)
        ->InitializeRenderFrameIfNeeded();
    return child_rfh;
  }

  CobaltLifecycleManager* manager_;
};

// Verifies that lifecycle transition expectations (StartWaitingForAck) are
// scoped strictly to the target WebContents, ensuring multiple concurrent
// WebContents instances do not interfere with each other's visibility
// callbacks.
TEST_F(CobaltLifecycleManagerTest, ScopedWaiting) {
  std::unique_ptr<content::WebContents> contents1 =
      CreateScopedTestWebContents();
  content::RenderFrameHostTester::For(contents1->GetPrimaryMainFrame())
      ->InitializeRenderFrameIfNeeded();

  std::unique_ptr<content::WebContents> contents2 =
      CreateScopedTestWebContents();
  content::RenderFrameHostTester::For(contents2->GetPrimaryMainFrame())
      ->InitializeRenderFrameIfNeeded();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote1;
  manager_->BindReceiver(contents1->GetPrimaryMainFrame(),
                         remote1.BindNewPipeAndPassReceiver());
  remote1->OnFrameReady();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote2;
  manager_->BindReceiver(contents2->GetPrimaryMainFrame(),
                         remote2.BindNewPipeAndPassReceiver());
  remote2->OnFrameReady();

  // Establish Mojo connections and register frames.
  base::RunLoop().RunUntilIdle();

  MockCobaltLifecycleObserver observer;
  manager_->AddObserver(&observer);

  // Start waiting for contents1.
  manager_->StartWaitingForAck(contents1.get(), PendingAck::kReveal);

  // We expect OnWebContentsVisible to be called only for contents1.
  EXPECT_CALL(observer, OnWebContentsVisible(contents1.get())).Times(1);
  EXPECT_CALL(observer, OnWebContentsVisible(contents2.get())).Times(0);

  // Frame 1 reports visible via Mojo.
  remote1->OnPageVisibilityChanged(true);

  // Wait for Mojo message dispatch.
  base::RunLoop().RunUntilIdle();

  // Start waiting for contents2.
  manager_->StartWaitingForAck(contents2.get(), PendingAck::kReveal);

  EXPECT_CALL(observer, OnWebContentsVisible(contents2.get())).Times(1);

  // Frame 2 reports visible via Mojo.
  remote2->OnPageVisibilityChanged(true);

  base::RunLoop().RunUntilIdle();
  manager_->RemoveObserver(&observer);

  remote1.reset();
  remote2.reset();
  base::RunLoop().RunUntilIdle();
}

// Verifies that multi-WebContents batch transitions fire Tier 1 per-WebContents
// hooks as each WebContents completes, and fire the Tier 2 process-wide barrier
// hook (OnAllWebContentsVisible) only once ALL participating WebContents have
// finished.
TEST_F(CobaltLifecycleManagerTest, MultiWebContentsProcessBarrierReveal) {
  std::unique_ptr<content::WebContents> contents1 =
      CreateScopedTestWebContents();
  content::RenderFrameHostTester::For(contents1->GetPrimaryMainFrame())
      ->InitializeRenderFrameIfNeeded();

  std::unique_ptr<content::WebContents> contents2 =
      CreateScopedTestWebContents();
  content::RenderFrameHostTester::For(contents2->GetPrimaryMainFrame())
      ->InitializeRenderFrameIfNeeded();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote1;
  manager_->BindReceiver(contents1->GetPrimaryMainFrame(),
                         remote1.BindNewPipeAndPassReceiver());
  remote1->OnFrameReady();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote2;
  manager_->BindReceiver(contents2->GetPrimaryMainFrame(),
                         remote2.BindNewPipeAndPassReceiver());
  remote2->OnFrameReady();

  base::RunLoop().RunUntilIdle();

  MockCobaltLifecycleObserver observer;
  manager_->AddObserver(&observer);

  // Batch wait for both WebContents.
  std::vector<content::WebContents*> batch = {contents1.get(), contents2.get()};
  manager_->StartWaitingForAck(batch, PendingAck::kReveal);

  // Neither process barrier nor second window hook should fire when only
  // frame 1 completes.
  EXPECT_CALL(observer, OnWebContentsVisible(contents1.get())).Times(1);
  EXPECT_CALL(observer, OnWebContentsVisible(contents2.get())).Times(0);
  EXPECT_CALL(observer, OnAllWebContentsVisible()).Times(0);

  remote1->OnPageVisibilityChanged(true);
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&observer);

  // When frame 2 completes, both the per-WebContents hook and the process
  // barrier must fire.
  EXPECT_CALL(observer, OnWebContentsVisible(contents2.get())).Times(1);
  EXPECT_CALL(observer, OnAllWebContentsVisible()).Times(1);

  remote2->OnPageVisibilityChanged(true);
  base::RunLoop().RunUntilIdle();

  manager_->RemoveObserver(&observer);
  remote1.reset();
  remote2.reset();
  base::RunLoop().RunUntilIdle();
}

// Verifies that if one of the WebContents is destroyed mid-transition, it is
// cleanly erased from the process barrier set, allowing the barrier to satisfy
// once the remaining WebContents completes.
TEST_F(CobaltLifecycleManagerTest,
       MultiWebContentsDestroyedCompletesProcessBarrier) {
  std::unique_ptr<content::WebContents> contents1 =
      CreateScopedTestWebContents();
  content::RenderFrameHostTester::For(contents1->GetPrimaryMainFrame())
      ->InitializeRenderFrameIfNeeded();

  std::unique_ptr<content::WebContents> contents2 =
      CreateScopedTestWebContents();
  content::RenderFrameHostTester::For(contents2->GetPrimaryMainFrame())
      ->InitializeRenderFrameIfNeeded();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote1;
  manager_->BindReceiver(contents1->GetPrimaryMainFrame(),
                         remote1.BindNewPipeAndPassReceiver());
  remote1->OnFrameReady();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote2;
  manager_->BindReceiver(contents2->GetPrimaryMainFrame(),
                         remote2.BindNewPipeAndPassReceiver());
  remote2->OnFrameReady();

  base::RunLoop().RunUntilIdle();

  MockCobaltLifecycleObserver observer;
  manager_->AddObserver(&observer);

  std::vector<content::WebContents*> batch = {contents1.get(), contents2.get()};
  manager_->StartWaitingForAck(batch, PendingAck::kReveal);

  // Destroy contents2 while waiting.
  contents2.reset();
  base::RunLoop().RunUntilIdle();

  // Completing contents1 should now satisfy the entire process barrier.
  EXPECT_CALL(observer, OnWebContentsVisible(contents1.get())).Times(1);
  EXPECT_CALL(observer, OnAllWebContentsVisible()).Times(1);

  remote1->OnPageVisibilityChanged(true);
  base::RunLoop().RunUntilIdle();

  manager_->RemoveObserver(&observer);
  remote1.reset();
  base::RunLoop().RunUntilIdle();
}

// Verifies that initial transition notifications (OnStartWaitingForReveal) are
// dispatched asynchronously via posted tasks. This prevents re-entrancy issues
// when observers modify lifecycle state during direct call stacks.
TEST_F(CobaltLifecycleManagerTest, ObserverNotificationIsDeferred) {
  std::unique_ptr<content::WebContents> contents =
      CreateScopedTestWebContents();
  content::RenderFrameHostTester::For(contents->GetPrimaryMainFrame())
      ->InitializeRenderFrameIfNeeded();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote;
  manager_->BindReceiver(contents->GetPrimaryMainFrame(),
                         remote.BindNewPipeAndPassReceiver());
  remote->OnFrameReady();
  base::RunLoop().RunUntilIdle();

  MockCobaltLifecycleObserver observer;
  manager_->AddObserver(&observer);

  // Verify that notification is not delivered synchronously.
  EXPECT_CALL(observer, OnStartWaitingForReveal(contents.get())).Times(0);
  manager_->StartWaitingForAck(contents.get(), PendingAck::kReveal);
  testing::Mock::VerifyAndClearExpectations(&observer);

  // Verify that notification is delivered asynchronously on the run loop.
  EXPECT_CALL(observer, OnStartWaitingForReveal(contents.get())).Times(1);
  base::RunLoop().RunUntilIdle();
  manager_->RemoveObserver(&observer);

  remote.reset();
  base::RunLoop().RunUntilIdle();
}

// Verifies that if a main frame unregisters or disconnects while a lifecycle
// wait is pending, the transition completes automatically so observer state
// machines do not hang indefinitely waiting for dead frames.
TEST_F(CobaltLifecycleManagerTest, MainFrameUnregistrationWhileWaiting) {
  std::unique_ptr<content::WebContents> contents =
      CreateScopedTestWebContents();
  content::RenderFrameHostTester::For(contents->GetPrimaryMainFrame())
      ->InitializeRenderFrameIfNeeded();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote;
  manager_->BindReceiver(contents->GetPrimaryMainFrame(),
                         remote.BindNewPipeAndPassReceiver());
  remote->OnFrameReady();
  base::RunLoop().RunUntilIdle();

  MockCobaltLifecycleObserver observer;
  manager_->AddObserver(&observer);

  manager_->StartWaitingForAck(contents.get(), PendingAck::kReveal);

  // Expect OnWebContentsVisible to be called when the main frame is
  // unregistered.
  EXPECT_CALL(observer, OnWebContentsVisible(contents.get())).Times(1);
  EXPECT_CALL(observer, OnAllWebContentsVisible()).Times(1);

  // Resetting the remote destroys the receiver, which unregisters the frame.
  remote.reset();
  base::RunLoop().RunUntilIdle();

  manager_->RemoveObserver(&observer);
}

// Verifies that if a WebContents has no active connected frames when a
// transition is requested, the manager completes the transition immediately.
TEST_F(CobaltLifecycleManagerTest, ImmediateCompletion) {
  std::unique_ptr<content::WebContents> contents =
      CreateScopedTestWebContents();

  MockCobaltLifecycleObserver observer;
  manager_->AddObserver(&observer);

  // Expect immediate completion callback.
  EXPECT_CALL(observer, OnWebContentsVisible(contents.get())).Times(1);
  EXPECT_CALL(observer, OnAllWebContentsVisible()).Times(1);

  manager_->StartWaitingForAck(contents.get(), PendingAck::kReveal);
  base::RunLoop().RunUntilIdle();

  manager_->RemoveObserver(&observer);
}

// Verifies that attempting to unfreeze when a frame is not live (e.g. during
// mock unit tests or very early initialization before renderer creation) does
// not crash or trigger invalid IPC re-bindings on dead RenderFrameHost objects.
TEST_F(CobaltLifecycleManagerTest,
       StartWaitingForUnfreezeWithDeadFrameDoesNotCrash) {
  std::unique_ptr<content::WebContents> contents =
      CreateScopedTestWebContents();
  // Do not call InitializeRenderFrameIfNeeded(), ensuring IsRenderFrameLive()
  // returns false.

  manager_->StartWaitingForAck(contents.get(), PendingAck::kUnfreeze);
  base::RunLoop().RunUntilIdle();
}

// Verifies that when a frame is rebound to a new Mojo pipe during navigation or
// reload, disconnecting the old pipe asynchronously does not unregister the
// active frame from the manager while a valid connection still exists.
TEST_F(CobaltLifecycleManagerTest,
       ReceiverDisconnectDoesNotUnregisterActiveFrame) {
  std::unique_ptr<content::WebContents> contents =
      CreateScopedTestWebContents();
  content::RenderFrameHostTester::For(contents->GetPrimaryMainFrame())
      ->InitializeRenderFrameIfNeeded();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote1;
  manager_->BindReceiver(contents->GetPrimaryMainFrame(),
                         remote1.BindNewPipeAndPassReceiver());
  remote1->OnFrameReady();
  base::RunLoop().RunUntilIdle();

  // Establish a re-bind by creating a second remote for the same frame.
  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> remote2;
  manager_->BindReceiver(contents->GetPrimaryMainFrame(),
                         remote2.BindNewPipeAndPassReceiver());
  base::RunLoop().RunUntilIdle();

  // Disconnect the first remote, simulating the asynchronous teardown of the
  // old pipe.
  remote1.reset();
  base::RunLoop().RunUntilIdle();

  // The frame remains registered because remote2 is active.
  MockCobaltLifecycleObserver observer;
  manager_->AddObserver(&observer);

  manager_->StartWaitingForAck(contents.get(), PendingAck::kReveal);

  // Transition waits because remote2 has not yet reported its visibility ACK.
  EXPECT_CALL(observer, OnWebContentsVisible(contents.get())).Times(0);
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&observer);

  // Disconnecting the active remote unregisters the frame and completes the
  // wait.
  EXPECT_CALL(observer, OnWebContentsVisible(contents.get())).Times(1);
  EXPECT_CALL(observer, OnAllWebContentsVisible()).Times(1);
  remote2.reset();
  base::RunLoop().RunUntilIdle();

  manager_->RemoveObserver(&observer);
}

// Verifies that when a WebContents contains active subframes (iframes),
// StartWaitingForAck only tracks and waits for the primary main frame.
// Subframes are excluded because they may be throttled or paused by the Blink
// scheduler, which would otherwise prevent transition completion.
TEST_F(CobaltLifecycleManagerTest, StartWaitingForAckIgnoresSubframes) {
  std::unique_ptr<content::WebContents> contents =
      CreateScopedTestWebContents();
  content::RenderFrameHost* main_rfh = contents->GetPrimaryMainFrame();
  content::RenderFrameHostTester::For(main_rfh)
      ->InitializeRenderFrameIfNeeded();

  content::RenderFrameHost* child_rfh = AddChildFrame(main_rfh);

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> main_remote;
  manager_->BindReceiver(main_rfh, main_remote.BindNewPipeAndPassReceiver());
  main_remote->OnFrameReady();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> child_remote;
  manager_->BindReceiver(child_rfh, child_remote.BindNewPipeAndPassReceiver());
  child_remote->OnFrameReady();

  base::RunLoop().RunUntilIdle();

  MockCobaltLifecycleObserver observer;
  manager_->AddObserver(&observer);

  manager_->StartWaitingForAck(contents.get(), PendingAck::kReveal);

  // Sending the ACK from the primary main frame alone must satisfy the wait.
  EXPECT_CALL(observer, OnWebContentsVisible(contents.get())).Times(1);
  EXPECT_CALL(observer, OnAllWebContentsVisible()).Times(1);
  main_remote->OnPageVisibilityChanged(true);
  base::RunLoop().RunUntilIdle();

  manager_->RemoveObserver(&observer);
}

// Verifies that if a subframe dynamically registers while a lifecycle
// transition wait is already active, the subframe is ignored and does not get
// added to the pending acknowledgment set.
TEST_F(CobaltLifecycleManagerTest,
       SubframeRegistrationDuringActiveWaitIsIgnored) {
  std::unique_ptr<content::WebContents> contents =
      CreateScopedTestWebContents();
  content::RenderFrameHost* main_rfh = contents->GetPrimaryMainFrame();
  content::RenderFrameHostTester::For(main_rfh)
      ->InitializeRenderFrameIfNeeded();

  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> main_remote;
  manager_->BindReceiver(main_rfh, main_remote.BindNewPipeAndPassReceiver());
  main_remote->OnFrameReady();
  base::RunLoop().RunUntilIdle();

  MockCobaltLifecycleObserver observer;
  manager_->AddObserver(&observer);

  manager_->StartWaitingForAck(contents.get(), PendingAck::kReveal);

  // Dynamically attach and register a child subframe mid-transition.
  content::RenderFrameHost* child_rfh = AddChildFrame(main_rfh);
  mojo::Remote<cobalt::mojom::CobaltLifecycleObserver> child_remote;
  manager_->BindReceiver(child_rfh, child_remote.BindNewPipeAndPassReceiver());
  child_remote->OnFrameReady();
  base::RunLoop().RunUntilIdle();

  // ACKing the main frame must complete the transition without waiting on
  // child_remote.
  EXPECT_CALL(observer, OnWebContentsVisible(contents.get())).Times(1);
  EXPECT_CALL(observer, OnAllWebContentsVisible()).Times(1);
  main_remote->OnPageVisibilityChanged(true);
  base::RunLoop().RunUntilIdle();

  manager_->RemoveObserver(&observer);
}

}  // namespace cobalt
