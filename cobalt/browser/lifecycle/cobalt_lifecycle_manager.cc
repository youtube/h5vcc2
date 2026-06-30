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

#include <algorithm>

#include "base/no_destructor.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "services/service_manager/public/cpp/interface_provider.h"

namespace cobalt {

// static
CobaltLifecycleManager* CobaltLifecycleManager::GetInstance() {
  static base::NoDestructor<CobaltLifecycleManager> instance;
  return instance.get();
}

CobaltLifecycleManager::CobaltLifecycleManager() {
  receivers_.set_disconnect_handler(base::BindRepeating(
      &CobaltLifecycleManager::OnMojoDisconnect, base::Unretained(this)));
  COBALT_DETACH_FROM_THREAD(thread_checker_);
}
CobaltLifecycleManager::~CobaltLifecycleManager() = default;

void CobaltLifecycleManager::ResetForTesting() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  receivers_.Clear();
  receiver_ids_.clear();
  trackers_.clear();
  main_frames_.clear();
  frames_.clear();
  pending_ack_frames_.clear();
  pending_acks_.clear();
  observers_.Clear();
}

void CobaltLifecycleManager::BindReceiver(
    content::RenderFrameHost* frame,
    mojo::PendingReceiver<cobalt::mojom::CobaltLifecycleObserver> receiver) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(frame);
  mojo::ReceiverId id =
      receivers_.Add(this, std::move(receiver), {frame->GetGlobalId()});
  active_receiver_counts_[frame->GetGlobalId()]++;
  receiver_ids_[web_contents].push_back(id);
}

std::pair<content::RenderFrameHost*, content::WebContents*>
CobaltLifecycleManager::GetCurrentContext() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  FrameContext context = receivers_.current_context();
  content::RenderFrameHost* frame =
      content::RenderFrameHost::FromID(context.frame_id);
  if (!frame) {
    return {nullptr, nullptr};
  }
  return {frame, content::WebContents::FromRenderFrameHost(frame)};
}

void CobaltLifecycleManager::InitializeTracker(
    content::WebContents* web_contents) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetOrCreateTracker(web_contents);
}

void CobaltLifecycleManager::OnMojoDisconnect() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  FrameContext context = receivers_.current_context();

  auto active_it = active_receiver_counts_.find(context.frame_id);
  if (active_it != active_receiver_counts_.end()) {
    active_it->second--;
    if (active_it->second <= 0) {
      active_receiver_counts_.erase(active_it);
      content::RenderFrameHost* frame =
          content::RenderFrameHost::FromID(context.frame_id);
      if (frame) {
        UnregisterFrame(content::WebContents::FromRenderFrameHost(frame),
                        frame);
      }
    }
  }

  auto [frame, web_contents] = GetCurrentContext();
  mojo::ReceiverId id = receivers_.current_receiver();
  if (web_contents) {
    auto it = receiver_ids_.find(web_contents);
    if (it != receiver_ids_.end()) {
      auto& ids = it->second;
      ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
      if (ids.empty()) {
        receiver_ids_.erase(it);
      }
    }
  }
}

CobaltLifecycleManager::WebContentsTracker::WebContentsTracker(
    content::WebContents* web_contents,
    CobaltLifecycleManager* manager)
    : content::WebContentsObserver(web_contents), manager_(manager) {}

CobaltLifecycleManager::WebContentsTracker::~WebContentsTracker() = default;

void CobaltLifecycleManager::WebContentsTracker::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  if (!render_frame_host->IsRenderFrameLive()) {
    return;
  }
  mojo::Remote<cobalt::mojom::CobaltLifecycleController> controller;
  render_frame_host->GetRemoteInterfaces()->GetInterface(
      controller.BindNewPipeAndPassReceiver());

  // Register the browser-side CobaltLifecycleManager as an observer to the
  // renderer-side CobaltLifecycleController. This establishes the two-way
  // communication channel needed for lifecycle ACKs.
  mojo::PendingRemote<cobalt::mojom::CobaltLifecycleObserver> observer;
  manager_->BindReceiver(render_frame_host,
                         observer.InitWithNewPipeAndPassReceiver());
  controller->SetObserver(std::move(observer));

  controllers_[render_frame_host] = std::move(controller);

  controllers_[render_frame_host].set_disconnect_handler(
      base::BindOnce(&WebContentsTracker::OnControllerDisconnect,
                     base::Unretained(this), render_frame_host));
}

