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

#ifndef COBALT_BROWSER_DIAL_DIAL_UDP_SERVER_H_
#define COBALT_BROWSER_DIAL_DIAL_UDP_SERVER_H_

#include <memory>
#include <string>

#include "base/gtest_prod_util.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/server/http_server_request_info.h"
#include "net/socket/udp_server_socket.h"

namespace in_app_dial {

class DialDeviceDescription;

class DialUdpServer final {
 public:
  DialUdpServer(const DialDeviceDescription&,
                const net::IPEndPoint& http_server_address);
  ~DialUdpServer();

 private:
  FRIEND_TEST_ALL_PREFIXES(DialUdpServerTest, ParseSearchRequest);

  void Start();
  void Stop();

  void CreateAndBind();
  void AcceptAndProcessConnection();

  void DidRead(int result);
  void DidClose(net::UDPSocket* sock);
  void WriteComplete(int rv);

  // Parse a request to make sure it is a M-Search.
  static bool ParseSearchRequest(const std::string& request);

  // Is the valid SSDP request a valid M-Search request too ?
  static bool IsValidMSearchRequest(const net::HttpServerRequestInfo& info);

  std::unique_ptr<net::UDPServerSocket> socket_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Value to pass in LOCATION: header
  const net::IPEndPoint http_server_address_
      GUARDED_BY_CONTEXT(sequence_checker_);

  bool is_running_ GUARDED_BY_CONTEXT(sequence_checker_);

  scoped_refptr<net::IOBuffer> read_buffer_
      GUARDED_BY_CONTEXT(sequence_checker_);
  net::IPEndPoint client_address_ GUARDED_BY_CONTEXT(sequence_checker_);

  const std::string device_uuid_ GUARDED_BY_CONTEXT(sequence_checker_);

  const std::string search_response_ GUARDED_BY_CONTEXT(sequence_checker_);

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<DialUdpServer> weak_ptr_factory_{this};
};

}  // namespace in_app_dial

#endif  // COBALT_BROWSER_DIAL_DIAL_UDP_SERVER_H_
