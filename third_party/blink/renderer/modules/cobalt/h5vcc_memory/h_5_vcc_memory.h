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

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_H5VCC_MEMORY_H_5_VCC_MEMORY_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_H5VCC_MEMORY_H_5_VCC_MEMORY_H_

#include "cobalt/browser/h5vcc_memory/public/mojom/h5vcc_memory.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/events/event_target.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h"
#include "third_party/blink/renderer/modules/event_target_modules_names.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_receiver.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"

namespace blink {

class ExecutionContext;
class LocalDOMWindow;

// H5vccMemory implements the web-exposed window.h5vcc.memory API.
// It allows JavaScript clients to register for low memory notifications
// forwarded from the browser process.
class MODULES_EXPORT H5vccMemory final
    : public EventTarget,
      public ExecutionContextLifecycleObserver,
      public h5vcc_memory::mojom::blink::LowMemoryListener {
  DEFINE_WRAPPERTYPEINFO();

 public:
  explicit H5vccMemory(LocalDOMWindow&);

  void ContextDestroyed() override;

  // Web-exposed interface:
  DEFINE_ATTRIBUTE_EVENT_LISTENER(lowmemory, kLowmemory)

  // EventTarget interface:
  void AddedEventListener(const AtomicString& event_type,
                          RegisteredEventListener&) override;
  void RemovedEventListener(const AtomicString& event_type,
                            const RegisteredEventListener&) override;

  // Mojom interface:
  void OnLowMemory() override;

  // EventTarget impl.
  ExecutionContext* GetExecutionContext() const override {
    return ExecutionContextLifecycleObserver::GetExecutionContext();
  }
  const AtomicString& InterfaceName() const override {
    return event_target_names::kH5VccMemory;
  }

  void Trace(Visitor*) const override;

 private:
  void EnsureRemoteIsBound();
  void MaybeRegisterLowMemoryListener();
  void MaybeUnregisterLowMemoryListener();
  void OnConnectionError();
  void OnListenerConnectionError();

  HeapMojoRemote<h5vcc_memory::mojom::blink::H5vccMemory> remote_h5vcc_memory_;
  HeapMojoReceiver<h5vcc_memory::mojom::blink::LowMemoryListener, H5vccMemory>
      low_memory_receiver_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_H5VCC_MEMORY_H_5_VCC_MEMORY_H_
