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

#include "media/starboard/decoder_buffer_allocator.h"

#include <sys/mman.h>  // For MADV_COLD

#include <algorithm>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/types/expected.h"
#include "build/build_config.h"
#include "media/base/media_switches.h"
#include "media/base/starboard/sbmedia_interface.h"
#include "media/base/video_codecs.h"
#include "media/starboard/bidirectional_fit_decoder_buffer_allocator_strategy.h"
#include "media/starboard/media_buffer_pool_decoder_buffer_allocator_strategy.h"
#include "media/starboard/starboard_utils.h"
#include "starboard/common/allocator.h"
#include "starboard/common/embedded_metadata_reuse_allocator_base.h"
#include "starboard/common/experimental/media_buffer_pool.h"
#include "starboard/common/external_metadata_reuse_allocator_base.h"
#include "starboard/common/log.h"
#include "starboard/configuration.h"

namespace media {

namespace {

constexpr base::TimeDelta kDefaultPeriodicDecommitInterval = base::Seconds(5);

// The current default AllocatorStrategy is EmbeddedMetadataReuseAllocatorBase.
// To see more context as to why this is the case, see b/487332929.
using DefaultReuseAllocatorStrategy =
    BidirectionalFitDecoderBufferAllocatorStrategy<
        starboard::EmbeddedMetadataReuseAllocatorBase>;
using starboard::experimental::MediaBufferPool;

const char* ToString(bool value) {
  return value ? "enabled" : "disabled";
}

template <typename Callback>
base::expected<void, std::string> ProcessEnableOnlySetting(
    const std::string& name,
    int value,
    Callback on_enable) {
  if (value == 0) {
    return base::unexpected(name + " cannot be disabled.");
  }
  on_enable();
  return base::ok();
}

}  // namespace

DecoderBufferAllocator::DecoderBufferAllocator()
    : DecoderBufferAllocator(
          GetSbMediaInterface()->IsBufferPoolAllocateOnDemand(),
          GetSbMediaInterface()->GetInitialBufferCapacity(),
          GetSbMediaInterface()->GetBufferAllocationUnit()) {}

DecoderBufferAllocator::DecoderBufferAllocator(
    bool is_memory_pool_allocated_on_demand,
    int initial_capacity,
    int allocation_unit)
    : is_memory_pool_allocated_on_demand_(is_memory_pool_allocated_on_demand),
      initial_capacity_(initial_capacity),
      allocation_unit_(allocation_unit) {
  DCHECK_GE(initial_capacity_, 0);
  DCHECK_GE(allocation_unit_, 0);

  base::UmaHistogramBoolean("Cobalt.Media.IsBufferPoolAllocateOnDemand",
                            is_memory_pool_allocated_on_demand_);

  if (is_memory_pool_allocated_on_demand_) {
    LOG(INFO) << "Allocated decoder buffer pool on demand.";
    return;
  }

  base::AutoLock scoped_lock(mutex_);
  EnsureStrategyIsCreated();
}

DecoderBufferAllocator::~DecoderBufferAllocator() {
  StopPeriodicDecommitLoop();

  base::AutoLock scoped_lock(mutex_);

  if (strategy_) {
    DCHECK_EQ(strategy_->GetAllocated(), 0u);
    strategy_.reset();
  }
}

// static
DecoderBufferAllocator* DecoderBufferAllocator::Get() {
  return static_cast<DecoderBufferAllocator*>(DecoderBuffer::Allocator::Get());
}

void DecoderBufferAllocator::DecommitAllDecommitableBlocks() {
  if (!decommit_on_suspend_enabled_.load(std::memory_order_acquire)) {
    return;
  }
  base::AutoLock scoped_lock(mutex_);
  if (strategy_) {
    strategy_->DecommitAllDecommitableBlocks();
  }
}

DecoderBuffer::Allocator::Handle DecoderBufferAllocator::Allocate(
    DemuxerStream::Type type,
    size_t size) {
  base::AutoLock scoped_lock(mutex_);

  EnsureStrategyIsCreated();

  void* p = strategy_->Allocate(type, size);
  CHECK(p);

#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  if (starboard::Allocator::ExtraLogLevel() >= 2) {
    ++pending_allocation_operations_count_;
    pending_allocation_operations_ << " a " << p << " " << type << " " << size;
    TryFlushAllocationLog_Locked();
  }
#endif  // !BUILDFLAG(COBALT_IS_RELEASE_BUILD)

