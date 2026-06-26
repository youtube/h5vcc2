// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

#ifndef COBALT_BROWSER_HANG_WATCHER_DELEGATE_IMPL_H_
#define COBALT_BROWSER_HANG_WATCHER_DELEGATE_IMPL_H_

#include "base/threading/hang_watcher.h"

namespace cobalt {
namespace browser {

class CobaltHangWatcherDelegate : public base::HangWatcher::Delegate {
 public:
  CobaltHangWatcherDelegate() = default;
  ~CobaltHangWatcherDelegate() override = default;

  static void Initialize();

  bool IsHangReportingEnabled() override;
  std::optional<base::TimeDelta> GetHangWatchTime() override;
  std::optional<base::TimeDelta> GetHangWatchMonitoringPeriod() override;
  std::optional<bool> IsThreadDumpingEnabled(
      base::HangWatcher::ThreadType thread_type) override;
};

}  // namespace browser
}  // namespace cobalt

#endif  // COBALT_BROWSER_HANG_WATCHER_DELEGATE_IMPL_H_
