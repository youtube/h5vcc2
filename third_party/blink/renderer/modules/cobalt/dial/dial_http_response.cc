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

#include "third_party/blink/renderer/modules/cobalt/dial/dial_http_response.h"

namespace blink {

// Note: setting the default response code to 500 comes from C25.
DialHttpResponse::DialHttpResponse(const String& path,
                                   const AtomicString& method)
    : path_(path), method_(method), response_code_(500) {}

DialHttpResponse::~DialHttpResponse() = default;

String DialHttpResponse::path() const {
  return path_;
}

AtomicString DialHttpResponse::method() const {
  return method_;
}

unsigned short DialHttpResponse::responseCode() const {
  return response_code_;
}

void DialHttpResponse::setResponseCode(unsigned short response_code) {
  response_code_ = response_code;
}

String DialHttpResponse::body() const {
  return body_;
}

void DialHttpResponse::setBody(const String& body) {
  body_ = body;
}

String DialHttpResponse::mimeType() const {
  return mime_type_;
}

void DialHttpResponse::setMimeType(const String& mime_type) {
  mime_type_ = mime_type;
}

void DialHttpResponse::addHeader(const String& header, const String& value) {
  headers_.emplace_back(header, value);
}

const Vector<std::pair<String, String>>& DialHttpResponse::headers() const {
  return headers_;
}

}  // namespace blink
