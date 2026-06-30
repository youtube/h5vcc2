// Copyright 2026 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "base/memory/cobalt_memory_context.h"


#include <atomic>
#include <pthread.h>
#include <stdint.h>
#include <string_view>

#if defined(OS_LINUX) || defined(OS_ANDROID)
#include <sys/prctl.h>
#endif

namespace base {
namespace memory {



MAYBE_COBALT_WEAK pthread_key_t GetSharedMemoryContextKey() {
  // Use a static atomic to ensure lazy initialization happens safely.
  // Because this function is weak, the linker will merge all copies into a single instance,
  // meaning `g_key` will be identical across both `base` and `copied_base`.
  static std::atomic<intptr_t> g_key{-1};
  intptr_t key = g_key.load(std::memory_order_acquire);
  if (key == -1) {
    pthread_key_t new_key;
    pthread_key_create(&new_key, nullptr);
    intptr_t expected = -1;
    if (g_key.compare_exchange_strong(expected,
                                      static_cast<intptr_t>(new_key),
                                      std::memory_order_acq_rel)) {
      key = static_cast<intptr_t>(new_key);
    } else {
      pthread_key_delete(new_key);
      key = expected;
    }
  }
  return static_cast<pthread_key_t>(key);
}

MAYBE_COBALT_WEAK MemoryContext GetCurrentMemoryContext() {
  uintptr_t val = reinterpret_cast<uintptr_t>(pthread_getspecific(GetSharedMemoryContextKey()));
  uintptr_t context_val = val & 0xFFFF;
  uintptr_t checks = val >> 16;
  if (context_val == 0) {
    MemoryContext context = MemoryContext::kUnknown;
#if defined(OS_LINUX) || defined(OS_ANDROID)
    char thread_name[17] = {0};
    if (prctl(PR_GET_NAME, thread_name) == 0) {
      thread_name[16] = '\0';
      std::string_view name(thread_name);
      struct ThreadPatternMapping {
        std::string_view pattern;
        MemoryContext context;
        bool exact_match;
      };

      constexpr ThreadPatternMapping kThreadPatterns[] = {
          // Media
          {"Media", MemoryContext::kMedia, false},
          {"Audio", MemoryContext::kMedia, false},
          {"Video", MemoryContext::kMedia, false},
          {"FFmpeg", MemoryContext::kMedia, false},
          {"Decoder", MemoryContext::kMedia, false},
          {"Vpx", MemoryContext::kMedia, false},

          // Network
          {"Network", MemoryContext::kNetwork, false},
          {"IOThread", MemoryContext::kNetwork, false},
          {"Socket", MemoryContext::kNetwork, false},

          // Script
          {"Script", MemoryContext::kScript, false},
          {"V8", MemoryContext::kScript, false},
          {"JS", MemoryContext::kScript, false},
          {"GC", MemoryContext::kScript, false},
          {"Scavenger", MemoryContext::kScript, false},

          // Graphics
          {"Graphics", MemoryContext::kGraphics, false},
          {"Compositor", MemoryContext::kGraphics, false},
          {"Skia", MemoryContext::kGraphics, false},
          {"Ganesh", MemoryContext::kGraphics, false},
          {"Raster", MemoryContext::kGraphics, false},
          {"Gpu", MemoryContext::kGraphics, false},

          // BrowserMain
          {"Browser", MemoryContext::kBrowserMain, false},
          {"CrBrowserMain", MemoryContext::kBrowserMain, false},
          {"CrRendererMain", MemoryContext::kBrowserMain, false},
          {"cobalt", MemoryContext::kBrowserMain, true},

          // PlatformIPC
          {"IPC", MemoryContext::kPlatformIPC, false},
          {"Mojo", MemoryContext::kPlatformIPC, false},
          {"MessagePump", MemoryContext::kPlatformIPC, false},
      };

      for (const auto& entry : kThreadPatterns) {
        if (entry.exact_match) {
          if (name == entry.pattern) {
            context = entry.context;
            break;
          }
        } else {
          if (name.find(entry.pattern) != std::string_view::npos) {
            context = entry.context;
            break;
          }
        }
      }
    }
#endif
    if (context != MemoryContext::kUnknown) {
      val = static_cast<uintptr_t>(context) + 1;
      pthread_setspecific(GetSharedMemoryContextKey(), reinterpret_cast<void*>(val));
      return context;
    } else {
      if (checks < 60000) {
        checks++;
        val = (checks << 16) | 0; // context_val remains 0
        pthread_setspecific(GetSharedMemoryContextKey(), reinterpret_cast<void*>(val));
        return MemoryContext::kUnknown;
      } else {
        val = static_cast<uintptr_t>(MemoryContext::kUnknown) + 1;
        pthread_setspecific(GetSharedMemoryContextKey(), reinterpret_cast<void*>(val));
        return MemoryContext::kUnknown;
      }
    }
  }
  return static_cast<MemoryContext>(context_val - 1);
}

MAYBE_COBALT_WEAK void SetCurrentMemoryContext(MemoryContext context) {
  pthread_setspecific(GetSharedMemoryContextKey(),
                      reinterpret_cast<void*>(static_cast<uintptr_t>(context) + 1));
}

MAYBE_COBALT_WEAK std::string_view ContextToString(MemoryContext context) {
  switch (context) {
    case MemoryContext::kUnknown:
      return "Unknown";
    case MemoryContext::kDOM:
      return "DOM";
    case MemoryContext::kLayout:
      return "Layout";
    case MemoryContext::kMedia:
      return "Media";
    case MemoryContext::kScript:
      return "Script";
    case MemoryContext::kNetwork:
      return "Network";
    case MemoryContext::kGraphics:
      return "Graphics";
    case MemoryContext::kStorage:
      return "Storage";
    case MemoryContext::kGraphicsCanvas:
      return "GraphicsCanvas";
    case MemoryContext::kGraphicsCompositor:
      return "GraphicsCompositor";
    case MemoryContext::kGraphicsGlyphs:
      return "GraphicsGlyphs";
    case MemoryContext::kScriptHeap:
      return "ScriptHeap";
    case MemoryContext::kScriptJIT:
      return "ScriptJIT";
    case MemoryContext::kScriptBindings:
      return "ScriptBindings";
    case MemoryContext::kNetworkLoader:
      return "NetworkLoader";
    case MemoryContext::kNetworkCache:
      return "NetworkCache";
    case MemoryContext::kBlinkDOM:
      return "BlinkDOM";
    case MemoryContext::kBlinkStyle:
      return "BlinkStyle";
    case MemoryContext::kBlinkParser:
      return "BlinkParser";
    case MemoryContext::kPlatformIPC:
      return "PlatformIPC";
    case MemoryContext::kPlatformStarboard:
      return "PlatformStarboard";
    case MemoryContext::kPlatformDevTools:
      return "PlatformDevTools";
    case MemoryContext::kBrowserMain:
      return "BrowserMain";
    case MemoryContext::kCount:
      return "Unknown";
  }
}

}  // namespace memory
}  // namespace base