  return reinterpret_cast<Handle>(p);
}

void DecoderBufferAllocator::Free(DemuxerStream::Type type,
                                  Handle handle,
                                  size_t size) {
  void* p = reinterpret_cast<void*>(handle);

  if (p == nullptr) {
    DCHECK_EQ(size, 0u);
    return;
  }

  base::AutoLock scoped_lock(mutex_);

  DCHECK(strategy_);

  strategy_->Free(type, p);

#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  if (starboard::Allocator::ExtraLogLevel() >= 2) {
    ++pending_allocation_operations_count_;
    pending_allocation_operations_ << " f " << p;
    TryFlushAllocationLog_Locked();
  }
#endif  // !BUILDFLAG(COBALT_IS_RELEASE_BUILD)

  bool should_reset_strategy =
      is_strategy_switch_pending_ || is_memory_pool_allocated_on_demand_;

  if (should_reset_strategy && strategy_->GetAllocated() == 0) {
    // `strategy_->PrintAllocations()` will be called inside the dtor when
    // supported, so it shouldn't be called here.
    LOG(INFO) << "Freeing " << strategy_->GetCapacity()
              << " bytes of decoder buffer pool.";
    strategy_.reset();
  }
}

void DecoderBufferAllocator::Write(Handle handle,
                                   const void* data,
                                   size_t size) {
  // The lock adds overhead to the cases where |handle| is a pointer, so we take
  // a short cut to ensure that there is no overhead adding to our existing
  // logic.
  using ::starboard::experimental::IsPointerAnnotated;

  if (!IsPointerAnnotated(handle)) {
    memcpy(reinterpret_cast<void*>(handle), data, size);
    return;
  }

  // TODO(b/369245553): Consider combining Allocate() and Write() into one
  //                    function to avoid the extra lock.
  base::AutoLock scoped_lock(mutex_);
  DCHECK(strategy_);
  strategy_->Write(reinterpret_cast<void*>(handle), data, size);
}

base::TimeDelta
DecoderBufferAllocator::GetBufferGarbageCollectionDurationThreshold() const {
  return base::Microseconds(
      GetSbMediaInterface()->GetBufferGarbageCollectionDurationThreshold());
}

size_t DecoderBufferAllocator::GetAllocatedMemory() const {
  base::AutoLock scoped_lock(mutex_);
  return strategy_ ? strategy_->GetAllocated() : 0;
}

size_t DecoderBufferAllocator::GetCurrentMemoryCapacity() const {
  base::AutoLock scoped_lock(mutex_);
  return strategy_ ? strategy_->GetCapacity() : 0;
}

size_t DecoderBufferAllocator::GetMaximumMemoryCapacity() const {
  // Always returns 0, as we no longer cap the capacity since Cobalt 25.
  //
  // base::AutoLock scoped_lock(mutex_);
  return 0;
}

void DecoderBufferAllocator::UpdateAllocatorStrategy(
    StrategyCreateCB create_cb) {
  DCHECK(!create_cb.is_null());

  base::AutoLock scoped_lock(mutex_);
  experimental_strategy_create_cb_ = std::move(create_cb);
  is_strategy_switch_pending_ = true;

  if (strategy_ && strategy_->GetAllocated() > 0) {
    LOG(INFO) << "Strategy switch pending. Waiting for memory to drain.";
    return;
  }
  if (strategy_) {
    strategy_.reset();
  }
}

