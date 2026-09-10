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

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_H5VCC_SYSTEM_H_5_VCC_SYSTEM_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_H5VCC_SYSTEM_H_5_VCC_SYSTEM_H_

#include <optional>

#include "cobalt/browser/h5vcc_system/public/mojom/h5vcc_system.mojom-blink.h"
#include "third_party/blink/renderer/bindings/core/v8/idl_types.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_observer.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_receiver.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_wrapper_mode.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class ExecutionContext;
class LocalDOMWindow;
class ScriptState;

class MODULES_EXPORT H5vccSystem final
    : public ScriptWrappable,
      public ExecutionContextLifecycleObserver {
  DEFINE_WRAPPERTYPEINFO();

 public:
  explicit H5vccSystem(LocalDOMWindow&);

  void ContextDestroyed() override;

  // Web-exposed interface:
  ScriptPromise<IDLString> getAdvertisingId(ScriptState*, ExceptionState&);
  const String& advertisingId();
  ScriptPromise<IDLBoolean> getLimitAdTracking(ScriptState*, ExceptionState&);
  std::optional<bool> limitAdTracking();
  ScriptPromise<IDLString> getFriendlyName(ScriptState*, ExceptionState&);
  ScriptPromise<IDLDouble> getScreenDiagonal(ScriptState*, ExceptionState&);
  ScriptPromise<IDLString> getTrackingAuthorizationStatus(ScriptState*,
                                                          ExceptionState&);
  const String& trackingAuthorizationStatus();
  ScriptPromise<IDLUndefined> requestTrackingAuthorization(ScriptState*,
                                                           ExceptionState&);
  void exit();
  uint32_t userOnExitStrategy();
  void hideSplashScreen();
  ScriptPromise<IDLBoolean> wasLowMemoryKilled(ScriptState*);

  void Trace(Visitor*) const override;

 private:
  void OnGetAdvertisingId(ScriptPromiseResolver<IDLString>*, const String&);
  void OnGetLimitAdTracking(ScriptPromiseResolver<IDLBoolean>*, bool);
  void OnGetFriendlyName(ScriptPromiseResolver<IDLString>*, const String&);
  void OnGetScreenDiagonal(ScriptPromiseResolver<IDLDouble>*, double);
  void OnGetTrackingAuthorizationStatus(ScriptPromiseResolver<IDLString>*,
                                        const String&);
  void OnRequestTrackingAuthorization(ScriptPromiseResolver<IDLUndefined>*,
                                      bool);
  void OnGetWasLowMemoryKilled(ScriptPromiseResolver<IDLBoolean>*, bool);
  void EnsureReceiverIsBound();
  HeapMojoRemote<h5vcc_system::mojom::blink::H5vccSystem> remote_h5vcc_system_;

  String advertising_id_;
  String tracking_authorization_status_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_H5VCC_SYSTEM_H_5_VCC_SYSTEM_H_
