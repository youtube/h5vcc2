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

#include "third_party/blink/renderer/modules/cobalt/h5vcc_memory/h_5_vcc_memory.h"

#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"

namespace blink {

H5vccMemory::H5vccMemory(LocalDOMWindow& window)
    : ExecutionContextLifecycleObserver(window.GetExecutionContext()),
      remote_h5vcc_memory_(window.GetExecutionContext()),
      low_memory_receiver_(this, window.GetExecutionContext()) {}

void H5vccMemory::ContextDestroyed() {
  remote_h5vcc_memory_.reset();
  low_memory_receiver_.reset();
}

void H5vccMemory::EnsureRemoteIsBound() {
  DCHECK(GetExecutionContext());

  if (remote_h5vcc_memory_.is_bound()) {
    return;
  }

  auto task_runner =
      GetExecutionContext()->GetTaskRunner(TaskType::kMiscPlatformAPI);
  GetExecutionContext()->GetBrowserInterfaceBroker().GetInterface(
      remote_h5vcc_memory_.BindNewPipeAndPassReceiver(task_runner));
  remote_h5vcc_memory_.set_disconnect_handler(
      WTF::BindOnce(&H5vccMemory::OnConnectionError, WrapWeakPersistent(this)));
}

void H5vccMemory::OnConnectionError() {
  remote_h5vcc_memory_.reset();
}

void H5vccMemory::OnListenerConnectionError() {
  low_memory_receiver_.reset();
}

void H5vccMemory::OnLowMemory() {
  DispatchEvent(*Event::Create(event_type_names::kLowmemory));
}

void H5vccMemory::AddedEventListener(
    const AtomicString& event_type,
    RegisteredEventListener& registered_listener) {
  EventTarget::AddedEventListener(event_type, registered_listener);
  if (event_type == event_type_names::kLowmemory) {
    MaybeRegisterLowMemoryListener();
  }
}

void H5vccMemory::RemovedEventListener(
    const AtomicString& event_type,
    const RegisteredEventListener& registered_listener) {
  EventTarget::RemovedEventListener(event_type, registered_listener);
  if (event_type == event_type_names::kLowmemory) {
    MaybeUnregisterLowMemoryListener();
  }
}

void H5vccMemory::Trace(Visitor* visitor) const {
  ScriptWrappable::Trace(visitor);
  ExecutionContextLifecycleObserver::Trace(visitor);
  EventTarget::Trace(visitor);
  visitor->Trace(remote_h5vcc_memory_);
  visitor->Trace(low_memory_receiver_);
}

void H5vccMemory::MaybeRegisterLowMemoryListener() {
  DCHECK(HasEventListeners(event_type_names::kLowmemory));
  if (low_memory_receiver_.is_bound()) {
    return;
  }

  EnsureRemoteIsBound();

  auto task_runner =
      GetExecutionContext()->GetTaskRunner(TaskType::kMiscPlatformAPI);
  remote_h5vcc_memory_->AddLowMemoryListener(
      low_memory_receiver_.BindNewPipeAndPassRemote(task_runner));
  low_memory_receiver_.set_disconnect_handler(WTF::BindOnce(
      &H5vccMemory::OnListenerConnectionError, WrapWeakPersistent(this)));
}

void H5vccMemory::MaybeUnregisterLowMemoryListener() {
  DCHECK(low_memory_receiver_.is_bound());
  if (!HasEventListeners(event_type_names::kLowmemory)) {
    low_memory_receiver_.reset();
  }
}

}  // namespace blink
