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

#include "cobalt/browser/dial/dial_service.h"

#include <memory>
#include <utility>

#include "base/memory/scoped_refptr.h"
#include "base/message_loop/message_pump_type.h"
#include "base/no_destructor.h"
#include "base/sequence_checker.h"
#include "base/strings/stringprintf.h"
#include "base/task/bind_post_task.h"
#include "base/task/current_thread.h"
#include "base/task/sequenced_task_runner.h"
#include "base/thread_annotations.h"
#include "cobalt/browser/dial/dial_device_description.h"
#include "cobalt/browser/dial/dial_http_server.h"
#include "cobalt/browser/dial/dial_udp_server.h"
#include "net/base/ip_endpoint.h"
#include "net/server/http_server_response_info.h"

namespace in_app_dial {

class DialService::BlockingHelper final
    : public DialHttpServer::RequestHandler {
 public:
  BlockingHelper() = default;
  ~BlockingHelper() { DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_); }

  void Start() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (dial_http_server_) {
      LOG(WARNING) << "Start() called with a running HTTP server. Exiting.";
      return;
    }

    dial_http_server_ = std::make_unique<DialHttpServer>(
        device_description_, weak_ptr_factory_.GetWeakPtr());

    const net::IPEndPoint http_server_address =
        dial_http_server_->GetLocalAddress();
    if (http_server_address.ToString().empty()) {
      LOG(WARNING) << "Unable to get an IP address from the HTTP server. The "
                      "UDP server will not be started.";
      return;
    }
    dial_udp_server_ = std::make_unique<DialUdpServer>(device_description_,
                                                       http_server_address);
  }

  void Stop() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    dial_udp_server_.reset();
    dial_http_server_.reset();
  }

  void Restart() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    Stop();
    Start();
  }

  void RegisterHandlerCallback(DialHttpHandlerCallback callback) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (handler_callback_) {
      // DialService exists as a singleton, while handlers (i.e. DialServerImpl)
      // are per-Document. This works for Kabuki, which is an SPA with one
      // frame, but will have to be revisited if multiple frames with their own
      // DialServer objects are ever used.
      LOG(ERROR) << __func__
                 << " A handler callback is already registered and will be "
                    "cleared. This is probably a mistake in the calling code.";
    }
    handler_callback_ = std::move(callback);
  }

  void ResetHandlerCallback() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    handler_callback_.Reset();
  }

  // DialHttpServer::RequestHandler overrides.
  void HandleRequest(
      const std::string& method,
      const std::string& service_name,
      const std::string& path,
      const std::string& data,
      const std::string& host_with_port,
      base::OnceCallback<void(std::optional<net::HttpServerResponseInfo>)>
          callback) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    if (!handler_callback_) {
      // No handler has been registered.
      std::move(callback).Run(std::nullopt);
      return;
    }

    handler_callback_.Run(
        method, service_name, path, data, host_with_port,
        base::BindPostTask(base::SequencedTaskRunner::GetCurrentDefault(),
                           std::move(callback)));
  }

 private:
  DialService::DialHttpHandlerCallback handler_callback_
      GUARDED_BY_CONTEXT(sequence_checker_);

  const DialDeviceDescription device_description_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<DialHttpServer> dial_http_server_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<DialUdpServer> dial_udp_server_
      GUARDED_BY_CONTEXT(sequence_checker_);

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<BlockingHelper> weak_ptr_factory_{this};
};

DialService::DialService() : io_thread_("dial_io_thread") {
  CHECK(io_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, /*size=*/0)));
  blocking_helper_ =
      base::SequenceBound<BlockingHelper>(io_thread_.task_runner());
}

DialService::~DialService() = default;

// static
DialService* DialService::GetInstance() {
  static base::NoDestructor<DialService> instance;
  return instance.get();
}

void DialService::Start() {
  blocking_helper_.AsyncCall(&BlockingHelper::Start);
}

void DialService::Stop() {
  blocking_helper_.AsyncCall(&BlockingHelper::Stop);
}

void DialService::RegisterHandlerCallback(DialHttpHandlerCallback callback) {
  blocking_helper_.AsyncCall(&BlockingHelper::RegisterHandlerCallback)
      .WithArgs(std::move(callback));
}

void DialService::ResetHandlerCallback() {
  blocking_helper_.AsyncCall(&BlockingHelper::ResetHandlerCallback);
}

}  // namespace in_app_dial
