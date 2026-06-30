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

#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/containers/small_map.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "cobalt/browser/lifecycle/public/mojom/cobalt_lifecycle.mojom.h"
#include "cobalt/common/cobalt_thread_checker.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"

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
  // Called when all frames of a specific WebContents have completed reveal.
  virtual void OnAllFramesVisible(content::WebContents* web_contents) = 0;

  // Called to proactively map/show the platform window during reveal
  // transitions to enable standard Chromium visibility IPCs to propagate.
  virtual void OnProactiveMapWindow(content::WebContents* web_contents) {}

  // Called when the main frame of a WebContents is registered.
  virtual void OnMainFrameRegistered(content::WebContents* web_contents) {}

  // Called when a WebContents starts waiting for reveal. This is used by
  // observers (like AppEventDelegate) to know that they should expect
  // a corresponding OnAllFramesVisible call later, and thus should defer
  // focus if it arrives too early.
  virtual void OnStartWaitingForReveal(content::WebContents* web_contents) {}

  // Called when a WebContents has completed conceal (hidden).
  virtual void OnAllFramesConcealed(content::WebContents* web_contents) {}

  // Called when a WebContents has completed blur.
  virtual void OnAllFramesBlurred(content::WebContents* web_contents) {}

  // Called when all frames of a specific WebContents have completed resume.
  virtual void OnAllFramesResumed(content::WebContents* web_contents) {}

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

  // Called to start waiting for a specific ACK type.
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

    // Methods to update the tracked state of a specific frame.
    void SetResumed(content::RenderFrameHost* frame);
    void SetVisible(content::RenderFrameHost* frame, bool visible);
    void SetFocused(content::RenderFrameHost* frame, bool focused);

    bool IsComplete(PendingAck ack_type) const;

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
    std::map<content::RenderFrameHost*,
             mojo::Remote<cobalt::mojom::CobaltLifecycleController>>
        controllers_;

    // Sets to track the current state of each frame. A frame is considered
    // to have achieved the state if it is present in the corresponding set.
    base::flat_set<content::RenderFrameHost*> resumed_frames_;
    base::flat_set<content::RenderFrameHost*> visible_frames_;
    base::flat_set<content::RenderFrameHost*> focused_frames_;
  };

  WebContentsTracker* GetOrCreateTracker(content::WebContents* web_contents);

  // Note: We use raw WebContents* as keys here instead of WeakPtr<WebContents>
  // because base::WeakPtr does not implement operator< in this version of base,
  // making it unusable as a std::map key without a custom comparator. A custom
  // comparator comparing raw pointers would be unsafe because once the
  // WebContents is destroyed, all WeakPtrs pointing to it become null and
  // compare equal, violating strict weak ordering. We rely on
  // OnWebContentsDestroyed for cleanup.
  base::small_map<std::map<content::WebContents*, content::RenderFrameHost*>>
      main_frames_;
  base::small_map<
      std::map<content::WebContents*, std::set<content::RenderFrameHost*>>>
      frames_;

  // The single set of frames we are waiting for an ACK from.
  base::small_map<
      std::map<content::WebContents*, std::set<content::RenderFrameHost*>>>
      pending_ack_frames_;

  // What we are waiting for per WebContents.
  base::small_map<std::map<content::WebContents*, PendingAck>> pending_acks_;

  base::small_map<
      std::map<content::WebContents*, std::unique_ptr<WebContentsTracker>>>
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
  std::map<content::WebContents*, std::vector<mojo::ReceiverId>> receiver_ids_;

  // Tracks the number of active Mojo lifecycle observer receivers (connections)
  // associated with each RenderFrameHost (keyed by its global ID). This is used
  // to dynamically monitor frame connection lifetimes and determine when a
  // frame is actively participating in the lifecycle transition handshake.
  std::map<content::GlobalRenderFrameHostId, int> active_receiver_counts_;

  base::WeakPtrFactory<CobaltLifecycleManager> weak_factory_{this};

  COBALT_THREAD_CHECKER(thread_checker_);
};

}  // namespace cobalt

#endif  // COBALT_BROWSER_LIFECYCLE_COBALT_LIFECYCLE_MANAGER_H_
