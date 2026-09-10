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

#include "cobalt/browser/metrics/cobalt_stability_metrics_helper.h"

#include <set>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/metrics/statistics_recorder.h"
#include "base/process/process_handle.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/buildflag.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/build_info.h"
#include "base/base_paths.h"
#include "base/path_service.h"
#include "components/crash/content/browser/process_exit_reason_from_system_android.h"
#endif

namespace cobalt {

#if BUILDFLAG(IS_ANDROID)
void RecordPriorSessionExitReasons() {
  if (base::android::BuildInfo::GetInstance()->sdk_int() <
      base::android::SDK_VERSION_R) {
    return;
  }
  base::FilePath base_dir;
  if (!base::PathService::Get(base::DIR_ANDROID_APP_DATA, &base_dir)) {
    return;
  }
  base::FilePath metrics_dir =
      base_dir.AppendASCII(kBrowserStabilityMetricsName);
  for (base::ProcessId pid :
       ExtractPriorSessionPids(metrics_dir, kBrowserStabilityMetricsName,
                               base::GetCurrentProcId())) {
    crash_reporter::ProcessExitReasonFromSystem::RecordExitReasonToUma(
        pid, kSystemExitReasonHistogram);
  }
}
#endif  // BUILDFLAG(IS_ANDROID)

bool WasPriorSessionLowMemoryKilled() {
  base::HistogramBase* histogram =
      base::StatisticsRecorder::FindHistogram(kSystemExitReasonHistogram);
  if (!histogram) {
    return false;
  }
  auto samples = histogram->SnapshotSamples();
  if (!samples) {
    return false;
  }
  return samples->GetCount(kAndroidExitReasonLowMemory) > 0;
}

std::vector<base::ProcessId> ExtractPriorSessionPids(
    const base::FilePath& metrics_dir,
    const std::string& expected_allocator_name,
    base::ProcessId current_pid) {
  std::vector<base::ProcessId> pids;
  std::set<base::ProcessId> seen_pids;

  base::FileEnumerator file_iter(metrics_dir, /*recursive=*/false,
                                 base::FileEnumerator::FILES);
  for (base::FilePath file = file_iter.Next(); !file.empty();
       file = file_iter.Next()) {
    if (file.Extension() != FILE_PATH_LITERAL(".pma")) {
      continue;
    }

    std::string name;
    base::Time stamp;
    base::ProcessId previous_pid;
    if (!base::GlobalHistogramAllocator::ParseFilePath(file, &name, &stamp,
                                                       &previous_pid)) {
      continue;
    }

    if (name != expected_allocator_name) {
      continue;
    }

    if (previous_pid <= 0 || previous_pid == current_pid) {
      continue;
    }

    if (seen_pids.insert(previous_pid).second) {
      pids.push_back(previous_pid);
    }
  }

  return pids;
}

}  // namespace cobalt