// static
base::expected<void, std::string> DecoderBufferAllocator::SetSetting(
    const std::string& name,
    int value) {
  // Temporarily accept both setting names to support backward-compatible
  // experiment rollouts before transitioning to the V2 format.
  if (name == "DecoderBuffer.EnableConfigurableDecommitStrategy" ||
      name == "DecoderBuffer.EnableConfigurableDecommitStrategyV2") {
    if (value <= 0) {
      // Explicitly allow non-positive values as placebo.
      return base::ok();
    }

    // The value is a 32-bit integer encoding four parts:
    // flags (8 bits), block_size (in MB), retain_blocks (count), and
    // conservative_decommit_blocks (count). For example,
    // 0x01040402 sets aggressive_decommit_on_suspend to true, 4 MB block size,
    // 4 retain blocks, and 2 conservative decommit blocks respectively.
    // Passing multiple parameters encoded within a single integer is not
    // ideal, but it simplifies experiment setup in the current framework.
    //
    // - flags (8 bits):
    //   - enable_decommit_on_suspend (bit 24): When set (bit 24 is non-zero),
    //     enables decommit on all idle blocks when app suspends/hides.
    //   - allocate_with_page_alignment (bit 25): When set (bit 25 is
    //     non-zero), forces new OS block allocations to be page-aligned and
    //     rounded up to page sizes. Default is true.
    //   - aggressive_decommit_on_suspend (bit 26): When set (bit 26 is
    //     non-zero), aggressively decommits all idle blocks (ignoring retain
    //     window) on suspend.
    //   - memset_on_reclaim (bit 27): When set (bit 27 is non-zero),
    //     memsets reclaimed blocks to 0.
    //   - mark_as_cold_on_reclaim (bit 28): When set (bit 28 is non-zero),
    //     uses MADV_COLD on reclaimed blocks.
    //   - periodic_decommit (bit 29): When set (bit 29 is non-zero),
    //     periodically decommits idle blocks in the background via
    //     base::ThreadPool.
    // - block_size (8 bits): Specifies both the initial pool capacity and the
    //   fallback allocation increment.
    // - retain_blocks (8 bits): Specifies the first `retain_blocks` blocks of
    //   the pool are kept fully committed.
    // - conservative_decommit_blocks (8 bits): Specifies the next
    //   `conservative_decommit_blocks` blocks are conservatively decommitted
    //   (e.g. using MADV_FREE).
    // Any remaining memory beyond these two windows is aggressively decommitted
    // (e.g. MADV_DONTNEED).
    // For example, if 128 MB is allocated for the memory pool, and
    // retain_blocks is set to 4 with conservative_decommit_blocks set to 2
    // (with 4 MB block size):
    //   - The first 16 MB (4 blocks) will be retained during an idle flush.
    //   - The next 8 MB (2 blocks) will be conservatively decommitted (the OS
    //     may reclaim it if under memory pressure, but it is not freed
    //     immediately).
    //   - The remaining 104 MB (26 blocks) will be aggressively decommitted
    //   (returned
    //     to the OS immediately, with virtual memory address range retained).
    //
    // Note: A value of 0 or less will not enable this feature (handled as a
    // placebo).
    uint32_t unsigned_value = static_cast<uint32_t>(value);
    uint32_t flags = unsigned_value >> 24;

    size_t block_size_mb = (unsigned_value >> 16) & 0xFF;

    Strategy::ExperimentConfig strategy_config;
    strategy_config.initial_capacity = block_size_mb * 1024 * 1024;
    strategy_config.allocation_increment = block_size_mb * 1024 * 1024;
    strategy_config.enable_decommit_on_idle = true;
    strategy_config.retain_blocks = (unsigned_value >> 8) & 0xFF;
    strategy_config.conservative_decommit_blocks = unsigned_value & 0xFF;
    strategy_config.aggressive_decommit_on_suspend = (flags & 0x04) != 0;
    strategy_config.allocate_with_page_alignment = (flags & 0x02) != 0;
    strategy_config.memset_on_reclaim = (flags & 0x08) != 0;
    strategy_config.mark_as_cold_on_reclaim = (flags & 0x10) != 0;

    bool enable_decommit_on_suspend = (flags & 0x01) != 0;
    bool periodic_decommit = (flags & 0x20) != 0;

    EnableConfigurableDecommitStrategy(
        strategy_config, enable_decommit_on_suspend, periodic_decommit);
    return base::ok();
  }
  if (name == "DecoderBuffer.EnableMediaBufferPoolAllocatorStrategy") {
    return ProcessEnableOnlySetting(name, value,
                                    [] { EnableMediaBufferPoolStrategy(); });
  }
  return base::unexpected(name + " isn't a supported setting.");
}

