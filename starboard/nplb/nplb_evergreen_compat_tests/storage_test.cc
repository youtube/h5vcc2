// Copyright 2020 The Cobalt Authors. All Rights Reserved.
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

#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "starboard/common/file.h"
#include "starboard/common/log.h"
#include "starboard/configuration.h"
#include "starboard/configuration_constants.h"
#include "starboard/nplb/nplb_evergreen_compat_tests/checks.h"
#include "starboard/system.h"
#include "testing/gtest/include/gtest/gtest.h"

#if !BUILDFLAG(IS_STARBOARD)
#error These tests apply only to Starboard platforms.
#endif

namespace nplb {

namespace {

const char kFileName[] = "test_file.data";
const size_t kBufSize = 64 * 1024 * 1024;  // 64 MB

void WriteBuffer(const char* file_path,
                 const char* buffer,
                 size_t buffer_size) {
  starboard::ScopedFile file(file_path, O_CREAT | O_WRONLY);
  ASSERT_TRUE(file.IsValid()) << "Failed to open file for writing";
  int bytes_written = file.WriteAll(buffer, buffer_size);
  ASSERT_EQ(kBufSize, static_cast<size_t>(bytes_written));
}

void ReadBuffer(const char* file_path, char* buffer, size_t buffer_size) {
  starboard::ScopedFile file(file_path, 0);
  ASSERT_TRUE(file.IsValid()) << "Failed to open file for reading";
  int count = file.ReadAll(buffer, buffer_size);
  ASSERT_EQ(kBufSize, static_cast<size_t>(count));
}

TEST(StorageTest, VerifyStorageDirectory) {
  std::vector<char> storage_dir(kSbFileMaxPath);
  ASSERT_TRUE(SbSystemGetPath(kSbSystemPathStorageDirectory, storage_dir.data(),
                              kSbFileMaxPath));

  std::string file_path = storage_dir.data();
  file_path += kSbFileSepString;
  file_path += kFileName;
  SB_LOG(INFO) << "file: " << file_path;

  std::vector<char> buf(kBufSize);
  memset(buf.data(), 'A', kBufSize);

  WriteBuffer(file_path.c_str(), buf.data(), kBufSize);

  memset(buf.data(), 0, kBufSize);

  ReadBuffer(file_path.c_str(), buf.data(), kBufSize);

  for (size_t i = 0; i < kBufSize; i++) {
    ASSERT_EQ('A', buf[i]);
  }

  ASSERT_TRUE(!unlink(file_path.data()));
  struct stat info;
  ASSERT_FALSE(stat(file_path.data(), &info) == 0);
}

}  // namespace
}  // namespace nplb
