// Copyright 2024 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/configuration.h"
#include "starboard/nplb/posix_compliance/posix_socket_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace nplb {
namespace {

class PosixSocketSetOptionsTest : public ::testing::TestWithParam<int> {
 public:
  int GetSocketDomain() { return GetParam(); }
};

TEST_P(PosixSocketSetOptionsTest, TryThemAllTCP) {
  int socket_fd = socket(GetSocketDomain(), SOCK_STREAM, IPPROTO_TCP);

  int true_val = 1;
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &true_val,
                       sizeof(true_val)),
            0);

  int value = 16 * 1024;
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)),
            0);
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)),
            0);

  // Test TCP keepalive option.
  int period_seconds = static_cast<int>(30'000'000 / 1'000'000);
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &true_val,
                       sizeof(true_val)),
            0);
#if defined(TCP_KEEPALIVE)
  // In tvOS, TCP_KEEPIDLE and SOL_TCP are not available.
  // For reference:
  // https://stackoverflow.com/questions/15860127/how-to-configure-tcp-keepalive-under-mac-os-x
  EXPECT_EQ(setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPALIVE, &period_seconds,
                       sizeof(period_seconds)),
            0);
#else
  EXPECT_EQ(setsockopt(socket_fd, SOL_TCP, TCP_KEEPIDLE, &period_seconds,
                       sizeof(period_seconds)),
            0);
  EXPECT_EQ(setsockopt(socket_fd, SOL_TCP, TCP_KEEPINTVL, &period_seconds,
                       sizeof(period_seconds)),
            0);
#endif

  EXPECT_EQ(setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &true_val,
                       sizeof(true_val)),
            0);

  close(socket_fd);
}

TEST_P(PosixSocketSetOptionsTest, TryThemAllUDP) {
  int socket_fd = socket(GetSocketDomain(), SOCK_DGRAM, IPPROTO_UDP);

  int true_val = 1;
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &true_val,
                       sizeof(true_val)),
            0);
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &true_val,
                       sizeof(true_val)),
            0);

  int value = 16 * 1024;
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)),
            0);
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)),
            0);

  close(socket_fd);
}

TEST_P(PosixSocketSetOptionsTest, RainyDayInvalidSocket) {
  int true_val = 1;
  EXPECT_EQ(
      setsockopt(-1, SOL_SOCKET, SO_REUSEADDR, &true_val, sizeof(true_val)),
      -1);

  int value = 16 * 1024;
  EXPECT_EQ(setsockopt(-1, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value)), -1);
  EXPECT_EQ(setsockopt(-1, SOL_SOCKET, SO_SNDBUF, &value, sizeof(value)), -1);

  EXPECT_EQ(
      setsockopt(-1, SOL_SOCKET, SO_KEEPALIVE, &true_val, sizeof(true_val)),
      -1);

  EXPECT_EQ(
      setsockopt(-1, IPPROTO_TCP, TCP_NODELAY, &true_val, sizeof(true_val)),
      -1);
}

INSTANTIATE_TEST_SUITE_P(PosixSocketAddressTypes,
                         PosixSocketSetOptionsTest,
                         ::testing::Values(AF_INET, AF_INET6));

}  // namespace
}  // namespace nplb