void CobaltLifecycleManager::WebContentsTracker::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  controllers_.erase(render_frame_host);
  resumed_frames_.erase(render_frame_host);
  visible_frames_.erase(render_frame_host);
  focused_frames_.erase(render_frame_host);
  manager_->UnregisterFrame(web_contents(), render_frame_host);
}

void CobaltLifecycleManager::WebContentsTracker::WebContentsDestroyed() {
  manager_->OnWebContentsDestroyed(web_contents());
}

void CobaltLifecycleManager::WebContentsTracker::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // We only care about successful, committed cross-document navigations.
  // - If the navigation was aborted or failed to commit, the document state
  //   doesn't change, so we don't need to re-bind.
  // - Error pages (e.g. net errors) do not load the Cobalt web application
  //   and therefore do not host the renderer-side lifecycle controller.
  if (!navigation_handle->HasCommitted() || navigation_handle->IsErrorPage()) {
    return;
  }

  // Same-document navigations (e.g. hash changes) reuse the
  // existing document and JS context. The existing Mojo pipe remains valid.
  // We MUST skip re-binding here. If we re-bound:
  // 1. The new remote binding on the renderer would close the old pipe.
  // 2. The browser would receive a disconnect handler call for the old pipe
  //    and call UnregisterFrame.
  // 3. This would race with the new pipe's RegisterFrame call, potentially
  //    leaving the frame permanently unregistered in the browser.
  if (navigation_handle->IsSameDocument()) {
    return;
  }

  // For cross-document navigations, the new document in the renderer gets a
  // fresh JS context, invalidating the old Mojo pipe. We must proactively
  // re-bind the interface to the new frame host to resume lifecycle tracking.
  content::RenderFrameHost* rfh = navigation_handle->GetRenderFrameHost();
  if (rfh && rfh->IsRenderFrameLive()) {
    Rebind(rfh);
  }
}

void CobaltLifecycleManager::WebContentsTracker::SetResumed(
    content::RenderFrameHost* frame) {
  resumed_frames_.insert(frame);
}

void CobaltLifecycleManager::WebContentsTracker::SetVisible(
    content::RenderFrameHost* frame,
    bool visible) {
  if (visible) {
    visible_frames_.insert(frame);
  } else {
    visible_frames_.erase(frame);
  }
}

void CobaltLifecycleManager::WebContentsTracker::SetFocused(
    content::RenderFrameHost* frame,
    bool focused) {
  if (focused) {
    focused_frames_.insert(frame);
  } else {
    focused_frames_.erase(frame);
  }
}

bool CobaltLifecycleManager::WebContentsTracker::IsComplete(
    PendingAck ack_type) const {
  auto it = manager_->frames_.find(web_contents());
  if (it == manager_->frames_.end()) {
    return true;
  }
  const auto& all_frames = it->second;

  switch (ack_type) {
    case PendingAck::kUnfreeze:
      for (auto* frame : all_frames) {
        if (resumed_frames_.find(frame) == resumed_frames_.end()) {
          return false;
        }
      }
      return true;
    case PendingAck::kReveal:
      for (auto* frame : all_frames) {
        if (visible_frames_.find(frame) == visible_frames_.end()) {
          return false;
        }
      }
      return true;
    case PendingAck::kConceal:
      for (auto* frame : all_frames) {
        if (visible_frames_.find(frame) != visible_frames_.end()) {
          return false;
        }
      }
      return true;
    case PendingAck::kBlur:
      for (auto* frame : all_frames) {
        if (focused_frames_.find(frame) != focused_frames_.end()) {
          return false;
        }
      }
      return true;
    default:
      return false;
  }
}

