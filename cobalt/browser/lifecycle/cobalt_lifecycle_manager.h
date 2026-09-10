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

#ifndef COBALT_BROWSER_LIFECYCLE_COBALT_LIFECYCLE_MANAGER_H_
#define COBALT_BROWSER_LIFECYCLE_COBALT_LIFECYCLE_MANAGER_H_

#include <utility>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "cobalt/browser/lifecycle/public/mojom/cobalt_lifecycle.mojom.h"
#include "cobalt/common/cobalt_thread_checker.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_map.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_set.h"

namespace content {
class WebContents;
class RenderFrameHost;
class NavigationHandle;
}  // namespace content

namespace cobalt {

class H5vccRuntimeImpl;

struct FrameContext {
  content::GlobalRenderFrameHostId frame_id;
};

enum class PendingAck {
  kNone,
  kReveal,
  kConceal,
  kBlur,
  kUnfreeze,
  kCookieFlush,
};

class CobaltLifecycleManagerObserver {
 public:
  // =========================================================================
  // Tier 1: Per-WebContents Granular Hooks
  // Fired when an individual WebContents completes its frame transitions.
  // =========================================================================

  // Called when all frames of a specific WebContents have completed reveal.
  virtual void OnWebContentsVisible(content::WebContents* web_contents) {}

  // Proactively map/show the platform window during reveal transitions.
  virtual void OnProactiveMapWindow(content::WebContents* web_contents) {}

  // Called when the main frame of a WebContents is registered.
  virtual void OnMainFrameRegistered(content::WebContents* web_contents) {}

  // Called when a WebContents starts waiting for reveal.
  virtual void OnStartWaitingForReveal(content::WebContents* web_contents) {}

  // Called when all frames of a specific WebContents have acknowledged conceal.
  virtual void OnWebContentsConcealed(content::WebContents* web_contents) {}

  // Called when all frames of a specific WebContents have acknowledged blur.
  virtual void OnWebContentsBlurred(content::WebContents* web_contents) {}

  // Called when all frames of a specific WebContents have acknowledged resume.
  virtual void OnWebContentsResumed(content::WebContents* web_contents) {}

  // =========================================================================
  // Tier 2: Process-Wide Barrier Synchronization Hooks
  // Fired ONLY when ALL active WebContents participating in the transition
  // have reached completion.
  // =========================================================================

  // Called when ALL participating WebContents have completed reveal and layout.
  virtual void OnAllWebContentsVisible() {}

  // Called when ALL participating WebContents have completed conceal.
  virtual void OnAllWebContentsConcealed() {}

  // Called when ALL participating WebContents have completed blur.
  virtual void OnAllWebContentsBlurred() {}

  // Called when ALL participating WebContents have completed resume.
  virtual void OnAllWebContentsResumed() {}

  // Called after GPU resources and EGLDisplay are completely destroyed on GPU
  // thread.
  virtual void OnConcealCompleted() {}

 protected:
  virtual ~CobaltLifecycleManagerObserver() = default;
};

// CobaltLifecycleManager coordinates all renderer-side asynchronous lifecycle
// ACKs (such as Reveal, Blur, Conceal, and Freeze/Unfreeze transitions) across
// multiple frames. It ensures that browser-side state changes are safely
// blocked or deferred until the renderers have acknowledged completion,
// preventing race conditions and ensuring a predictable transition sequence
// (e.g., preventing "early focus" bugs before layout is complete, or executing
// Conceal checks before the thread is frozen).
//
// Note: "Reveal" refers to the Starboard lifecycle concept where the
// application becomes visible to the user (e.g., after being concealed), while
// "Focus" refers to the standard window/input focus.
//
// It takes WebContents* to scope the tracking per window/tab. This ensures that
// a background tab failing to become visible does not block the reveal and
// focus of the active foreground tab.
//
// A `WebContents` typically represents a "tab" or window. Each tab can have
// multiple `H5vccRuntimeImpl` instances (e.g., one for the main frame and one
// for each iframe). Since `H5vccRuntimeImpl` is a `DocumentService` bound to a
// frame's lifetime, the manager is needed to coordinate across all these
// isolated contexts within the same tab.
//
// It is a global singleton to avoid layering violations between cobalt/app
// and cobalt/shell.
//
// Threading Model:
// This class is thread-affine and must only be accessed on the Browser UI
// thread.
class CobaltLifecycleManager : public cobalt::mojom::CobaltLifecycleObserver {
 public:
  // Returns the singleton instance.
  // This is a global singleton rather than being owned by content::Shell
  // to avoid a layering violation: AppEventDelegate (in cobalt/app) needs
  // to observe reveal ACKs, but it cannot depend on content::Shell
  // (in cobalt/shell).
  static CobaltLifecycleManager* GetInstance();

