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

#include "cobalt/renderer/cobalt_discardable_shared_memory_manager.h"

#include <stdint.h>

#include <memory>
#include <utility>

#include "base/memory/discardable_shared_memory.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "components/discardable_memory/client/client_discardable_shared_memory_manager.h"
#include "mojo/public/cpp/bindings/pending_remote.h"

namespace cobalt {

namespace {

// Closes each discardable segment's /dev/shm file descriptor right after it
// is mapped, mirroring what the in-process and service-for-remote-client
// allocation paths in ClientDiscardableSharedMemoryManager already do. Only
// Android needs the fd kept open past Map() (Lock()/Unlock() use ashmem
// ioctls on it); everywhere else it's unused dead weight that otherwise
// accumulates one fd per live discardable segment, bounded only by a byte
// budget rather than an fd count, until RLIMIT_NOFILE is exhausted during a
// long-running session (e.g. the YTLR Browse-and-Watch endurance run).
//
// Lifetime and ownership: ref-counted (via base::RefCountedDeleteOnSequence,
// inherited from the base class) and created exclusively through
// CreateCobaltDiscardableSharedMemoryManager(), which is called once by
// content::CreateDiscardableMemoryAllocator() when the renderer process sets
// up its discardable memory allocator. The returned scoped_refptr is held by
// that allocator for as long as the renderer process needs discardable
// memory, i.e. for the lifetime of the process; there is no other owner and
// no expectation of being constructed or destroyed more than once.
//
// Threading model: inherits the base class's threading model unchanged —
// safe to call from any thread, with the actual IPC and shared-memory
// allocation work for AllocateLockedDiscardableSharedMemory() performed on
// |io_task_runner|. This subclass only adds a synchronous Close() call after
// the base implementation returns, on the same thread/sequence the base
// class already runs that allocation on, so no new threading considerations
// are introduced.
class CobaltDiscardableSharedMemoryManager
    : public discardable_memory::ClientDiscardableSharedMemoryManager {
 public:
  CobaltDiscardableSharedMemoryManager(
      mojo::PendingRemote<
          discardable_memory::mojom::DiscardableSharedMemoryManager> manager,
      scoped_refptr<base::SingleThreadTaskRunner> io_task_runner)
      : discardable_memory::ClientDiscardableSharedMemoryManager(
            std::move(manager),
            std::move(io_task_runner)) {}

  CobaltDiscardableSharedMemoryManager(
      const CobaltDiscardableSharedMemoryManager&) = delete;
  CobaltDiscardableSharedMemoryManager& operator=(
      const CobaltDiscardableSharedMemoryManager&) = delete;

 protected:
  ~CobaltDiscardableSharedMemoryManager() override = default;

 private:
  std::unique_ptr<base::DiscardableSharedMemory>
  AllocateLockedDiscardableSharedMemory(size_t size, int32_t id) override {
    std::unique_ptr<base::DiscardableSharedMemory> memory =
        discardable_memory::ClientDiscardableSharedMemoryManager::
            AllocateLockedDiscardableSharedMemory(size, id);

#if !BUILDFLAG(IS_ANDROID)
    if (memory) {
      memory->Close();
    }
#endif

    return memory;
  }
};

}  // namespace

scoped_refptr<discardable_memory::ClientDiscardableSharedMemoryManager>
CreateCobaltDiscardableSharedMemoryManager(
    mojo::PendingRemote<
        discardable_memory::mojom::DiscardableSharedMemoryManager> manager,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner) {
  return base::MakeRefCounted<CobaltDiscardableSharedMemoryManager>(
      std::move(manager), std::move(io_task_runner));
}

}  // namespace cobalt
