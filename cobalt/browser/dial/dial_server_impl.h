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

#ifndef COBALT_BROWSER_DIAL_DIAL_SERVER_IMPL_H_
#define COBALT_BROWSER_DIAL_DIAL_SERVER_IMPL_H_

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "cobalt/browser/dial/dial_http_server.h"
#include "cobalt/browser/dial/public/mojom/in_app_dial.mojom.h"
#include "content/public/browser/document_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace in_app_dial {

class DialServerImpl final : public content::DocumentService<mojom::DialServer>,
                             public DialHttpServer::RequestHandler {
 public:
  static void Create(content::RenderFrameHost* render_frame_host,
                     mojo::PendingReceiver<mojom::DialServer> receiver);

  DialServerImpl(content::RenderFrameHost& render_frame_host,
                 mojo::PendingReceiver<mojom::DialServer> receiver);
  ~DialServerImpl() override;

  DialServerImpl(const DialServerImpl&) = delete;
  DialServerImpl& operator=(const DialServerImpl&) = delete;

  // in_app_dial::mojom::DialServer overrides
  void RegisterHandler(mojo::PendingRemote<mojom::DialRequestHandler>) override;

  // DialHttpServer::RequestHandler overrides
  void HandleRequest(
      const std::string& method,
      const std::string& service_name,
      const std::string& path,
      const std::string& data,
      const std::string& host_with_port,
      base::OnceCallback<void(std::optional<net::HttpServerResponseInfo>)>
          callback) override;

 private:
  void OnRequestHandlerResponse(
      base::OnceCallback<void(std::optional<net::HttpServerResponseInfo>)>
          http_request_handler_callback,
      mojom::DialResponsePtr);

  SEQUENCE_CHECKER(sequence_checker_;)

  mojo::Remote<mojom::DialRequestHandler> request_handler_
      GUARDED_BY_CONTEXT(sequence_checker_);

  base::WeakPtrFactory<DialServerImpl> weak_ptr_factory_{this};
};

}  // namespace in_app_dial

#endif  // COBALT_BROWSER_DIAL_DIAL_SERVER_IMPL_H_
