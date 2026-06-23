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

#ifndef COBALT_RENDERER_COBALT_DISCARDABLE_SHARED_MEMORY_MANAGER_H_
#define COBALT_RENDERER_COBALT_DISCARDABLE_SHARED_MEMORY_MANAGER_H_

#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "components/discardable_memory/client/client_discardable_shared_memory_manager.h"
#include "components/discardable_memory/public/mojom/discardable_shared_memory_manager.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"

namespace cobalt {

// Creates a ClientDiscardableSharedMemoryManager that closes each segment's
// fd right after Map(), see cobalt_discardable_shared_memory_manager.cc.
scoped_refptr<discardable_memory::ClientDiscardableSharedMemoryManager>
CreateCobaltDiscardableSharedMemoryManager(
    mojo::PendingRemote<
        discardable_memory::mojom::DiscardableSharedMemoryManager> manager,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner);

}  // namespace cobalt

#endif  // COBALT_RENDERER_COBALT_DISCARDABLE_SHARED_MEMORY_MANAGER_H_
