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

#include "starboard/elf_loader/zstd_file_impl.h"

#include <sys/stat.h>

#include <string>
#include <vector>

#include "starboard/configuration_constants.h"
#include "starboard/system.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace elf_loader {
namespace {

// TODO: b/497012299 - Because LZ4FileImplTest and ZstdFileImplTest share
// many behaviors, their unit test cases and implementations are quite similar.
// We should refactor the tests - maybe leveraging GoogleTest's Typed Tests -
// so that common test cases need only be written once.

// The expected result of decompressing the test file is 64 bytes of 'a'.
constexpr char kUncompressedData[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

// Returns the absolute path to the requested test data file.
//
// In `is_starboard` build configurations, the build copies the test data to a
// starboard subdirectory under the executable directory. In other build
// configurations, the test data is copied directly to the content directory.
// This helper checks both locations.
std::string GetTestFilePath(const std::string& filename) {
  std::vector<char> content_path(kSbFileMaxPath + 1);
  if (!SbSystemGetPath(kSbSystemPathContentDirectory, content_path.data(),
                       kSbFileMaxPath + 1)) {
    return "";
  }
  std::string base = content_path.data();
  std::string suffix = std::string("test") + kSbFileSepChar + "starboard" +
                       kSbFileSepChar + "elf_loader" + kSbFileSepChar +
                       "testdata" + kSbFileSepChar + filename;

  std::string path = base + kSbFileSepChar + suffix;
  struct stat info;
  if (stat(path.c_str(), &info) == 0) {
    return path;
  }

  // Fallback for modular Starboard toolchain builds.
  path = base + kSbFileSepChar + "starboard" + kSbFileSepChar + suffix;
  if (stat(path.c_str(), &info) == 0) {
    return path;
  }

  return "";
}

TEST(ZstdFileImplTest, OpenNonExistentFileFails) {
  ZstdFileImpl file;
  EXPECT_FALSE(file.Open("thisfiledoesnotexist"));
}

TEST(ZstdFileImplTest, ReadBeforeOpeningFails) {
  ZstdFileImpl file;
  char buf[10];
  EXPECT_FALSE(file.ReadFromOffset(/*offset=*/0, buf, /*size=*/10));
}

TEST(ZstdFileImplTest, ReadNegativeOffsetFails) {
  std::string file_path = GetTestFilePath("compressed.zst");
  ASSERT_FALSE(file_path.empty()) << "Test file compressed.zst not found.";

  ZstdFileImpl file;
  ASSERT_TRUE(file.Open(file_path.c_str()));

  char buf[10];
  EXPECT_FALSE(file.ReadFromOffset(/*offset=*/-1, buf, /*size=*/10));
}

TEST(ZstdFileImplTest, ReadOutOfBoundsFails) {
  std::string file_path = GetTestFilePath("compressed.zst");
  ASSERT_FALSE(file_path.empty()) << "Test file compressed.zst not found.";

  ZstdFileImpl file;
  ASSERT_TRUE(file.Open(file_path.c_str()));

  char buf[10];
  // Valid file is 65 bytes (64 'a's + newline). Reading 60 bytes at offset 10
  // goes out of bounds.
  EXPECT_FALSE(file.ReadFromOffset(/*offset=*/10, buf, /*size=*/60));
}

TEST(ZstdFileImplTest, FrameHeaderMissingContentSizeDecompressionFails) {
  std::string file_path = GetTestFilePath("compressed_no_content_size.zst");
  ASSERT_FALSE(file_path.empty())
      << "Test file compressed_no_content_size.zst not found.";

  ZstdFileImpl file;
  EXPECT_FALSE(file.Open(file_path.c_str()));
}

TEST(ZstdFileImplTest, MultiFrameFileDecompressionSucceeds) {
  std::string file_path = GetTestFilePath("compressed_multi_frame.zst");
  ASSERT_FALSE(file_path.empty())
      << "Test file compressed_multi_frame.zst not found.";

  ZstdFileImpl file;
  ASSERT_TRUE(file.Open(file_path.c_str()));

  char decompressed[SB_ARRAY_SIZE(kUncompressedData)]{0};
  EXPECT_TRUE(file.ReadFromOffset(0, decompressed,
                                  SB_ARRAY_SIZE(kUncompressedData) - 1));
  EXPECT_EQ(strcmp(decompressed, kUncompressedData), 0);
}

TEST(ZstdFileImplTest, ValidFileDecompressionSucceeds) {
  std::string file_path = GetTestFilePath("compressed.zst");
  ASSERT_FALSE(file_path.empty()) << "Test file compressed.zst not found.";

  ZstdFileImpl file;
  ASSERT_TRUE(file.Open(file_path.c_str()));

  char decompressed[SB_ARRAY_SIZE(kUncompressedData)]{0};
  EXPECT_TRUE(file.ReadFromOffset(0, decompressed,
                                  SB_ARRAY_SIZE(kUncompressedData) - 1));
  EXPECT_EQ(strcmp(decompressed, kUncompressedData), 0);
}

TEST(ZstdFileImplTest, OpenEmptyFileFails) {
  std::string file_path = GetTestFilePath("empty.zst");
  ASSERT_FALSE(file_path.empty()) << "Test file empty.zst not found.";

  ZstdFileImpl file;
  EXPECT_FALSE(file.Open(file_path.c_str()));
}

TEST(ZstdFileImplTest, OpenPartiallyCorruptedFileFails) {
  std::string file_path = GetTestFilePath("compressed_with_garbage.zst");
  ASSERT_FALSE(file_path.empty())
      << "Test file compressed_with_garbage.zst not found.";

  ZstdFileImpl file;
  EXPECT_FALSE(file.Open(file_path.c_str()));
}

}  // namespace
}  // namespace elf_loader
