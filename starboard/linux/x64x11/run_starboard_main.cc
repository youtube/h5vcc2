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

#include <malloc.h>
#include <time.h>

#include "starboard/configuration.h"
#include "starboard/crashpad_wrapper/wrapper.h"
#include "starboard/event.h"
#include "starboard/shared/signal/crash_signals.h"
#include "starboard/shared/signal/debug_signals.h"
#include "starboard/shared/signal/suspend_signals.h"
#include "starboard/shared/starboard/link_receiver.h"
#include "starboard/shared/x11/application_x11.h"
#if BUILDFLAG(IS_STARBOARD)
#include "starboard/common/command_line.h"
#include "starboard/common/paths.h"
#include "starboard/elf_loader/elf_loader_constants.h"
#endif
#include "starboard/shared/x11/application_x11.h"

int SbRunStarboardMain(int argc, char** argv, SbEventHandleCallback callback) {
  // Set M_ARENA_MAX to 2 to limit fragmentation without single-arena lock
  // contention.
  mallopt(M_ARENA_MAX, 2);
  // Return free memory at the top of the heap to the OS at 32 KB (vs 128 KB
  // default).
  mallopt(M_TRIM_THRESHOLD, 32768);
  // Retain 32 KB resident padding after trimming (vs 128 KB default).
  mallopt(M_TOP_PAD, 32768);
  // Route >= 64 KB allocations via direct mmap/munmap (disabling dynamic
  // threshold growth) to instantly return physical pages to the OS without VMA
  // exhaustion.
  mallopt(M_MMAP_THRESHOLD, 65536);

  tzset();
  starboard::InstallCrashSignalHandlers();
  starboard::InstallDebugSignalHandlers();
  starboard::InstallSuspendSignalHandlers();

#if SB_HAS_QUIRK(BACKTRACE_DLOPEN_BUG)
  // Call backtrace() once to work around potential
  // crash bugs in glibc, in dlopen()
  SbLogRawDumpStack(3);
#endif

  starboard::ApplicationX11 application(callback);

  starboard::LinkReceiver receiver(&application);
  int result = application.Run(argc, argv);

  starboard::UninstallSuspendSignalHandlers();
  starboard::UninstallDebugSignalHandlers();
  starboard::UninstallCrashSignalHandlers();

  return result;
}
