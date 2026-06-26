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

#include "cobalt/browser/dial/dial_udp_server.h"

#include <stddef.h>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/hash/hash.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/current_thread.h"
#include "base/task/sequenced_task_runner.h"
#include "cobalt/browser/dial/dial_device_description.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log.h"
#include "net/log/net_log_source_type.h"
#include "net/server/http_server.h"
#include "net/socket/datagram_socket.h"
#include "net/socket/udp_server_socket.h"
#include "net/socket/udp_socket_posix.h"

namespace in_app_dial {

namespace {

// From C25:
// https://github.com/youtube/cobalt/blob/b8a6647a9aa54ffc55fe13f1e77a3d73dd30eea1/cobalt/network/dial/dial_service.cc#L30
constexpr const char kServerAgent[] = "Cobalt/2.0 UPnP/1.1";

// From the DIAL spec.
constexpr const char kDialStRequest[] =
    "urn:dial-multiscreen-org:service:dial:1";

// From C25:
// https://github.com/youtube/cobalt/blob/b8a6647a9aa54ffc55fe13f1e77a3d73dd30eea1/cobalt/network/dial/dial_udp_server.cc#L46
constexpr size_t kReadBufferSize = 50 * 1024;

// From the DIAL spec.
constexpr int kUdpServerPort = 1900;

// From the DIAL spec. Multicast address this server is expected to respond to.
constexpr net::IPAddress kMulticastIPv4Address =
    net::IPAddress(239, 255, 255, 250);

std::string ConstructSearchResponse(const std::string& server_address,
                                    const std::string& device_uuid) {
  const std::string dd_xml_location =
      base::StringPrintf("http://%s/dd.xml", server_address);
  return base::StrCat({
      "HTTP/1.1 200 OK\r\n",
      base::StringPrintf("LOCATION: %s\r\n", dd_xml_location),
      "CACHE-CONTROL: max-age=1800\r\n",
      "EXT:\r\n",
      "BOOTID.UPNP.ORG: 1\r\n",
      // CONFIGID represents the state of the device description. Can be any
      // non-negative integer from 0 to 2^24 - 1.
      // DIAL clients like the Cast extension will cache the response based on
      // this, so we ensure each location change has a different config id.
      // This way when the app restarts with a new IP or port, the client
      // updates its cache of DIAL devices accordingly.
      base::StringPrintf("CONFIGID.UPNP.ORG: %u\r\n",
                         base::Hash(dd_xml_location) >> 8),
      base::StringPrintf("SERVER: %s\r\n", kServerAgent),
      base::StringPrintf("ST: %s\r\n", kDialStRequest),
      // This is formatted according to the UPnP 1.1 spec (section 1.2.2,
      // table 1-3).
      base::StringPrintf("USN: uuid:%s::%s\r\n", device_uuid, kDialStRequest),
      "\r\n",
  });
}

}  // namespace

DialUdpServer::DialUdpServer(const DialDeviceDescription& device_description,
                             const net::IPEndPoint& http_server_address)
    : http_server_address_(http_server_address),
      is_running_(false),
      read_buffer_(new net::IOBufferWithSize(kReadBufferSize)),
      device_uuid_(device_description.formatted_uuid()),
      search_response_(ConstructSearchResponse(http_server_address.ToString(),
                                               device_uuid_)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(base::CurrentIOThread::IsSet());
  Start();
}

DialUdpServer::~DialUdpServer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Stop();
}

void DialUdpServer::CreateAndBind() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  socket_ = std::make_unique<net::UDPServerSocket>(/*net_log=*/nullptr,
                                                   net::NetLogSource());
  socket_->AllowAddressReuse();
  if (int rv = socket_->Listen(net::IPEndPoint(net::IPAddress::IPv4AllZeros(),
                                               kUdpServerPort)) != net::OK) {
    socket_.reset();
    LOG(WARNING) << "Failed to bind socket for DIAL UDP Server. Error: " << rv;
    return;
  }

  if (socket_->JoinGroup(kMulticastIPv4Address) != net::OK) {
    LOG(WARNING) << "JoinGroup failed on DIAL UDP socket.";
  }

  AcceptAndProcessConnection();
}

