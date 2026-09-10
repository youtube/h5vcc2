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

#ifndef STARBOARD_SHARED_STARBOARD_PLAYER_BUFFER_INTERNAL_H_
#define STARBOARD_SHARED_STARBOARD_PLAYER_BUFFER_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "starboard/common/check_op.h"
#include "starboard/shared/internal_only.h"

namespace starboard {

// A buffer containing arbitrary binary data, with life time and size managed.
// It performs better than std::vector<> as it doesn't fill the buffer with 0s.
class Buffer {
 public:
  static constexpr uint8_t kPadding = 0x78;
#if defined(NDEBUG)
  static constexpr int kPaddingSize = 0;
#else   // defined(NDEBUG)
  static constexpr int kPaddingSize = 32;
#endif  // defined(NDEBUG)

  Buffer() = default;
  explicit Buffer(int size)
      : size_(size), data_(new uint8_t[size + kPaddingSize * 2]) {
#if !defined(NDEBUG)
    memset(data_, kPadding, kPaddingSize);
    memset(data_ + kPaddingSize + size_, kPadding, kPaddingSize);
#endif  // !defined(NDEBUG)
  }

  Buffer(const Buffer& that)
      : size_(that.size_),
        data_(that.data_ ? new uint8_t[that.size_ + kPaddingSize * 2]
                         : nullptr) {
    if (!data_) {
      return;
    }
    memcpy(data_, that.data_, size_ + kPaddingSize * 2);
  }
  Buffer(Buffer&& that) : size_(that.size_), data_(that.data_) {
    that.size_ = 0;
    that.data_ = nullptr;
  }
  ~Buffer() {
#if !defined(NDEBUG)
    if (data_) {
      uint8_t buffer[kPaddingSize];
      memset(buffer, kPadding, kPaddingSize);
      SB_CHECK_EQ(memcmp(data_, buffer, kPaddingSize), 0);
      SB_CHECK_EQ(memcmp(data_ + kPaddingSize + size_, buffer, kPaddingSize),
                  0);
    }
#endif  // !defined(NDEBUG)
    delete[] data_;
  }

  Buffer& operator=(Buffer&& that) {
    std::swap(this->size_, that.size_);
    std::swap(this->data_, that.data_);
    return *this;
  }

  int size() const { return size_; }

  uint8_t* data() { return size_ == 0 ? nullptr : data_ + kPaddingSize; }
  const uint8_t* data() const {
    return size_ == 0 ? nullptr : data_ + kPaddingSize;
  }

 private:
  int size_ = 0;
  uint8_t* data_ = nullptr;

  Buffer& operator=(const Buffer& that) = delete;
};

}  // namespace starboard

#endif  // STARBOARD_SHARED_STARBOARD_PLAYER_BUFFER_INTERNAL_H_
