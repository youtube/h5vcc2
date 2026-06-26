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

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_SERVER_MANAGER_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_SERVER_MANAGER_H_

#include "cobalt/browser/dial/public/mojom/in_app_dial.mojom-blink.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_receiver.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"
#include "third_party/blink/renderer/platform/supplementable.h"

namespace blink {

class DialServer;
class ExecutionContext;

// This class acts as a broker between the browser side (DialServerImpl) and
// multiple DialServer instances. It handles the Mojo connections to and from
// the browser side and routes DIAL HTTP requests to an appropriate DialServer
// if one exists.
class MODULES_EXPORT DialServerManager final
    : public GarbageCollected<DialServerManager>,
      public Supplement<ExecutionContext>,
      public in_app_dial::mojom::blink::DialRequestHandler {
 public:
  static const char kSupplementName[];

  static DialServerManager* From(ExecutionContext*);

  explicit DialServerManager(ExecutionContext*);
  ~DialServerManager() override;

  DialServerManager(const DialServerManager&) = delete;
  DialServerManager& operator=(const DialServerManager&) = delete;

  // Register a DialServer by service name as handling requests to
  // /app/<service_name>/. Per C25 behavior, if a server with the same service
  // name is already registered, it will be overwritten.
  void RegisterDialServer(DialServer*);

  // in_app_dial::mojom::blink::DialRequestHandler implementation.
  void HandleRequest(in_app_dial::mojom::blink::DialRequestMethod method,
                     const String& service_name,
                     const String& path,
                     const String& data,
                     const String& host_with_port,
                     HandleRequestCallback callback) override;

  // GarbageCollected implementation.
  void Trace(Visitor*) const override;

 private:
  // Called when either `dial_server_` or `receiver_` is disconnected.
  void OnConnectionError();

  HeapMojoRemote<in_app_dial::mojom::blink::DialServer> dial_server_;
  HeapMojoReceiver<in_app_dial::mojom::blink::DialRequestHandler,
                   DialServerManager>
      receiver_;

  HeapHashMap<String, WeakMember<DialServer>> handler_registry_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_SERVER_MANAGER_H_