  // Resets the singleton instance for testing.
  void ResetForTesting();

  CobaltLifecycleManager(const CobaltLifecycleManager&) = delete;
  CobaltLifecycleManager& operator=(const CobaltLifecycleManager&) = delete;

  // Note: The following methods take raw WebContents* because they are called
  // synchronously from H5vccRuntimeImpl (which is scoped to the frame's
  // lifetime). Therefore, the WebContents* is guaranteed to be valid during the
  // call.

  // Called when a new document is created and H5vccRuntimeImpl is bound to it.
  // Registers the frame with the manager to track its visibility.
  void RegisterFrame(content::WebContents* web_contents,
                     content::RenderFrameHost* frame);

  // Called when a document is destroyed or navigated away.
  // Removes the frame from tracking.
  void UnregisterFrame(content::WebContents* web_contents,
                       content::RenderFrameHost* frame);

  void BindReceiver(
      content::RenderFrameHost* frame,
      mojo::PendingReceiver<cobalt::mojom::CobaltLifecycleObserver> receiver);

  // Proactively creates the tracker for the specified WebContents to monitor
  // frame lifecycle events from startup.
  void InitializeTracker(content::WebContents* web_contents);

  // cobalt::mojom::CobaltLifecycleObserver impl.
  void OnPageVisibilityChanged(bool visible) override;
  void OnPageBlurred() override;
  void OnPageFocused() override;
  void OnPageResumed() override;
  void OnFrameReady() override;

  // Adds/removes observers.
  void AddObserver(CobaltLifecycleManagerObserver* observer);
  void RemoveObserver(CobaltLifecycleManagerObserver* observer);

  // Called when background GPU cleanup and platform window conceal complete.
  void OnConcealCompleted();

  // Called to start waiting for a specific ACK type across a batch of
  // WebContents.
  void StartWaitingForAck(
      const std::vector<content::WebContents*>& web_contents_list,
      PendingAck ack_type);

  // Convenience overload for a single WebContents.
  void StartWaitingForAck(content::WebContents* web_contents,
                          PendingAck ack_type);

 private:
  friend class base::NoDestructor<CobaltLifecycleManager>;

  CobaltLifecycleManager();
  ~CobaltLifecycleManager();

  std::pair<content::RenderFrameHost*, content::WebContents*>
  GetCurrentContext();
  void OnMojoDisconnect();

  void CompleteAckImmediately(content::WebContents* web_contents,
                              PendingAck ack_type);

  void CheckCompletion(content::WebContents* web_contents);
  void CheckProcessCompletion(PendingAck ack_type);
  void NotifyStartWaitingForReveal(
      base::WeakPtr<content::WebContents> web_contents);
  void OnAckTimeout(base::WeakPtr<content::WebContents> web_contents,
                    PendingAck ack_type);
  void OnWebContentsDestroyed(content::WebContents* web_contents);

