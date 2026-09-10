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

#ifndef COBALT_BROWSER_METRICS_COBALT_STABILITY_METRICS_HELPER_H_
#define COBALT_BROWSER_METRICS_COBALT_STABILITY_METRICS_HELPER_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/process/process_handle.h"
#include "build/build_config.h"
#include "build/buildflag.h"

namespace cobalt {

inline constexpr char kBrowserStabilityMetricsName[] =
    "BrowserStabilityMetrics";

// UMA histogram name for recording Android system exit reasons.
inline constexpr char kSystemExitReasonHistogram[] =
    "Cobalt.Stability.Android.SystemExitReason";

// Number of entries in the AndroidProcessExitReason enum.
inline constexpr int kAndroidExitReasonNumEntries = 18;

// Process exit reason enum value for normal exit (self) on Android.
// Corresponds to ExitReason.REASON_EXIT_SELF in
// org.chromium.components.crash.browser.ProcessExitReasonFromSystem.
inline constexpr int kAndroidExitReasonExitSelf = 5;

// Process exit reason enum value for low-memory kills on Android.
// Corresponds to ExitReason.REASON_LOW_MEMORY in
// org.chromium.components.crash.browser.ProcessExitReasonFromSystem and
// the "AndroidProcessExitReason" enum in tools/metrics/histograms/enums.xml.
inline constexpr int kAndroidExitReasonLowMemory = 7;

#if BUILDFLAG(IS_ANDROID)
// Queries Android system for the exit reasons of prior Cobalt browser sessions
// (identified via PMA files in the stability metrics directory) and records
// them to UMA. Safe to call on any thread; performs blocking file I/O and JNI.
// No-ops on Android versions earlier than Android R (API 30).
void RecordPriorSessionExitReasons();
#endif  // BUILDFLAG(IS_ANDROID)

// Returns true if the stability metrics histogram indicates that any prior
// session was killed by the system due to low memory. Thread-safe; queries the
// global StatisticsRecorder.
bool WasPriorSessionLowMemoryKilled();

// Extracts process IDs of prior sessions from persistent memory allocator
// (.pma) files located in |metrics_dir| matching |expected_allocator_name|.
// Ignores non-.pma files, files where ParseFilePath fails, files with
// mismatched allocator names, PIDs <= 0, and |current_pid|. Returned PIDs are
// deduplicated.
std::vector<base::ProcessId> ExtractPriorSessionPids(
    const base::FilePath& metrics_dir,
    const std::string& expected_allocator_name,
    base::ProcessId current_pid);

}  // namespace cobalt

#endif  // COBALT_BROWSER_METRICS_COBALT_STABILITY_METRICS_HELPER_H_
