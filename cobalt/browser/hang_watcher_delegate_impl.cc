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

#include "cobalt/browser/hang_watcher_delegate_impl.h"

#include <cstdint>
#include <optional>
#include <variant>

#include "base/feature_list.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "cobalt/browser/features.h"
#include "cobalt/browser/global_features.h"
namespace cobalt {
namespace browser {

// static
void CobaltHangWatcherDelegate::Initialize() {
  static base::NoDestructor<CobaltHangWatcherDelegate> instance;
  base::HangWatcher::SetDelegate(instance.get());
}

bool CobaltHangWatcherDelegate::IsHangReportingEnabled() {
  // First, check 'GlobalFeatures', which is a process-wide settings dictionary
  // typically populated by the embedder via the JS H5VCC API.
  // We prioritize this to allow embedders to enable hang reporting
  // until the proper Finch bridge is fully functional. If the setting isn't
  // explicitly provided, we fallback to the standard Chromium 'FeatureList'
  // which is backed by Finch.
  if (auto* global_features = GlobalFeatures::GetInstance()) {
    auto val_opt = global_features->GetSetting("EnableHangReporting");
    if (val_opt.has_value()) {
      if (const auto* val_int = std::get_if<int64_t>(&val_opt.value())) {
        bool enabled = *val_int != 0;
        DLOG(INFO) << "CobaltHangWatcherDelegate: EnableHangReporting: "
                   << enabled;
        return enabled;
      }
    }
  }
  DLOG(INFO) << "CobaltHangWatcherDelegate: EnableHangReporting: using Finch";

  return base::FeatureList::IsEnabled(cobalt::features::kHangReporting);
}

std::optional<base::TimeDelta> CobaltHangWatcherDelegate::GetHangWatchTime() {
  if (auto* global_features = GlobalFeatures::GetInstance()) {
    auto val_opt = global_features->GetSetting("HangWatchTimeSeconds");
    if (val_opt.has_value()) {
      if (const auto* val = std::get_if<int64_t>(&val_opt.value())) {
        if (*val > 0) {
          return base::Seconds(*val);
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<base::TimeDelta>
CobaltHangWatcherDelegate::GetHangWatchMonitoringPeriod() {
  if (auto* global_features = GlobalFeatures::GetInstance()) {
    auto val_opt =
        global_features->GetSetting("HangWatchMonitoringPeriodSeconds");
    if (val_opt.has_value()) {
      if (const auto* val = std::get_if<int64_t>(&val_opt.value())) {
        if (*val > 0) {
          return base::Seconds(*val);
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<bool> CobaltHangWatcherDelegate::IsThreadDumpingEnabled(
    base::HangWatcher::ThreadType thread_type) {
  if (auto* global_features = GlobalFeatures::GetInstance()) {
    std::string key;
    switch (thread_type) {
      case base::HangWatcher::ThreadType::kMainThread:
        key = "EnableHangWatchMainThreadDump";
        break;
      case base::HangWatcher::ThreadType::kIOThread:
        key = "EnableHangWatchIOThreadDump";
        break;
      case base::HangWatcher::ThreadType::kThreadPoolThread:
        key = "EnableHangWatchThreadPoolDump";
        break;
      case base::HangWatcher::ThreadType::kRendererThread:
        key = "EnableHangWatchRendererThreadDump";
        break;
      default:
        return std::nullopt;
    }
    if (!key.empty()) {
      auto val_opt = global_features->GetSetting(key);
      if (val_opt.has_value()) {
        if (const auto* val_int = std::get_if<int64_t>(&val_opt.value())) {
          return *val_int != 0;
        }
      }
    }
  }
  return std::nullopt;
}

}  // namespace browser
}  // namespace cobalt
