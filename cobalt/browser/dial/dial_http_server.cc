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

#include "cobalt/browser/dial/dial_http_server.h"

#include <optional>

#include "base/location.h"
#include "base/sequence_checker.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "cobalt/browser/dial/dial_device_description.h"
#include "net/base/address_family.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/network_interfaces.h"
#include "net/http/http_response_info.h"
#include "net/http/http_status_code.h"
#include "net/log/net_log_source.h"
#include "net/server/http_server_request_info.h"
#include "net/server/http_server_response_info.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace in_app_dial {

namespace {

constexpr const char kXmlMimeType[] = "text/xml; charset=\"utf-8\"";

constexpr const char kAppsPrefix[] = "/apps/";

constexpr net::NetworkTrafficAnnotationTag kNetworkTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("dial_http_server", "dial_http_server");

// Retrieves the first IPv4 address in any of the network interfaces that is not
// a loopback address.
std::optional<net::IPAddress> GetLocalIpAddress() {
  net::NetworkInterfaceList network_list;
  if (!net::GetNetworkList(&network_list,
                           net::EXCLUDE_HOST_SCOPE_VIRTUAL_INTERFACES)) {
    return std::nullopt;
  }

  for (const auto& network : network_list) {
    if (!network.address.IsIPv4() || network.address.IsLoopback()) {
      continue;
    }
    return network.address;
  }
  return std::nullopt;
}

}  // namespace

DialHttpServer::RequestHandler::~RequestHandler() = default;

DialHttpServer::DialHttpServer(const DialDeviceDescription& device_description,
                               base::WeakPtr<RequestHandler> request_handler)
    : dd_xml_response_(device_description.AsXml()),
      request_handler_(std::move(request_handler)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(blocking_sequence_checker_);
  Start();
}

void DialHttpServer::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(blocking_sequence_checker_);
  auto socket = std::make_unique<net::TCPServerSocket>(/*net_log=*/nullptr,
                                                       net::NetLogSource());
  auto local_ip = GetLocalIpAddress();
  if (!local_ip) {
    LOG(WARNING) << "Unable to get a local interface address";
  } else {
    int rv = socket->ListenWithAddressAndPort(local_ip->ToString(), 0,
                                              /*backlog=*/10);
    if (rv != net::OK) {
      LOG(WARNING) << "Failed to listen with address and port: "
                   << local_ip->ToString();
      return;
    }

    http_server_ = std::make_unique<net::HttpServer>(std::move(socket), this);

    if (http_server_->GetLocalAddress(&server_address_) != net::OK) {
      LOG(WARNING)
          << "Failed to retrieve HTTP server and port. Stopping HTTP server.";
      http_server_.reset();
    }
    VLOG(1) << "HTTP server launched and listening on "
            << server_address_.ToString();
  }
}

void DialHttpServer::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(blocking_sequence_checker_);
  http_server_.reset();
}

DialHttpServer::~DialHttpServer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(blocking_sequence_checker_);
}

net::IPEndPoint DialHttpServer::GetLocalAddress() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(blocking_sequence_checker_);
  return server_address_;
}

void DialHttpServer::OnConnect(int connection_id) {}

void DialHttpServer::OnHttpRequest(int connection_id,
                                   const net::HttpServerRequestInfo& info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(blocking_sequence_checker_);

  if (info.method == "GET" &&
      base::EqualsCaseInsensitiveASCII(info.path, "/dd.xml")) {
    net::HttpServerResponseInfo response_info(net::HTTP_OK);
    response_info.SetBody(dd_xml_response_, kXmlMimeType);

    response_info.AddHeader(
        "Application-URL",
        base::StringPrintf("http://%s/apps/", server_address_.ToString()));
    http_server_->SendResponse(connection_id, response_info,
                               kNetworkTrafficAnnotation);
    return;
  } else if (base::StartsWith(info.path, kAppsPrefix)) {
    if (info.method == "GET" && info.path == kAppsPrefix) {
      const std::string new_location = base::StringPrintf(
          "http://%s/apps/YouTube", server_address_.ToString());
      http_server_->SendRaw(
          connection_id,
          base::StringPrintf("HTTP/1.1 %d %s\r\n"
                             "Location: %s\r\n"
                             "Content-Length: 0\r\n"
                             "\r\n",
                             net::HTTP_FOUND, "Found", new_location),
          kNetworkTrafficAnnotation);
    } else {
      if (!request_handler_) {
        http_server_->Send404(connection_id, kNetworkTrafficAnnotation);
      } else {
        // Given a path like "/apps/YouTube/foo", the code below sets
        // `service_name` to "YouTube" and `path_after_service` to "/foo".
        const auto path = base::RemovePrefix(info.path, kAppsPrefix);
        // We have already checked that info.path starts with kAppsPrefix above.
        CHECK(path.has_value());
        const auto split_path = base::SplitStringOnce(*path, '/');
        const std::string service_name(
            split_path.has_value() ? split_path->first : *path);
        // If `split_path` is not std::nullopt, the leading '/' will have been
        // stripped, so add it back (i.e. "/apps/YouTube/foo" sets it to "/foo"
        // instead of "foo"). When `split_path` is std::nullopt (i.e.
        // "/apps/YouTube"), set it to "/".
        const std::string path_after_service(
            split_path.has_value() ? base::StrCat({"/", split_path->second})
                                   : "/");
        DVLOG(1) << "Forwarding request to request handler. service name="
                 << service_name << " path=" << path_after_service;
        request_handler_->HandleRequest(
            info.method, service_name, path_after_service, info.data,
            server_address_.ToString(),
            base::BindOnce(&DialHttpServer::OnResponseFromRequestHandler,
                           weak_ptr_factory_.GetWeakPtr(), connection_id));
      }
    }
  } else {
    // For all other cases, send 404.
    http_server_->Send404(connection_id, kNetworkTrafficAnnotation);
  }
}

void DialHttpServer::OnResponseFromRequestHandler(
    int connection_id,
    std::optional<net::HttpServerResponseInfo> info) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(blocking_sequence_checker_);

  if (!info) {
    http_server_->Send404(connection_id, kNetworkTrafficAnnotation);
    return;
  }
  http_server_->SendResponse(connection_id, *info, kNetworkTrafficAnnotation);
}

void DialHttpServer::OnWebSocketRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info) {}

void DialHttpServer::OnWebSocketMessage(int connection_id, std::string data) {}

void DialHttpServer::OnClose(int connection_id) {}

}  // namespace in_app_dial