bool CobaltLifecycleManager::WebContentsTracker::IsConnected(
    content::RenderFrameHost* frame) const {
  auto it = controllers_.find(frame);
  if (it == controllers_.end()) {
    return false;
  }
  return it->second.is_bound() && it->second.is_connected();
}

void CobaltLifecycleManager::WebContentsTracker::Rebind(
    content::RenderFrameHost* frame) {
  LOG(INFO) << "WebContentsTracker::Rebind: Proactively re-binding frame="
            << frame;
  mojo::Remote<cobalt::mojom::CobaltLifecycleController> controller;
  frame->GetRemoteInterfaces()->GetInterface(
      controller.BindNewPipeAndPassReceiver());

  // Re-register the browser as an observer after re-binding the interface.
  mojo::PendingRemote<cobalt::mojom::CobaltLifecycleObserver> observer;
  // Track the ReceiverId associated with the WebContents for clean teardown.
  manager_->BindReceiver(frame, observer.InitWithNewPipeAndPassReceiver());
  controller->SetObserver(std::move(observer));

  controllers_[frame] = std::move(controller);

  controllers_[frame].set_disconnect_handler(
      base::BindOnce(&WebContentsTracker::OnControllerDisconnect,
                     base::Unretained(this), frame));
}

void CobaltLifecycleManager::WebContentsTracker::OnControllerDisconnect(
    content::RenderFrameHost* frame) {
  LOG(WARNING) << "WebContentsTracker::OnControllerDisconnect for frame="
               << frame;
  resumed_frames_.erase(frame);
  visible_frames_.erase(frame);
  focused_frames_.erase(frame);

  // Proactively remove the disconnected frame from the parent manager's active
  // pending ACK sets.
  auto it = manager_->pending_ack_frames_.find(web_contents());
  if (it != manager_->pending_ack_frames_.end()) {
    it->second.erase(frame);
    manager_->CheckCompletion(web_contents());
  }
}

CobaltLifecycleManager::WebContentsTracker*
CobaltLifecycleManager::GetOrCreateTracker(content::WebContents* web_contents) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto it = trackers_.find(web_contents);
  if (it == trackers_.end()) {
    trackers_[web_contents] =
        std::make_unique<WebContentsTracker>(web_contents, this);
    web_contents->ForEachRenderFrameHost(
        [this, web_contents](content::RenderFrameHost* rfh) {
          trackers_[web_contents]->RenderFrameCreated(rfh);
        });

    return trackers_[web_contents].get();
  }
  return it->second.get();
}

void CobaltLifecycleManager::RegisterFrame(content::WebContents* web_contents,
                                           content::RenderFrameHost* frame) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  frames_[web_contents].insert(frame);
  GetOrCreateTracker(web_contents);

  // If we are currently waiting for an ACK, only dynamically track newly
  // registered Main Frames (e.g. during active navigations) to resolve the
  // focus/visibility event race. Subframes (iframes) must be excluded to
  // prevent Blink scheduler throttling deadlocks.
  PendingAck pending_ack = pending_acks_[web_contents];
  if (pending_ack != PendingAck::kNone && !frame->GetParent()) {
    pending_ack_frames_[web_contents].insert(frame);
  }

  if (!frame->GetParent()) {
    main_frames_[web_contents] = frame;
    for (auto& observer : observers_) {
      observer.OnMainFrameRegistered(web_contents);
    }
  }
}

void CobaltLifecycleManager::UnregisterFrame(content::WebContents* web_contents,
                                             content::RenderFrameHost* frame) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  frames_[web_contents].erase(frame);
  pending_ack_frames_[web_contents].erase(frame);

  if (main_frames_[web_contents] == frame) {
    main_frames_.erase(web_contents);
  }

  // Clean up active receiver counts for this frame.
  active_receiver_counts_.erase(frame->GetGlobalId());

  if (frames_[web_contents].empty()) {
    frames_.erase(web_contents);
    pending_ack_frames_.erase(web_contents);
    // Do NOT erase tracker here, as this might be called from the tracker
    // itself.
  }
  CheckCompletion(web_contents);
}

