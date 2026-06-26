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

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_HTTP_REQUEST_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_HTTP_REQUEST_H_

#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class MODULES_EXPORT DialHttpRequest final : public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  DialHttpRequest(const String& path,
                  const AtomicString& method,
                  const String& body,
                  const String& host);
  ~DialHttpRequest() override;

  // Web-exposed interfaces
  String path() const;
  AtomicString method() const;
  String body() const;
  String host() const;

 private:
  const String path_;
  const AtomicString method_;
  const String body_;
  const String host_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_HTTP_REQUEST_H_