  // WebContentsTracker tracks the lifecycle state of all frames within a
  // specific WebContents. It maintains the state reported by the renderer via
  // Mojo, allowing the manager to check if a requested ACK condition has
  // already been met by early-arriving messages.
  class WebContentsTracker : public content::WebContentsObserver {
   public:
    WebContentsTracker(content::WebContents* web_contents,
                       CobaltLifecycleManager* manager);
    ~WebContentsTracker() override;

    // content::WebContentsObserver overrides.
    void RenderFrameCreated(
        content::RenderFrameHost* render_frame_host) override;
    void RenderFrameDeleted(
        content::RenderFrameHost* render_frame_host) override;
    void WebContentsDestroyed() override;
    void DidFinishNavigation(
        content::NavigationHandle* navigation_handle) override;

    // Checks if the remote controller for the specified frame is bound and
    // connected.
    bool IsConnected(content::RenderFrameHost* frame) const;

    // Proactively re-binds the remote controller for the specified frame.
    void Rebind(content::RenderFrameHost* frame);

    // Called when the remote controller for a frame disconnects.
    void OnControllerDisconnect(content::RenderFrameHost* frame);

   private:
    CobaltLifecycleManager* manager_;

    // Map of active frames to their remote controllers.
    absl::flat_hash_map<content::RenderFrameHost*,
                        mojo::Remote<cobalt::mojom::CobaltLifecycleController>>
        controllers_;
  };

  WebContentsTracker* GetOrCreateTracker(content::WebContents* web_contents);
  void TrackPrimaryMainFrameForAck(content::WebContents* web_contents,
                                   WebContentsTracker* tracker,
                                   PendingAck ack_type);

  absl::flat_hash_map<content::WebContents*,
                      absl::flat_hash_set<content::RenderFrameHost*>>
      frames_;

  // The single set of frames we are waiting for an ACK from.
  absl::flat_hash_map<content::WebContents*,
                      absl::flat_hash_set<content::RenderFrameHost*>>
      pending_ack_frames_;

  // What we are waiting for per WebContents.
  absl::flat_hash_map<content::WebContents*, PendingAck> pending_acks_;

  // Active process-wide pending transition type.
  PendingAck current_process_ack_ = PendingAck::kNone;

  // Set of WebContents currently participating in the active process-wide
  // transition.
  absl::flat_hash_set<content::WebContents*> pending_transition_web_contents_;

  absl::flat_hash_map<content::WebContents*,
                      std::unique_ptr<WebContentsTracker>>
      trackers_;

  base::ObserverList<CobaltLifecycleManagerObserver>::Unchecked observers_;

  // Mojo receiver set for the CobaltLifecycleObserver interface. Each receiver
  // corresponds to a renderer-side frame that has registered to send back
  // lifecycle ACKs. The FrameContext associated with each receiver tracks the
  // frame's WebContents and GlobalRenderFrameHostId.
  mojo::ReceiverSet<cobalt::mojom::CobaltLifecycleObserver, FrameContext>
      receivers_;

  // Map tracking the Mojo ReceiverIds associated with each WebContents. Since
  // mojo::ReceiverSet does not provide a public API to query or iterate over
  // receivers by their associated context (WebContents*), we must track the
  // ReceiverIds explicitly. This allows us to cleanly remove only the receivers
  // associated with a specific WebContents when it is destroyed.
  absl::flat_hash_map<content::WebContents*, std::vector<mojo::ReceiverId>>
      receiver_ids_;

  // Tracks the number of active Mojo lifecycle observer receivers (connections)
  // associated with each RenderFrameHost (keyed by its global ID). This is used
  // to dynamically monitor frame connection lifetimes and determine when a
  // frame is actively participating in the lifecycle transition handshake.
  absl::flat_hash_map<content::GlobalRenderFrameHostId, int>
      active_receiver_counts_;

  base::WeakPtrFactory<CobaltLifecycleManager> weak_factory_{this};

  COBALT_THREAD_CHECKER(thread_checker_);
};

}  // namespace cobalt

#endif  // COBALT_BROWSER_LIFECYCLE_COBALT_LIFECYCLE_MANAGER_H_
