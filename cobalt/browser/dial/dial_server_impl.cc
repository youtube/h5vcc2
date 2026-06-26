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

#include "cobalt/browser/dial/dial_server_impl.h"

#include <optional>

#include "base/sequence_checker.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "cobalt/browser/dial/dial_service.h"
#include "content/public/browser/render_frame_host.h"
#include "net/http/http_status_code.h"
#include "net/server/http_server_response_info.h"

namespace in_app_dial {

// static
void DialServerImpl::Create(content::RenderFrameHost* render_frame_host,
                            mojo::PendingReceiver<mojom::DialServer> receiver) {
  new DialServerImpl(*render_frame_host, std::move(receiver));
}

DialServerImpl::DialServerImpl(
    content::RenderFrameHost& render_frame_host,
    mojo::PendingReceiver<mojom::DialServer> receiver)
    : content::DocumentService<mojom::DialServer>(render_frame_host,
                                                  std::move(receiver)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DialService::GetInstance()->RegisterHandlerCallback(
      base::BindPostTask(base::SequencedTaskRunner::GetCurrentDefault(),
                         base::BindRepeating(&DialServerImpl::HandleRequest,
                                             weak_ptr_factory_.GetWeakPtr())));
}

DialServerImpl::~DialServerImpl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DialService::GetInstance()->ResetHandlerCallback();
}

void DialServerImpl::RegisterHandler(
    mojo::PendingRemote<mojom::DialRequestHandler> request_handler) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  request_handler_.Bind(std::move(request_handler));
  request_handler_.reset_on_disconnect();
}

void DialServerImpl::HandleRequest(
    const std::string& method,
    const std::string& service_name,
    const std::string& path,
    const std::string& data,
    const std::string& host_with_port,
    base::OnceCallback<void(std::optional<net::HttpServerResponseInfo>)>
        callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DVLOG(1) << __func__ << " method=" << method
           << " service_name=" << service_name << " path=" << path
           << " data=" << data << " host_with_port=" << host_with_port;

  if (!request_handler_.is_bound()) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  std::optional<mojom::DialRequestMethod> dial_request_method;
  if (method == "DELETE") {
    dial_request_method = mojom::DialRequestMethod::kDelete;
  } else if (method == "GET") {
    dial_request_method = mojom::DialRequestMethod::kGet;
  } else if (method == "POST") {
    dial_request_method = mojom::DialRequestMethod::kPost;
  }

  if (!dial_request_method) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  request_handler_->HandleRequest(
      *dial_request_method, service_name, path, data, host_with_port,
      base::BindOnce(&DialServerImpl::OnRequestHandlerResponse,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DialServerImpl::OnRequestHandlerResponse(
    base::OnceCallback<void(std::optional<net::HttpServerResponseInfo>)>
        http_request_handler_callback,
    mojom::DialResponsePtr dial_response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!dial_response) {
    DVLOG(1) << __func__ << " received null response";
    std::move(http_request_handler_callback).Run(std::nullopt);
    return;
  }

  auto status_code = net::TryToGetHttpStatusCode(dial_response->status_code);
  if (!status_code) {
    DVLOG(1) << __func__ << " received invalid status code "
             << dial_response->status_code;
    std::move(http_request_handler_callback).Run(std::nullopt);
    return;
  }

  net::HttpServerResponseInfo response_info(*status_code);
  response_info.SetBody(dial_response->body, dial_response->mime_type);
  for (const auto& header : dial_response->headers) {
    response_info.AddHeader(header->key, header->value);
  }
  std::move(http_request_handler_callback).Run(response_info);
}

}  // namespace in_app_dial
