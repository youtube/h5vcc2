// Copyright 2019 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/player/file_cache_reader.h"

#include <string.h>
#include <sys/stat.h>

#include <algorithm>

#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/configuration_constants.h"

namespace starboard {

namespace {

// Accepts test file name and returns the complete file path based on the path:
// "<content path>/test/starboard/shared/starboard/player/testdata".
std::string ResolveTestFileName(const char* filename) {
  SB_CHECK(filename);

  std::vector<char> content_path(kSbFileMaxPath + 1);
  SB_CHECK(SbSystemGetPath(kSbSystemPathContentDirectory, content_path.data(),
                           content_path.size()));

  const auto content_path_as_string = std::string(content_path.data());
  const auto path_inside_content =
      std::string({kSbFileSepChar}) + "test" + kSbFileSepChar + "starboard" +
      kSbFileSepChar + "shared" + kSbFileSepChar + "starboard" +
      kSbFileSepChar + "player" + kSbFileSepChar + "testdata";
  std::string path = content_path_as_string + path_inside_content;

  struct stat info;
  bool does_path_exist =
      stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
  if (!does_path_exist) {
    // If |path| doesn't exist it could be due to |content_path_as_string| not
    // including the starboard_toolchain's output folder, i.e. it's e.g.
    // out/linux.../content when it should be out/linux.../starboard/content,
    // or out/linux... when test assets are under out/linux.../starboard.
    // TODO(b/384819454): Use EvergreenConfig here to overwrite the path. For
    // the time being, try appending "starboard" or inserting "starboard".
    path = content_path_as_string + kSbFileSepChar + "starboard" +
           path_inside_content;
    does_path_exist = stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
  }
  if (!does_path_exist) {
    std::size_t last_separation_char_pos =
        content_path_as_string.find_last_of(kSbFileSepChar);
    path = content_path_as_string.substr(0, last_separation_char_pos) +
           kSbFileSepChar + "starboard" + kSbFileSepChar +
           content_path_as_string.substr(last_separation_char_pos + 1) +
           path_inside_content;
    does_path_exist = stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
  }
  if (!does_path_exist) {
    // Check fallback location where test_runner.py pushes files on Android
    // device storage.
    path = std::string("/sdcard/chromium_tests_root/content/data") +
           path_inside_content;
    does_path_exist = stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
  }
  SB_CHECK(does_path_exist) << "Cannot open directory: " << path;
  return path + kSbFileSepChar + filename;
}

}  // namespace

FileCacheReader::FileCacheReader(const char* filename, int file_cache_size)
    : absolute_path_(ResolveTestFileName(filename)),
      default_file_cache_size_(file_cache_size) {}

int FileCacheReader::Read(void* out_buffer, int bytes_to_read) {
  EnsureFileOpened();

  int total_bytes_read = 0;

  while (bytes_to_read > 0 && file_cache_.size() != 0) {
    int bytes_read = ReadFromCache(
        static_cast<char*>(out_buffer) + total_bytes_read, bytes_to_read);
    SB_CHECK_GE(bytes_read, 0);
    bytes_to_read -= bytes_read;
    total_bytes_read += bytes_read;
    RefillCacheIfEmpty();
  }

  return total_bytes_read;
}

const std::string& FileCacheReader::GetAbsolutePathName() const {
  return absolute_path_;
}

int64_t FileCacheReader::GetSize() {
  EnsureFileOpened();
  return file_->GetSize();
}

void FileCacheReader::EnsureFileOpened() {
  if (file_) {
    return;
  }
  file_.reset(new ScopedFile(absolute_path_.c_str(), 0));
  SB_CHECK(file_->IsValid());

  max_file_cache_size_ =
      std::min(default_file_cache_size_, static_cast<int>(file_->GetSize()));
  file_cache_.resize(max_file_cache_size_);
  file_cache_offset_ = max_file_cache_size_;
}

int FileCacheReader::ReadFromCache(void* out_buffer, int bytes_to_read) {
  SB_CHECK_LE(static_cast<size_t>(file_cache_offset_), file_cache_.size());
  bytes_to_read = std::min(
      static_cast<int>(file_cache_.size()) - file_cache_offset_, bytes_to_read);
  memcpy(out_buffer, file_cache_.data() + file_cache_offset_, bytes_to_read);
  file_cache_offset_ += bytes_to_read;
  return bytes_to_read;
}

void FileCacheReader::RefillCacheIfEmpty() {
  if (static_cast<size_t>(file_cache_offset_) != file_cache_.size()) {
    return;
  }
  file_cache_offset_ = 0;
  int bytes_read = file_->ReadAll(file_cache_.data(), file_cache_.size());
  SB_CHECK_GE(bytes_read, 0);
  if (bytes_read < static_cast<int>(file_cache_.size())) {
    file_cache_.resize(bytes_read);
  }
}

}  // namespace starboard
