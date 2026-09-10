//
// Copyright 2020 Comcast Cable Communications Management, LLC
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
//
// SPDX-License-Identifier: Apache-2.0
//
// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#include <gst/gst.h>
#include <malloc.h>
#include <signal.h>
#include <sys/resource.h>

#include <cstring>

#include "starboard/configuration.h"
#include "starboard/event.h"
#include "starboard/shared/signal/crash_signals.h"
#include "starboard/shared/signal/suspend_signals.h"

#include "third_party/starboard/rdk/shared/application_rdk.h"
#include "third_party/starboard/rdk/shared/media/gst_media_utils.h"
#include <cstdio>

#if BUILDFLAG(IS_STARBOARD)
#include "starboard/common/command_line.h"
#include "starboard/common/paths.h"
#include "starboard/crashpad_wrapper/wrapper.h"
#include "starboard/elf_loader/elf_loader_constants.h"
#endif

namespace {

void debug_log_override(GstDebugCategory *category, GstDebugLevel level,
                        const gchar *file, const gchar *function, gint line,
                        GObject *object, GstDebugMessage *message,
                        gpointer data) {
  gchar *log_line = gst_debug_log_get_line(category, level, file, function,
                                           line, object, message);
  gint64 ts = g_get_monotonic_time();
  fprintf(stderr, "%.010" G_GINT64_FORMAT ".%.06" G_GINT64_FORMAT " %s",
          reinterpret_cast<gint64>(ts / G_USEC_PER_SEC),
          reinterpret_cast<gint64>(ts % G_USEC_PER_SEC), log_line);
  g_free(log_line);
}

} // namespace

namespace starboard {

static struct sigaction old_actions[2];

static void RequestStop(int signal_id) {
  SbSystemRequestStop(0);
}

static void InstallStopSignalHandlers() {
  struct sigaction action{};
  action.sa_handler = RequestStop;

  ::sigemptyset(&action.sa_mask);
  ::sigaction(SIGINT, &action, &old_actions[0]);
  ::sigaction(SIGTERM, &action, &old_actions[1]);
}

static void UninstallStopSignalHandlers() {
  ::sigaction(SIGINT, &old_actions[0], nullptr);
  ::sigaction(SIGTERM, &old_actions[1], nullptr);
}

}  // namespace starboard

int SbRunStarboardMain(int argc, char** argv, SbEventHandleCallback callback) {
  // Set M_ARENA_MAX to 2 to limit fragmentation without single-arena lock contention.
  mallopt(M_ARENA_MAX, 2);
  // Return free memory at the top of the heap to the OS at 32 KB (vs 128 KB default).
  mallopt(M_TRIM_THRESHOLD, 32768);
  // Retain 32 KB resident padding after trimming (vs 128 KB default).
  mallopt(M_TOP_PAD, 32768);
  // Route >= 64 KB allocations via direct mmap/munmap (disabling dynamic threshold
  // growth) to instantly return physical pages to the OS without VMA exhaustion.
  mallopt(M_MMAP_THRESHOLD, 65536);

  tzset();

  rlimit stack_size;
  getrlimit(RLIMIT_STACK, &stack_size);
  stack_size.rlim_cur = 2 * 1024 * 1024;
  setrlimit(RLIMIT_STACK, &stack_size);

  starboard::InstallCrashSignalHandlers();
  starboard::InstallSuspendSignalHandlers();
  starboard::InstallStopSignalHandlers();

  starboard::EnsureGstInit();

  if (const char *env = std::getenv("COBALT_OVERRIDE_GST_DEBUG_LOG");
      env && g_str_equal(env, "1")) {
    gst_debug_remove_log_function(gst_debug_log_default);
    gst_debug_add_log_function(debug_log_override, nullptr, nullptr);
  }

  starboard::ApplicationRdk application(callback);
  int result = application.Run(argc, argv);

  gst_deinit();

  starboard::UninstallStopSignalHandlers();
  starboard::UninstallSuspendSignalHandlers();
  starboard::UninstallCrashSignalHandlers();

  return result;
}
