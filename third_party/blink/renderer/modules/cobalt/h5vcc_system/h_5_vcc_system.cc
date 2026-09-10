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

#include "third_party/blink/renderer/modules/cobalt/h5vcc_system/h_5_vcc_system.h"

#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_user_on_exit_strategy.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/modules/event_interface_modules_names.h"

namespace blink {

H5vccSystem::H5vccSystem(LocalDOMWindow& window)
    : ExecutionContextLifecycleObserver(window.GetExecutionContext()),
      remote_h5vcc_system_(window.GetExecutionContext()) {}

void H5vccSystem::ContextDestroyed() {}

ScriptPromise<IDLString> H5vccSystem::getAdvertisingId(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLString>>(
      script_state, exception_state.GetContext());

  EnsureReceiverIsBound();

  remote_h5vcc_system_->GetAdvertisingId(
      BindOnce(&H5vccSystem::OnGetAdvertisingId, WrapPersistent(this),
               WrapPersistent(resolver)));

  return resolver->Promise();
}

void H5vccSystem::OnGetAdvertisingId(ScriptPromiseResolver<IDLString>* resolver,
                                     const String& result) {
  resolver->Resolve(result);
}

const String& H5vccSystem::advertisingId() {
  EnsureReceiverIsBound();
  remote_h5vcc_system_->GetAdvertisingIdSync(&advertising_id_);
  return advertising_id_;
}

ScriptPromise<IDLBoolean> H5vccSystem::getLimitAdTracking(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLBoolean>>(
      script_state, exception_state.GetContext());

  EnsureReceiverIsBound();

  remote_h5vcc_system_->GetLimitAdTracking(
      BindOnce(&H5vccSystem::OnGetLimitAdTracking, WrapPersistent(this),
               WrapPersistent(resolver)));

  return resolver->Promise();
}

void H5vccSystem::OnGetLimitAdTracking(
    ScriptPromiseResolver<IDLBoolean>* resolver,
    bool result) {
  resolver->Resolve(result);
}

std::optional<bool> H5vccSystem::limitAdTracking() {
  EnsureReceiverIsBound();
  bool limit_ad_tracking;
  remote_h5vcc_system_->GetLimitAdTrackingSync(&limit_ad_tracking);
  return limit_ad_tracking;
}

ScriptPromise<IDLString> H5vccSystem::getFriendlyName(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLString>>(
      script_state, exception_state.GetContext());

  EnsureReceiverIsBound();

  remote_h5vcc_system_->GetFriendlyName(
      BindOnce(&H5vccSystem::OnGetFriendlyName, WrapPersistent(this),
               WrapPersistent(resolver)));

  return resolver->Promise();
}

void H5vccSystem::OnGetFriendlyName(ScriptPromiseResolver<IDLString>* resolver,
                                    const String& result) {
  resolver->Resolve(result);
}

ScriptPromise<IDLDouble> H5vccSystem::getScreenDiagonal(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLDouble>>(
      script_state, exception_state.GetContext());

  EnsureReceiverIsBound();

  remote_h5vcc_system_->GetScreenDiagonal(
      BindOnce(&H5vccSystem::OnGetScreenDiagonal, WrapPersistent(this),
               WrapPersistent(resolver)));

  return resolver->Promise();
}

void H5vccSystem::OnGetScreenDiagonal(
    ScriptPromiseResolver<IDLDouble>* resolver,
    double result) {
  resolver->Resolve(result);
}

ScriptPromise<IDLString> H5vccSystem::getTrackingAuthorizationStatus(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLString>>(
      script_state, exception_state.GetContext());

  EnsureReceiverIsBound();

  remote_h5vcc_system_->GetTrackingAuthorizationStatus(
      BindOnce(&H5vccSystem::OnGetTrackingAuthorizationStatus,
               WrapPersistent(this), WrapPersistent(resolver)));

  return resolver->Promise();
}

void H5vccSystem::OnGetTrackingAuthorizationStatus(
    ScriptPromiseResolver<IDLString>* resolver,
    const String& result) {
  resolver->Resolve(result);
}

const String& H5vccSystem::trackingAuthorizationStatus() {
  EnsureReceiverIsBound();
  remote_h5vcc_system_->GetTrackingAuthorizationStatusSync(
      &tracking_authorization_status_);
  return tracking_authorization_status_;
}

ScriptPromise<IDLUndefined> H5vccSystem::requestTrackingAuthorization(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_state.GetContext());