void CobaltLifecycleManager::OnPageVisibilityChanged(bool visible) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto [frame, web_contents] = GetCurrentContext();

  if (web_contents && frame) {
    auto* tracker = GetOrCreateTracker(web_contents);
    tracker->SetVisible(frame, visible);

    if ((pending_acks_[web_contents] == PendingAck::kReveal && visible) ||
        (pending_acks_[web_contents] == PendingAck::kConceal && !visible)) {
      pending_ack_frames_[web_contents].erase(frame);
    }
    CheckCompletion(web_contents);
  }
}

void CobaltLifecycleManager::OnPageBlurred() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto [frame, web_contents] = GetCurrentContext();

  if (web_contents && frame) {
    auto* tracker = GetOrCreateTracker(web_contents);
    tracker->SetFocused(frame, false);

    if (pending_acks_[web_contents] == PendingAck::kBlur) {
      pending_ack_frames_[web_contents].erase(frame);
    }
    CheckCompletion(web_contents);
  }
}

void CobaltLifecycleManager::OnPageFocused() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto [frame, web_contents] = GetCurrentContext();

  if (web_contents && frame) {
    auto* tracker = GetOrCreateTracker(web_contents);
    tracker->SetFocused(frame, true);
  }
}

void CobaltLifecycleManager::OnPageResumed() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto [frame, web_contents] = GetCurrentContext();

  if (web_contents && frame) {
    auto* tracker = GetOrCreateTracker(web_contents);
    tracker->SetResumed(frame);

    if (pending_acks_[web_contents] == PendingAck::kUnfreeze) {
      pending_ack_frames_[web_contents].erase(frame);
      CheckCompletion(web_contents);
    }
  }
}

void CobaltLifecycleManager::OnFrameReady() {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto [frame, web_contents] = GetCurrentContext();

  if (web_contents && frame) {
    RegisterFrame(web_contents, frame);
  }
}

void CobaltLifecycleManager::AddObserver(
    CobaltLifecycleManagerObserver* observer) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observers_.AddObserver(observer);
}

void CobaltLifecycleManager::RemoveObserver(
    CobaltLifecycleManagerObserver* observer) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observers_.RemoveObserver(observer);
}

void CobaltLifecycleManager::StartWaitingForAck(
    content::WebContents* web_contents,
    PendingAck ack_type) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  pending_acks_[web_contents] = ack_type;

  auto* tracker = GetOrCreateTracker(web_contents);

  if (ack_type == PendingAck::kReveal) {
    // Proactively tell observers to map the platform window first, which
    // enables standard Chromium visibility IPCs to propagate to the renderer!
    for (auto& observer : observers_) {
      observer.OnProactiveMapWindow(web_contents);
    }

    auto* main_frame = main_frames_[web_contents];
    if (main_frame) {
      pending_ack_frames_[web_contents].insert(main_frame);
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&CobaltLifecycleManager::NotifyStartWaitingForReveal,
                       base::Unretained(this), web_contents->GetWeakPtr()));
  } else {
    // For downward transitions, only track the active Primary Main Frame.
    // This excludes subframes (which can be aggressively throttled or paused
    // by Blink's scheduler) and older, navigated-away main frames that are
    // still leaking in memory pending deletion.
    auto* primary_main_frame = web_contents->GetPrimaryMainFrame();
    if (primary_main_frame) {
      bool connected = tracker->IsConnected(primary_main_frame);
      if (connected) {
        pending_ack_frames_[web_contents].insert(primary_main_frame);
      } else if (ack_type == PendingAck::kUnfreeze &&
                 primary_main_frame->IsRenderFrameLive()) {
        LOG(WARNING)
            << "StartWaitingForAck: Primary Main Frame not connected during "
               "Unfreeze! Re-binding frame="
            << primary_main_frame;
        tracker->Rebind(primary_main_frame);
      }
    }
  }

  if (pending_ack_frames_[web_contents].empty()) {
    // If there are no connected frames (e.g., during startup before any
    // frames are created or if all frames crashed), we complete the ACK
    // immediately to avoid hanging the transition.
    LOG(WARNING) << "StartWaitingForAck: No connected frames! Completing "
                    "immediately for "
                 << static_cast<int>(ack_type);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&CobaltLifecycleManager::CompleteAckImmediately,
                       weak_factory_.GetWeakPtr(), web_contents, ack_type));
    return;
  }
  // Post a 2-second timer as a fallback for all ACKs.
  content::GetUIThreadTaskRunner({})->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&CobaltLifecycleManager::OnAckTimeout,
                     base::Unretained(this), web_contents->GetWeakPtr(),
                     ack_type),
      base::Seconds(2));
}