void DialUdpServer::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_running_ = true;
  CreateAndBind();
}

void DialUdpServer::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  socket_.reset();
  is_running_ = false;
}

void DialUdpServer::AcceptAndProcessConnection() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_running_ || !socket_.get()) {
    return;
  }
  int result = socket_->RecvFrom(
      read_buffer_.get(), read_buffer_->size(), &client_address_,
      base::BindOnce(&DialUdpServer::DidRead, weak_ptr_factory_.GetWeakPtr()));
  if (result == net::ERR_IO_PENDING) {
    return;
  }
  DidRead(result);
}

void DialUdpServer::DidClose(net::UDPSocket* server) {}

void DialUdpServer::DidRead(int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!socket_ || !read_buffer_.get() || !read_buffer_->data()) {
    LOG(INFO) << "Dial server socket read error: no socket or buffer";
  } else if (result < 0) {
    LOG(ERROR) << "DialUdpServer read failed, stopping server: "
               << net::ErrorToString(result);
    Stop();
    return;
  } else {
    // ParseSearchRequest can be triggered when read bytes is zero, this will
    // prompt a response that updates the device picker (see b/268088112).
    if (result == 0 ||
        ParseSearchRequest(std::string(read_buffer_->data(), result))) {
      LOG(INFO) << "Dial server socket parses search request with " << result
                << " bytes read";
      LOG(INFO) << "In-App DIAL Discovery response : " << search_response_;
      const auto buffer =
          base::MakeRefCounted<net::StringIOBuffer>(search_response_);
      int send_result =
          socket_->SendTo(buffer.get(), buffer->size(), client_address_,
                          base::BindOnce(&DialUdpServer::WriteComplete,
                                         weak_ptr_factory_.GetWeakPtr()));
      if (send_result == net::ERR_IO_PENDING) {
        // WriteComplete is responsible for posting the next callback to accept
        // connection.
        return;
      } else if (send_result < 0) {
        LOG(ERROR) << "UDPSocket SendTo error: "
                   << net::ErrorToString(send_result);
      }
    }
  }

  // Register a watcher on the task runner and wait for the next dial message.
  // If we call AcceptAndProcessConnection directly, the function could call
  // DidRead and quickly increase stack size or even loop infinitely if the
  // socket can always provide messages through RecvFrom.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DialUdpServer::AcceptAndProcessConnection,
                                weak_ptr_factory_.GetWeakPtr()));
}

void DialUdpServer::WriteComplete(int rv) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (rv < 0) {
    LOG(ERROR) << "UDPSocket completion callback error: " << rv;
  }
  AcceptAndProcessConnection();
}

// static
bool DialUdpServer::ParseSearchRequest(const std::string& request) {
  net::HttpServerRequestInfo info;
  if (!net::HttpServer::ParseHeaders(request, &info)) {
    DVLOG(1) << "Failed parsing SSDP headers: " << request;
    return false;
  }

  if (!IsValidMSearchRequest(info)) {
    return false;
  }

  std::string st_request = info.GetHeaderValue("st");
  base::IgnoreResult(
      base::TrimWhitespaceASCII(st_request, base::TRIM_ALL, &st_request));

  if (st_request != kDialStRequest) {
    DVLOG(1) << "Received incorrect ST headers: " << st_request;
    return false;
  }

  DVLOG(1) << "Dial User-Agent: " << info.GetHeaderValue("user-agent");

  return true;
}

// static
bool DialUdpServer::IsValidMSearchRequest(
    const net::HttpServerRequestInfo& info) {
  if (info.method != "M-SEARCH") {
    DVLOG(1) << "Invalid M-Search: SSDP method incorrect. Received method: "
             << info.method;
    return false;
  }

  if (info.path != "*") {
    DVLOG(1) << "Invalid M-Search: SSDP path incorrect.";
    return false;
  }

  if (!info.data.empty()) {
    DVLOG(1) << "Invalid M-Search: SSDP request contains a body.";
    return false;
  }

  return true;
}

}  // namespace in_app_dial
