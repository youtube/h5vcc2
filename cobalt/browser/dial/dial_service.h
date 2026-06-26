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

#ifndef COBALT_BROWSER_DIAL_DIAL_SERVICE_H_
#define COBALT_BROWSER_DIAL_DIAL_SERVICE_H_

#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/threading/sequence_bound.h"
#include "base/threading/thread.h"
#include "cobalt/browser/dial/dial_http_server.h"

namespace in_app_dial {

class DialService final {
 public:
  // This matches DialHttpServer::RequestHandler::HandleRequest()'s arguments.
  using DialHttpHandlerCallback = base::RepeatingCallback<void(
      const std::string&,
      const std::string&,
      const std::string&,
      const std::string&,
      const std::string&,
      base::OnceCallback<void(std::optional<net::HttpServerResponseInfo>)>)>;

  static DialService* GetInstance();

  void Start();
  void Stop();

  void RegisterHandlerCallback(DialHttpHandlerCallback callback);
  void ResetHandlerCallback();

 private:
  friend class base::NoDestructor<DialService>;

  class BlockingHelper;

  DialService();
  ~DialService();

  base::Thread io_thread_;

  base::SequenceBound<BlockingHelper> blocking_helper_;
};

}  // namespace in_app_dial

#endif  // COBALT_BROWSER_DIAL_DIAL_SERVICE_H_
