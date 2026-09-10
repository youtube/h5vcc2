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

#ifndef COBALT_BROWSER_H5VCC_MEMORY_LOW_MEMORY_MANAGER_H_
#define COBALT_BROWSER_H5VCC_MEMORY_LOW_MEMORY_MANAGER_H_

#include <stddef.h>

#include "base/functional/callback.h"
#include "base/no_destructor.h"
#include "cobalt/browser/h5vcc_memory/public/mojom/h5vcc_memory.mojom.h"
#include "cobalt/common/cobalt_thread_checker.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote_set.h"

namespace cobalt {
namespace browser {

using h5vcc_memory::mojom::LowMemoryListener;

// This class is a singleton that provides a central point for handling low
// memory notifications received from the platform/Starboard. It manages a set
// of mojom::LowMemoryListener remotes and forwards the notification to all
// registered listeners in renderer processes.
class LowMemoryManager {
 public:
  // Get the singleton instance.
  static LowMemoryManager* GetInstance();

  LowMemoryManager(const LowMemoryManager&) = delete;
  LowMemoryManager& operator=(const LowMemoryManager&) = delete;

  void AddListener(mojo::PendingRemote<LowMemoryListener> listener);
  void OnLowMemory();

  size_t num_listeners() const;
  void SetOnListenerAddedCallbackForTesting(base::RepeatingClosure callback);

 private:
  friend class base::NoDestructor<LowMemoryManager>;

  LowMemoryManager();
  ~LowMemoryManager();

  mojo::RemoteSet<LowMemoryListener> listeners_;
  base::RepeatingClosure on_listener_added_callback_;

  COBALT_THREAD_CHECKER(thread_checker_);
};

}  // namespace browser
}  // namespace cobalt

#endif  // COBALT_BROWSER_H5VCC_MEMORY_LOW_MEMORY_MANAGER_H_