extern "C" __attribute__((visibility("default"))) MAYBE_COBALT_WEAK void CobaltSetMemoryContextForThread(const char* name) {
  if (!name) return;
  std::string_view name_view(name);
  base::memory::MemoryContext context = base::memory::MemoryContext::kUnknown;
  
  if (name_view.find("Media") != std::string_view::npos || name_view.find("Audio") != std::string_view::npos || name_view.find("Video") != std::string_view::npos || name_view.find("FFmpeg") != std::string_view::npos || name_view.find("Decoder") != std::string_view::npos || name_view.find("Vpx") != std::string_view::npos) {
    context = base::memory::MemoryContext::kMedia;
  } else if (name_view.find("Network") != std::string_view::npos || name_view.find("IOThread") != std::string_view::npos || name_view.find("Socket") != std::string_view::npos) {
    context = base::memory::MemoryContext::kNetwork;
  } else if (name_view.find("Script") != std::string_view::npos || name_view.find("V8") != std::string_view::npos || name_view.find("JS") != std::string_view::npos || name_view.find("GC") != std::string_view::npos || name_view.find("Scavenger") != std::string_view::npos) {
    context = base::memory::MemoryContext::kScript;
  } else if (name_view.find("Graphics") != std::string_view::npos || name_view.find("Compositor") != std::string_view::npos || name_view.find("Skia") != std::string_view::npos || name_view.find("Ganesh") != std::string_view::npos || name_view.find("Raster") != std::string_view::npos || name_view.find("Gpu") != std::string_view::npos) {
    context = base::memory::MemoryContext::kGraphics;
  } else if (name_view.find("Browser") != std::string_view::npos || name_view.find("CrBrowserMain") != std::string_view::npos || name_view.find("CrRendererMain") != std::string_view::npos || name_view == "cobalt") {
    context = base::memory::MemoryContext::kBrowserMain;
  } else if (name_view.find("IPC") != std::string_view::npos || name_view.find("Mojo") != std::string_view::npos || name_view.find("MessagePump") != std::string_view::npos) {
    context = base::memory::MemoryContext::kPlatformIPC;
  }

  if (context != base::memory::MemoryContext::kUnknown) {
    base::memory::SetCurrentMemoryContext(context);
  }
}
