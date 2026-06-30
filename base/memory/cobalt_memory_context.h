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

#ifndef BASE_MEMORY_COBALT_MEMORY_CONTEXT_H_
#define BASE_MEMORY_COBALT_MEMORY_CONTEXT_H_

#include <atomic>
#include <cstdint>
#include <string_view>
#include <pthread.h>

#include "base/base_export.h"
#include "build/build_config.h"

namespace base {
namespace memory {

enum class MemoryContext : uint8_t {
  kUnknown = 0,
  kDOM = 1,
  kLayout = 2,
  kMedia = 3,
  kScript = 4,
  kNetwork = 5,
  kGraphics = 6,
  kStorage = 7,

  // Granular Sub-Regions
  kGraphicsCanvas = 8,
  kGraphicsCompositor = 9,
  kGraphicsGlyphs = 10,
  kScriptHeap = 11,
  kScriptJIT = 12,
  kScriptBindings = 13,
  kNetworkLoader = 14,
  kNetworkCache = 15,
  kBlinkDOM = 16,
  kBlinkStyle = 17,  // CSS Style resolution
  kBlinkParser = 18,
  kPlatformIPC = 19,
  kPlatformStarboard = 20,
  kPlatformDevTools = 21,
  kBrowserMain = 22,

  kCount
};

#if defined(__GNUC__)
#define MAYBE_COBALT_WEAK __attribute__((weak))
#else
#define MAYBE_COBALT_WEAK
#endif

pthread_key_t GetSharedMemoryContextKey();
MemoryContext GetCurrentMemoryContext();
void SetCurrentMemoryContext(MemoryContext context);

// ScopedMemoryContext is a helper class that sets the current thread's
// memory context for the duration of its lifetime, restoring the previous
// context upon destruction.
//
// Lifetime and Ownership:
// This is a stack-allocated helper designed to be used as a local variable.
// It should not be dynamically allocated or outlive its enclosing scope.
//
// Threading Model:
// This class is thread-affine and must only be used on the thread where
// it was constructed.
class BASE_EXPORT ScopedMemoryContext {
 public:
  explicit ScopedMemoryContext(MemoryContext context) {
    prev_context_ = GetCurrentMemoryContext();
    SetCurrentMemoryContext(context);
  }

  ~ScopedMemoryContext() {
    SetCurrentMemoryContext(prev_context_);
  }

  ScopedMemoryContext(const ScopedMemoryContext&) = delete;
  ScopedMemoryContext& operator=(const ScopedMemoryContext&) = delete;
  ScopedMemoryContext(ScopedMemoryContext&&) = delete;
  ScopedMemoryContext& operator=(ScopedMemoryContext&&) = delete;

 private:
  MemoryContext prev_context_;
};

MAYBE_COBALT_WEAK std::string_view ContextToString(MemoryContext context);

}  // namespace memory
}  // namespace base

#endif  // BASE_MEMORY_COBALT_MEMORY_CONTEXT_H_