// static
void DecoderBufferAllocator::OnPeriodicDecommitTask(
    scoped_refptr<PeriodicDecommitState> state) {
  state->RunIfValid([](DecoderBufferAllocator* allocator) {
    base::AutoLock scoped_lock(allocator->mutex_);
    if (allocator->strategy_) {
      allocator->strategy_->TryToDecommitOneBlock(/*cadence=*/1);
    }
    allocator->SchedulePeriodicDecommitTask_Locked();
  });
}

// static
void DecoderBufferAllocator::EnableConfigurableDecommitStrategy(
    Strategy::ExperimentConfig strategy_config,
    bool enable_decommit_on_suspend,
    bool periodic_decommit) {
  auto* allocator = Get();
  CHECK(allocator);
  // Checking MADV_COLD support directly here simplifies experimental setup
  // without adding extra platform abstraction layers.
#if !defined(MADV_COLD)
  if (strategy_config.mark_as_cold_on_reclaim) {
    LOG(INFO) << "mark_as_cold_on_reclaim is set, but MADV_COLD is not "
                 "defined. Resetting mark_as_cold_on_reclaim to false.";
    strategy_config.mark_as_cold_on_reclaim = false;
  }
#endif  // !defined(MADV_COLD)
  if (strategy_config.aggressive_decommit_on_suspend) {
    enable_decommit_on_suspend = true;
  }
  if (enable_decommit_on_suspend) {
    // This optimization is enable only, and isn't expected to be disabled.
    allocator->decommit_on_suspend_enabled_.store(true,
                                                  std::memory_order_release);
  }
  if (periodic_decommit) {
    // This feature is enable only, and isn't expected to be disabled.
    allocator->EnablePeriodicDecommitLoop();
  }
  allocator->UpdateAllocatorStrategy(base::BindRepeating(
      [](const Strategy::ExperimentConfig& strategy_config,
         bool enable_decommit_on_suspend, bool periodic_decommit,
         int initial_capacity, int allocation_unit)
          -> std::unique_ptr<DecoderBufferAllocator::Strategy> {
        LOG(INFO)
            << "DecoderBufferAllocator is using "
               "DefaultReuseAllocatorStrategy with configurable decommit. "
            << "initial_capacity (" << initial_capacity
            << ") and allocation_unit (" << allocation_unit
            << ") are ignored. Using "
            << "initial_capacity: " << strategy_config.initial_capacity << ", "
            << "allocation_increment: " << strategy_config.allocation_increment
            << ", "
            << "retain_blocks: " << strategy_config.retain_blocks << ", "
            << "conservative_decommit_blocks: "
            << strategy_config.conservative_decommit_blocks
            << ", enable_decommit_on_suspend: "
            << ToString(enable_decommit_on_suspend)
            << ", allocate_with_page_alignment: "
            << ToString(strategy_config.allocate_with_page_alignment)
            << ", aggressive_decommit_on_suspend: "
            << ToString(strategy_config.aggressive_decommit_on_suspend)
            << ", memset_on_reclaim: "
            << ToString(strategy_config.memset_on_reclaim)
            << ", mark_as_cold_on_reclaim: "
            << ToString(strategy_config.mark_as_cold_on_reclaim)
            << ", periodic_decommit: " << ToString(periodic_decommit);

        return std::make_unique<DefaultReuseAllocatorStrategy>(strategy_config);
      },
      strategy_config, enable_decommit_on_suspend, periodic_decommit));
}

