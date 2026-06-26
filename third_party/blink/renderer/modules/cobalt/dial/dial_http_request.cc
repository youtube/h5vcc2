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

#include "third_party/blink/renderer/modules/cobalt/dial/dial_http_request.h"

namespace blink {

DialHttpRequest::DialHttpRequest(const String& path,
                                 const AtomicString& method,
                                 const String& body,
                                 const String& host)
    : path_(path), method_(method), body_(body), host_(host) {}

DialHttpRequest::~DialHttpRequest() = default;

String DialHttpRequest::path() const {
  return path_;
}

AtomicString DialHttpRequest::method() const {
  return method_;
}

String DialHttpRequest::body() const {
  return body_;
}

String DialHttpRequest::host() const {
  return host_;
}

}  // namespace blink