  EnsureReceiverIsBound();

  remote_h5vcc_system_->RequestTrackingAuthorization(
      BindOnce(&H5vccSystem::OnRequestTrackingAuthorization,
               WrapPersistent(this), WrapPersistent(resolver)));

  return resolver->Promise();
}

void H5vccSystem::OnRequestTrackingAuthorization(
    ScriptPromiseResolver<IDLUndefined>* resolver,
    bool is_tracking_authorization_supported) {
  if (is_tracking_authorization_supported) {
    resolver->Resolve();
  } else {
    resolver->Reject();
  }
}

void H5vccSystem::exit() {
  EnsureReceiverIsBound();
  remote_h5vcc_system_->Exit();
}

// TODO(b/385357645): consider caching value. Will need to be updated when
//     cobalt is updated on evergreen devices.
uint32_t H5vccSystem::userOnExitStrategy() {
  h5vcc_system::mojom::blink::UserOnExitStrategy strategy;
  EnsureReceiverIsBound();
  remote_h5vcc_system_->GetUserOnExitStrategy(&strategy);
  switch (strategy) {
    case h5vcc_system::mojom::blink::UserOnExitStrategy::kClose:
      return static_cast<uint32_t>(V8UserOnExitStrategy::Enum::kClose);
    case h5vcc_system::mojom::blink::UserOnExitStrategy::kMinimize:
      return static_cast<uint32_t>(V8UserOnExitStrategy::Enum::kMinimize);
    case h5vcc_system::mojom::blink::UserOnExitStrategy::kNoExit:
      return static_cast<uint32_t>(V8UserOnExitStrategy::Enum::kNoExit);
  }
  NOTREACHED() << "Invalid userOnExitStrategy: " << strategy;
}

void H5vccSystem::hideSplashScreen() {
  EnsureReceiverIsBound();
  remote_h5vcc_system_->HideSplashScreen();
}

ScriptPromise<IDLBoolean> H5vccSystem::wasLowMemoryKilled(
    ScriptState* script_state) {
  auto* resolver =
      MakeGarbageCollected<ScriptPromiseResolver<IDLBoolean>>(script_state);

  EnsureReceiverIsBound();

  remote_h5vcc_system_->GetWasLowMemoryKilled(
      WTF::BindOnce(&H5vccSystem::OnGetWasLowMemoryKilled, WrapPersistent(this),
                    WrapPersistent(resolver)));

  return resolver->Promise();
}

void H5vccSystem::OnGetWasLowMemoryKilled(
    ScriptPromiseResolver<IDLBoolean>* resolver,
    bool result) {
  resolver->Resolve(result);
}

void H5vccSystem::EnsureReceiverIsBound() {
  DCHECK(GetExecutionContext());

  if (remote_h5vcc_system_.is_bound()) {
    return;
  }

  auto task_runner =
      GetExecutionContext()->GetTaskRunner(TaskType::kMiscPlatformAPI);
  GetExecutionContext()->GetBrowserInterfaceBroker().GetInterface(
      remote_h5vcc_system_.BindNewPipeAndPassReceiver(task_runner));
}

void H5vccSystem::Trace(Visitor* visitor) const {
  ScriptWrappable::Trace(visitor);
  ExecutionContextLifecycleObserver::Trace(visitor);
  visitor->Trace(remote_h5vcc_system_);
}

}  // namespace blink
