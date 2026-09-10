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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/common/time.h"
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

  inline static std::atomic<int64_t> last_logged_us_{0};
  inline static std::atomic<int64_t> alloc_count_{0};
  inline static std::atomic<int64_t> alloc_bytes_{0};

  static void RecordAllocation(int size) {
    alloc_count_.fetch_add(1, std::memory_order_relaxed);
    alloc_bytes_.fetch_add(size, std::memory_order_relaxed);

    int64_t last_time = last_logged_us_.load(std::memory_order_relaxed);
    int64_t now = CurrentMonotonicTime();
    if (last_time == 0) {
      last_logged_us_.compare_exchange_strong(last_time, now,
                                              std::memory_order_relaxed);
      return;
    }

    constexpr int64_t kLogIntervalUs = 5 * 1'000'000LL;  // 5 seconds
    if (now - last_time >= kLogIntervalUs) {
      if (last_logged_us_.compare_exchange_strong(last_time, now,
                                                  std::memory_order_relaxed)) {
        int64_t count = alloc_count_.exchange(0, std::memory_order_relaxed);
        int64_t bytes = alloc_bytes_.exchange(0, std::memory_order_relaxed);
        double elapsed_sec = (now - last_time) / 1'000'000.0;
        if (elapsed_sec > 0.0) {
          double alloc_per_sec = count / elapsed_sec;
          double mb_per_sec = (bytes / (1024.0 * 1024.0)) / elapsed_sec;
          int64_t avg_size = count > 0 ? bytes / count : 0;
          SB_LOG(INFO) << "[AudioBuffer] Allocations: " << alloc_per_sec
                       << " allocs/sec (" << count << " in " << elapsed_sec
                       << "s), Throughput: " << mb_per_sec << " MB/sec, "
                       << "Avg size: " << avg_size << " bytes.";
        }
      }
    }
  }

  Buffer() = default;
  explicit Buffer(int size)
      : size_(size), data_(new uint8_t[size + kPaddingSize * 2]) {
    RecordAllocation(size_);
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
    RecordAllocation(size_);

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
