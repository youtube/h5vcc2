// Copyright 2023 The Cobalt Authors. All Rights Reserved.
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

#include "testing/gtest/include/gtest/gtest.h"

namespace in_app_dial {

TEST(DialUdpServerTest, ParseSearchRequest) {
  const struct TestData {
    std::string received_data;
    bool expected_result;
  } tests[] = {
      {
          // Empty request
          "",
          false,
      },
      {
          // The smallest correct response
          "M-SEARCH * HTTP/1.1\r\n"
          "ST: urn:dial-multiscreen-org:service:dial:1\r\n\r\n",
          true,
      },
      {
          // Incorrect ST-Header
          "M-SEARCH * HTTP/1.1\r\nST:  xyzfoo1231  \r\n\r\n",
          false,
      },
      {
          // Missing \r\n
          "M-SEARCH * HTTP/1.1\r\nST: 345512\r\n",
          false,
      },
      {
          // Incorrect SSDP method
          "GET * HTTP/1.1\r\nST: 13\r\n\r\n",
          false,
      },
      {
          // Incorrect SSDP path
          "M-SEARCH /path HTTP/1.1\r\nST: 13\r\n\r\n",
          false,
      },
      {
          // Empty headers
          "M-SEARCH * HTTP/1.1\r\n\r\n",
          false,
      },
  };

  for (const auto& test : tests) {
    EXPECT_EQ(DialUdpServer::ParseSearchRequest(test.received_data),
              test.expected_result);
  }
}

}  // namespace in_app_dial
