// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#ifndef MEDIA_STARBOARD_DECODER_BUFFER_ALLOCATOR_H_
#define MEDIA_STARBOARD_DECODER_BUFFER_ALLOCATOR_H_

#include <atomic>
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>

#include "base/compiler_specific.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/task/thread_pool.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "build/build_config.h"
#include "media/base/decoder_buffer.h"
#include "media/starboard/decoder_buffer_memory_info.h"
#include "starboard/media.h"

namespace media {

class DecoderBufferAllocator : public DecoderBuffer::Allocator,
                               public DecoderBufferMemoryInfo {
 public:
  // Manages the details of the Allocate() and Free(), to allow
  // DecoderBufferAllocator to adopt different strategies at runtime.
  // The class isn't required to be thread safe, and relies on
  // DecoderBufferAllocator to properly guard calls to its member functions with
  // mutex.
  class Strategy {
   public:
    // Temporary configuration structure for experimental strategy parameters,
    // allowing strategies to selectively adopt these settings.
    struct ExperimentConfig {
      // The initial capacity of the memory pool.
      size_t initial_capacity = 0;
      // The fallback allocation increment.
      size_t allocation_increment = 0;
      // Whether to perform any decommits when idle.
      bool enable_decommit_on_idle = false;
      // Number of blocks to keep fully committed when idle.
      size_t retain_blocks = 0;
      // Number of blocks beyond retain blocks to lazily decommit (e.g. using
      // MADV_FREE if supported). Any blocks beyond these are aggressively
      // decommitted (e.g. using MADV_DONTNEED).
      size_t conservative_decommit_blocks = 0;
      // Whether to aggressively decommit all idle blocks when app is suspended.
      bool aggressive_decommit_on_suspend = false;
      // Whether fallback allocations align to page boundary.
      bool allocate_with_page_alignment = false;
      // Whether to zero out fallback blocks on reclamation.
      bool memset_on_reclaim = false;
      // Whether to advise MADV_COLD on reclamation.
      bool mark_as_cold_on_reclaim = false;
    };

    virtual ~Strategy() {}
    virtual void* Allocate(DemuxerStream::Type type, size_t size) = 0;
    virtual void Free(DemuxerStream::Type type, void* p) = 0;
    virtual void Write(void* p, const void* data, size_t size) = 0;

    virtual size_t GetCapacity() const = 0;
    virtual size_t GetAllocated() const = 0;

    virtual void DecommitAllDecommitableBlocks() = 0;
    virtual void TryToDecommitOneBlock(int cadence) {}
  };

  using StrategyCreateCB =
      base::RepeatingCallback<std::unique_ptr<Strategy>(int initial_capacity,
                                                        int allocation_unit)>;

  explicit DecoderBufferAllocator();
  DecoderBufferAllocator(bool is_memory_pool_allocated_on_demand,
                         int initial_capacity,
                         int allocation_unit);
  ~DecoderBufferAllocator() override;

  static DecoderBufferAllocator* Get();

  void DecommitAllDecommitableBlocks();

  // DecoderBuffer::Allocator methods.
  Handle Allocate(DemuxerStream::Type type, size_t size) override;
  void Free(DemuxerStream::Type type, Handle p, size_t size) override;
  void Write(Handle handle, const void* data, size_t size) override;

  base::TimeDelta GetBufferGarbageCollectionDurationThreshold() const override;

  // DecoderBufferMemoryInfo methods.
  size_t GetAllocatedMemory() const override LOCKS_EXCLUDED(mutex_);
  size_t GetCurrentMemoryCapacity() const override LOCKS_EXCLUDED(mutex_);
  size_t GetMaximumMemoryCapacity() const override LOCKS_EXCLUDED(mutex_);

  void UpdateAllocatorStrategy(StrategyCreateCB create_cb);

  // Utility function for h5vcc settings.
  // TODO(b/460292554): To be deprecated with h5vcc settings.
  static base::expected<void, std::string> SetSetting(const std::string& name,
                                                      int value);

 private:
  // Thread-safe state container for periodic decommit tasks running on
  // ThreadPool.
  //
  // Holds a pointer to the owner DecoderBufferAllocator. Allows background
  // tasks to run safely on ThreadPool without dangling pointer risks during
  // teardown.
  class PeriodicDecommitState final
      : public base::RefCountedThreadSafe<PeriodicDecommitState> {
   public:
    explicit PeriodicDecommitState(DecoderBufferAllocator* allocator)
        : allocator_(allocator) {}

    PeriodicDecommitState(const PeriodicDecommitState&) = delete;
    PeriodicDecommitState& operator=(const PeriodicDecommitState&) = delete;

    void Detach() {
      base::AutoLock lock(lock_);
      allocator_ = nullptr;
    }

    template <typename Task>
    void RunIfValid(Task task) {
      base::AutoLock lock(lock_);
      if (allocator_) {
        task(allocator_);
      }
    }

   private:
    friend class base::RefCountedThreadSafe<PeriodicDecommitState>;
    ~PeriodicDecommitState() = default;

    base::Lock lock_;
    raw_ptr<DecoderBufferAllocator> allocator_ GUARDED_BY(lock_) = nullptr;
  };

  static void OnPeriodicDecommitTask(
      scoped_refptr<PeriodicDecommitState> state);

  // Utility functions for h5vcc settings.
  // TODO(b/460292554): To be deprecated with h5vcc settings.
  static void EnableConfigurableDecommitStrategy(
      Strategy::ExperimentConfig strategy_config,
      bool enable_decommit_on_suspend,
      bool periodic_decommit);
  static void EnableMediaBufferPoolStrategy();

  void EnsureStrategyIsCreated() EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void EnablePeriodicDecommitLoop();
  void StopPeriodicDecommitLoop();
  void SchedulePeriodicDecommitTask_Locked() EXCLUSIVE_LOCKS_REQUIRED(mutex_);

#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  void TryFlushAllocationLog_Locked() EXCLUSIVE_LOCKS_REQUIRED(mutex_);
#endif  // !BUILDFLAG(COBALT_IS_RELEASE_BUILD)

  const bool is_memory_pool_allocated_on_demand_;
  const int initial_capacity_;
  const int allocation_unit_;

  mutable base::Lock mutex_;
  std::unique_ptr<Strategy> strategy_ GUARDED_BY(mutex_);
  bool is_strategy_switch_pending_ GUARDED_BY(mutex_) = false;
  StrategyCreateCB experimental_strategy_create_cb_ GUARDED_BY(mutex_);
  scoped_refptr<PeriodicDecommitState> periodic_decommit_state_
      GUARDED_BY(mutex_);

#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  // The following variables are used for comprehensive logging of allocation
  // operations.
  std::stringstream pending_allocation_operations_ GUARDED_BY(mutex_);
  int pending_allocation_operations_count_ GUARDED_BY(mutex_) = 0;
  int allocation_operation_index_ GUARDED_BY(mutex_) = 0;
#endif  // !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  std::atomic<bool> decommit_on_suspend_enabled_{false};
};

}  // namespace media

#endif  // MEDIA_STARBOARD_DECODER_BUFFER_ALLOCATOR_H_