// static
void DecoderBufferAllocator::EnableMediaBufferPoolStrategy() {
  auto* allocator = Get();
  CHECK(allocator);
  allocator->UpdateAllocatorStrategy(base::BindRepeating(
      [](int initial_capacity, int allocation_unit)
          -> std::unique_ptr<DecoderBufferAllocator::Strategy> {
        auto pool = MediaBufferPool::Acquire();
        if (pool) {
          LOG(INFO) << "DecoderBufferAllocator is using MediaBufferPool.";
          return std::make_unique<
              MediaBufferPoolDecoderBufferAllocatorStrategy>(
              pool, initial_capacity, allocation_unit);
        }
        LOG(INFO) << "DecoderBufferAllocator failed to enable MediaBufferPool"
                  << " as MediaBufferPool::Acquire() returns nullptr.";
        return nullptr;
      }));
}

void DecoderBufferAllocator::EnsureStrategyIsCreated() {
  mutex_.AssertAcquired();
  if (strategy_) {
    return;
  }

  is_strategy_switch_pending_ = false;

  if (!experimental_strategy_create_cb_.is_null()) {
    strategy_ = experimental_strategy_create_cb_.Run(initial_capacity_,
                                                     allocation_unit_);
    if (strategy_) {
      LOG(INFO) << "Allocated " << initial_capacity_
                << " bytes for decoder buffer pool.";
      return;
    }
    LOG(WARNING) << "Failed to create the requested DecoderBufferAllocator "
                    "strategy. Falling back to default.";
  }

  strategy_ = std::make_unique<DefaultReuseAllocatorStrategy>(initial_capacity_,
                                                              allocation_unit_);
  LOG(INFO) << "DecoderBufferAllocator is using "
               "DefaultReuseAllocatorStrategy.";

  LOG(INFO) << "Allocated " << initial_capacity_
            << " bytes for decoder buffer pool.";
}

void DecoderBufferAllocator::EnablePeriodicDecommitLoop() {
  base::AutoLock scoped_lock(mutex_);
  if (periodic_decommit_state_) {
    return;
  }
  periodic_decommit_state_ = base::MakeRefCounted<PeriodicDecommitState>(this);
  SchedulePeriodicDecommitTask_Locked();
}

void DecoderBufferAllocator::StopPeriodicDecommitLoop() {
  scoped_refptr<PeriodicDecommitState> state_to_detach;
  {
    base::AutoLock scoped_lock(mutex_);
    state_to_detach = std::move(periodic_decommit_state_);
  }
  if (state_to_detach) {
    state_to_detach->Detach();
  }
}

void DecoderBufferAllocator::SchedulePeriodicDecommitTask_Locked() {
  mutex_.AssertAcquired();

  if (!periodic_decommit_state_) {
    return;
  }

  base::ThreadPool::PostDelayedTask(
      FROM_HERE,
      {base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&DecoderBufferAllocator::OnPeriodicDecommitTask,
                     periodic_decommit_state_),
      kDefaultPeriodicDecommitInterval);
}

#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
void DecoderBufferAllocator::TryFlushAllocationLog_Locked() {
  const int kMaxOperationsPerLog = 80;

  mutex_.AssertAcquired();

  // The allocation operations may generate a few hundred log lines per second
  // and lead to missing entries on some platforms.  Grouping them helps avoid
  // missing entries, and the log index will be verified in the processing
  // script.
  if ((allocation_operation_index_ + pending_allocation_operations_count_) %
              kMaxOperationsPerLog ==
          0 ||
      strategy_->GetAllocated() == 0) {
    SB_LOG(INFO) << " Media Allocation Log: " << allocation_operation_index_
                 << pending_allocation_operations_.str();

    allocation_operation_index_ += pending_allocation_operations_count_;
    pending_allocation_operations_count_ = 0;
    // Reset the pending log
    pending_allocation_operations_.str("");
    pending_allocation_operations_.clear();
  }
}
#endif  // !BUILDFLAG(COBALT_IS_RELEASE_BUILD)

}  // namespace media
