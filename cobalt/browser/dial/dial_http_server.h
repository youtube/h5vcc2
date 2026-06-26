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

#ifndef COBALT_BROWSER_DIAL_DIAL_HTTP_SERVER_H_
#define COBALT_BROWSER_DIAL_DIAL_HTTP_SERVER_H_

#include <memory>
#include <optional>

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/thread_annotations.h"
#include "net/base/ip_endpoint.h"
#include "net/server/http_server.h"

namespace net {

class HttpServerRequestInfo;
class HttpServerResponseInfo;

}  // namespace net

namespace in_app_dial {

class DialDeviceDescription;

class DialHttpServer final : public net::HttpServer::Delegate {
 public:
  struct RequestHandler {
    virtual ~RequestHandler();
    virtual void HandleRequest(
        const std::string& method,
        const std::string& service_name,
        const std::string& path,
        const std::string& data,
        const std::string& host_with_port,
        base::OnceCallback<
            void(std::optional<net::HttpServerResponseInfo>)>) = 0;
  };

  DialHttpServer(const DialDeviceDescription&,
                 base::WeakPtr<RequestHandler> request_handler);
  ~DialHttpServer();

  void Start();
  void Stop();

  net::IPEndPoint GetLocalAddress() const;

  // net::HttpServer::Delegate overrides
  void OnConnect(int connection_id) override;
  void OnHttpRequest(int connection_id,
                     const net::HttpServerRequestInfo& info) override;
  void OnWebSocketRequest(int connection_id,
                          const net::HttpServerRequestInfo& info) override;
  void OnWebSocketMessage(int connection_id, std::string data) override;
  void OnClose(int connection_id) override;

 private:
  // This function runs on `blocking_task_runner_`.
  void OnResponseFromRequestHandler(
      int connection_id,
      std::optional<net::HttpServerResponseInfo> info);

  const std::string dd_xml_response_
      GUARDED_BY_CONTEXT(blocking_sequence_checker_);

  std::unique_ptr<net::HttpServer> http_server_
      GUARDED_BY_CONTEXT(blocking_sequence_checker_);

  base::WeakPtr<RequestHandler> request_handler_
      GUARDED_BY_CONTEXT(blocking_sequence_checker_);

  net::IPEndPoint server_address_
      GUARDED_BY_CONTEXT(blocking_sequence_checker_);

  SEQUENCE_CHECKER(blocking_sequence_checker_);

  base::WeakPtrFactory<DialHttpServer> weak_ptr_factory_{this};
};

}  // namespace in_app_dial

#endif  // COBALT_BROWSER_DIAL_DIAL_HTTP_SERVER_H_