// If we are waiting for reveal on this WebContents and all its registered
// frames have reported visible, it completes the reveal sequence for THIS
// WebContents:
// 1. Clearing the waiting flag in the platform delegate.
// 2. Making the specific WebContents visible to trigger visibility events in
// JS.
// 3. Notifying observers to unblock focus for this WebContents.
void CobaltLifecycleManager::CompleteAckImmediately(
    content::WebContents* web_contents,
    PendingAck ack_type) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (pending_acks_[web_contents] != ack_type) {
    return;
  }
  pending_acks_[web_contents] = PendingAck::kNone;
  pending_ack_frames_.erase(web_contents);

  switch (ack_type) {
    case PendingAck::kUnfreeze:
      for (auto& observer : observers_) {
        observer.OnAllFramesResumed(web_contents);
      }
      break;
    case PendingAck::kReveal:
      for (auto& observer : observers_) {
        observer.OnAllFramesVisible(web_contents);
      }
      break;
    case PendingAck::kConceal:
      for (auto& observer : observers_) {
        observer.OnAllFramesConcealed(web_contents);
      }
      break;
    case PendingAck::kBlur:
      for (auto& observer : observers_) {
        observer.OnAllFramesBlurred(web_contents);
      }
      break;
    default:
      break;
  }
}

void CobaltLifecycleManager::CheckCompletion(
    content::WebContents* web_contents) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  PendingAck pending_ack = pending_acks_[web_contents];
  if (pending_ack != PendingAck::kNone &&
      pending_ack_frames_[web_contents].empty()) {
    CompleteAckImmediately(web_contents, pending_ack);
  }
}

void CobaltLifecycleManager::NotifyStartWaitingForReveal(
    base::WeakPtr<content::WebContents> web_contents) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!web_contents) {
    return;
  }
  if (pending_acks_[web_contents.get()] != PendingAck::kReveal) {
    return;
  }
  for (auto& observer : observers_) {
    observer.OnStartWaitingForReveal(web_contents.get());
  }
}

void CobaltLifecycleManager::OnAckTimeout(
    base::WeakPtr<content::WebContents> web_contents,
    PendingAck ack_type) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!web_contents) {
    return;
  }
  content::WebContents* wc = web_contents.get();
  if (pending_acks_[wc] == ack_type) {
    LOG(WARNING) << "Timeout fired for ack = " << static_cast<int>(ack_type)
                 << ". Proceeding anyway.";
    pending_ack_frames_[wc].clear();
    CheckCompletion(wc);
  }
}

void CobaltLifecycleManager::OnWebContentsDestroyed(
    content::WebContents* web_contents) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // The WebContents is being destroyed. Proactively remove all its Mojo
  // receivers from `receivers_` BEFORE the WebContents and its RFHs go out of
  // scope.
  // Proactively clean up the active_receiver_counts_ map for all frames
  // belonging to this WebContents to avoid memory leaks.
  auto frames_it = frames_.find(web_contents);
  if (frames_it != frames_.end()) {
    for (auto* frame : frames_it->second) {
      active_receiver_counts_.erase(frame->GetGlobalId());
    }
  }
  auto it = receiver_ids_.find(web_contents);
  if (it != receiver_ids_.end()) {
    for (auto id : it->second) {
      receivers_.Remove(id);
    }
    receiver_ids_.erase(it);
  }
  trackers_.erase(web_contents);
  main_frames_.erase(web_contents);
  frames_.erase(web_contents);
  pending_ack_frames_.erase(web_contents);
  pending_acks_.erase(web_contents);
}

}  // namespace cobalt
