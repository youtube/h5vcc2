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

#include "third_party/blink/renderer/modules/cobalt/dial/dial_server.h"

#include "base/notreached.h"
#include "cobalt/browser/dial/public/mojom/in_app_dial.mojom-blink.h"
#include "services/network/public/mojom/http_raw_headers.mojom-blink.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_dial_http_request_callback.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/modules/cobalt/dial/dial_http_request.h"
#include "third_party/blink/renderer/modules/cobalt/dial/dial_http_response.h"
#include "third_party/blink/renderer/modules/cobalt/dial/dial_server_manager.h"
#include "third_party/blink/renderer/platform/bindings/script_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"

namespace blink {

namespace {

AtomicString DialRequestMethodToString(
    in_app_dial::mojom::blink::DialRequestMethod method) {
  switch (method) {
    case in_app_dial::mojom::blink::DialRequestMethod::kDelete:
      return AtomicString("DELETE");
    case in_app_dial::mojom::blink::DialRequestMethod::kGet:
      return AtomicString("GET");
    case in_app_dial::mojom::blink::DialRequestMethod::kPost:
      return AtomicString("POST");
  }
  NOTREACHED();
}

}  // namespace

// static
DialServer* DialServer::Create(ExecutionContext* execution_context,
                               const String& service_name) {
  return MakeGarbageCollected<DialServer>(execution_context, service_name);
}

DialServer::DialServer(ExecutionContext* execution_context,
                       const String& service_name)
    : service_name_(service_name) {
  DialServerManager::From(execution_context)->RegisterDialServer(this);
}

DialServer::~DialServer() = default;

in_app_dial::mojom::blink::DialResponsePtr DialServer::HandleRequest(
    in_app_dial::mojom::blink::DialRequestMethod method,
    const String& path,
    const String& data,
    const String& host_with_port) {
  const HeapHashMap<String, Member<V8DialHttpRequestCallback>>* callback_map;
  switch (method) {
    case in_app_dial::mojom::blink::DialRequestMethod::kDelete:
      callback_map = &http_delete_callbacks_;
      break;
    case in_app_dial::mojom::blink::DialRequestMethod::kGet:
      callback_map = &http_get_callbacks_;
      break;
    case in_app_dial::mojom::blink::DialRequestMethod::kPost:
      callback_map = &http_post_callbacks_;
      break;
  }

  auto it = callback_map->find(path);
  if (it == callback_map->end()) {
    return nullptr;
  }
  V8DialHttpRequestCallback* callback = it->value;

  // Prepare arguments for the callback DialHttpRequestCallback invocation.
  const AtomicString& method_as_string = DialRequestMethodToString(method);
  auto* http_request = MakeGarbageCollected<DialHttpRequest>(
      path, method_as_string, data, host_with_port);
  auto* http_response =
      MakeGarbageCollected<DialHttpResponse>(path, method_as_string);

  // Invoke the user-defined callback. If the V8 value is not set (meaning an
  // internal error has occurred) or the return value is false, return an empty
  // response that will be treated like a 404 error (copying C25's behavior).
  auto result = callback->Invoke(nullptr, http_request, http_response);
  if (result.IsNothing() || !result.FromJust()) {
    return nullptr;
  }

  // Translate the potentially user-manipulated `http_response` into a Mojo
  // DialResponse.
  // This could be done automatically via Mojo typemapping, but since this is
  // only done here it is not worth the extra effort.
  auto dial_response = in_app_dial::mojom::blink::DialResponse::New();
  dial_response->status_code = http_response->responseCode();
  for (const auto& header : http_response->headers()) {
    dial_response->headers.push_back(
        network::mojom::blink::HttpRawHeaderPair::New(header.first,
                                                      header.second));
  }
  dial_response->mime_type = http_response->mimeType();
  dial_response->body = http_response->body();

  return dial_response;
}

String DialServer::serviceName() const {
  return service_name_;
}

bool DialServer::onDelete(const String& path,
                          V8DialHttpRequestCallback* listener) {
  auto result = http_delete_callbacks_.insert(path, listener);
  return result.is_new_entry;
}

bool DialServer::onGet(const String& path,
                       V8DialHttpRequestCallback* listener) {
  auto result = http_get_callbacks_.insert(path, listener);
  return result.is_new_entry;
}

bool DialServer::onPost(const String& path,
                        V8DialHttpRequestCallback* listener) {
  auto result = http_post_callbacks_.insert(path, listener);
  return result.is_new_entry;
}

void DialServer::Trace(Visitor* visitor) const {
  visitor->Trace(http_delete_callbacks_);
  visitor->Trace(http_get_callbacks_);
  visitor->Trace(http_post_callbacks_);
  ScriptWrappable::Trace(visitor);
}

}  // namespace blink
