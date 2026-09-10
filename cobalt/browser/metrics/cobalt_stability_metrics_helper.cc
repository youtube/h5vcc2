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
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/process/process_handle.h"
#include "base/time/time.h"

namespace cobalt {

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
