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

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_SERVER_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_SERVER_H_

#include "cobalt/browser/dial/public/mojom/in_app_dial.mojom-blink-forward.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/text/string_hash.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class ExecutionContext;
class V8DialHttpRequestCallback;

class MODULES_EXPORT DialServer final : public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static DialServer* Create(ExecutionContext*, const String& service_name);

  DialServer(ExecutionContext*, const String& service_name);
  ~DialServer() override;

  in_app_dial::mojom::blink::DialResponsePtr HandleRequest(
      in_app_dial::mojom::blink::DialRequestMethod method,
      const String& path,
      const String& data,
      const String& host_with_port);

  // Web-exposed interfaces
  String serviceName() const;
  bool onDelete(const String& path, V8DialHttpRequestCallback* listener);
  bool onGet(const String& path, V8DialHttpRequestCallback* listener);
  bool onPost(const String& path, V8DialHttpRequestCallback* listener);

  // GarbageCollected implementation.
  void Trace(Visitor*) const override;

 private:
  const String service_name_;

  HeapHashMap<String, Member<V8DialHttpRequestCallback>> http_delete_callbacks_;
  HeapHashMap<String, Member<V8DialHttpRequestCallback>> http_get_callbacks_;
  HeapHashMap<String, Member<V8DialHttpRequestCallback>> http_post_callbacks_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_SERVER_H_
