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

#include "third_party/blink/renderer/modules/cobalt/dial/dial_server_manager.h"

#include "base/functional/bind.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/modules/cobalt/dial/dial_server.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

namespace blink {

// static
const char DialServerManager::kSupplementName[] = "DialServerManager";

// static
DialServerManager* DialServerManager::From(ExecutionContext* context) {
  DialServerManager* manager =
      Supplement<ExecutionContext>::From<DialServerManager>(context);
  if (!manager) {
    manager = MakeGarbageCollected<DialServerManager>(context);
    Supplement<ExecutionContext>::ProvideTo(*context, manager);
  }
  return manager;
}

DialServerManager::DialServerManager(ExecutionContext* context)
    : Supplement<ExecutionContext>(*context),
      dial_server_(context),
      receiver_(this, context) {
  auto task_runner = context->GetTaskRunner(TaskType::kMiscPlatformAPI);

  context->GetBrowserInterfaceBroker().GetInterface(
      dial_server_.BindNewPipeAndPassReceiver(task_runner));
  dial_server_.set_disconnect_handler(WTF::BindOnce(
      &DialServerManager::OnConnectionError, WrapWeakPersistent(this)));
  dial_server_->RegisterHandler(
      receiver_.BindNewPipeAndPassRemote(task_runner));
  receiver_.set_disconnect_handler(WTF::BindOnce(
      &DialServerManager::OnConnectionError, WrapWeakPersistent(this)));
}

DialServerManager::~DialServerManager() {}

void DialServerManager::RegisterDialServer(DialServer* dial_server) {
  const String& service_name = dial_server->serviceName();
  handler_registry_.insert(service_name, dial_server);
}

void DialServerManager::HandleRequest(
    in_app_dial::mojom::blink::DialRequestMethod method,
    const String& service_name,
    const String& path,
    const String& data,
    const String& host_with_port,
    HandleRequestCallback callback) {
  in_app_dial::mojom::blink::DialResponsePtr response = nullptr;

  auto it = handler_registry_.find(service_name);
  if (it != handler_registry_.end()) {
    DialServer* dial_server = it->value.Get();
    if (dial_server) {
      response = dial_server->HandleRequest(method, path, data, host_with_port);
    }
  }

  std::move(callback).Run(std::move(response));
}

void DialServerManager::Trace(Visitor* visitor) const {
  visitor->Trace(dial_server_);
  visitor->Trace(receiver_);
  visitor->Trace(handler_registry_);
  Supplement<ExecutionContext>::Trace(visitor);
}

void DialServerManager::OnConnectionError() {
  dial_server_.reset();
  receiver_.reset();
}

}  // namespace blink
