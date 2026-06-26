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

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_HTTP_RESPONSE_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_HTTP_RESPONSE_H_

#include <utility>

#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/string_hash.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class MODULES_EXPORT DialHttpResponse final : public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  DialHttpResponse(const String& path, const AtomicString& method);
  ~DialHttpResponse() override;

  // Web-exposed interfaces
  String path() const;
  AtomicString method() const;
  unsigned short responseCode() const;
  void setResponseCode(unsigned short);
  String body() const;
  void setBody(const String&);
  String mimeType() const;
  void setMimeType(const String&);
  void addHeader(const String& header, const String& value);

  const Vector<std::pair<String, String>>& headers() const;

 private:
  const String path_;
  const AtomicString method_;
  unsigned short response_code_;
  String body_;
  String mime_type_;
  Vector<std::pair<String, String>> headers_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_DIAL_DIAL_HTTP_RESPONSE_H_
