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

#include "cobalt/app/cobalt_crash_reporter_client.h"

#include <string>

#include "base/no_destructor.h"
#include "build/buildflag.h"
#include "components/crash/core/app/crash_reporter_client.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/path_utils.h"  // Needed for GetCacheDirectory
#endif                                // BUILDFLAG(IS_ANDROID)
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "cobalt/version.h"

#if BUILDFLAG(IS_IOS_TVOS)
#include "base/command_line.h"
#include "cobalt/shell/common/shell_switches.h"
#endif  // BUILDFLAG(IS_IOS_TVOS)

namespace {
#if BUILDFLAG(IS_ANDROIDTV)
constexpr char kProductName[] = "Cobalt_ATV";
#else
constexpr char kProductName[] = "Cobalt";
#endif
constexpr char kCrashReportUrl[] = "https://clients2.google.com/cr/report";
}  // namespace

void CobaltCrashReporterClient::Create() {
  static base::NoDestructor<CobaltCrashReporterClient> crash_client;
  crash_reporter::SetCrashReporterClient(crash_client.get());
}

CobaltCrashReporterClient::CobaltCrashReporterClient() = default;

CobaltCrashReporterClient::~CobaltCrashReporterClient() = default;

void CobaltCrashReporterClient::GetProductInfo(ProductInfo* product_info) {
  product_info->product_name = kProductName;
  product_info->version = COBALT_VERSION;
  product_info->channel = "";  // Or appropriate channel
}

bool CobaltCrashReporterClient::GetCrashDumpLocation(
    base::FilePath* crash_dir) {
#if BUILDFLAG(IS_ANDROID)
  base::FilePath cache_dir;
  if (!base::android::GetCacheDirectory(&cache_dir)) {
    LOG(ERROR) << "Failed to get cache directory for Crashpad";
    return false;
  }
  *crash_dir = cache_dir.Append("crashpad");
  return true;
#elif BUILDFLAG(IS_IOS_TVOS)
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kCrashDumpsDir)) {
    *crash_dir = command_line->GetSwitchValuePath(switches::kCrashDumpsDir);
    return true;
  }

  base::FilePath cache_dir;
  if (!base::PathService::Get(base::DIR_CACHE, &cache_dir)) {
    LOG(ERROR) << "Failed to get cache directory for Crashpad";
    return false;
  }
  *crash_dir = cache_dir.Append("Crashpad");
  return true;
#else   // BUILDFLAG(IS_ANDROID)
  // [DUMMY] Provide a default path for non-Android platforms
  LOG(WARNING)
      << "GetCrashDumpLocation not fully implemented for this platform.";
  return false;
#endif  // BUILDFLAG(IS_ANDROID)
}

bool CobaltCrashReporterClient::IsRunningUnattended() {
  return false;
}

std::string CobaltCrashReporterClient::GetUploadUrl() {
  return kCrashReportUrl;
}

// Overrides the default behavior to explicitly enable stack sanitization.
// Setting *sanitize_stacks = true ensures that all stack memory in the
// minidump is overwritten with the 0x0DEFACED pattern.
// This aligns the native Android crashpad path with the
// behavior established in the Starboard crashpad wrapper for 3P platforms.
void CobaltCrashReporterClient::GetSanitizationInformation(
    const char* const** allowed_annotations,
    void** target_module,
    bool* sanitize_stacks) {
  if (allowed_annotations) {
    *allowed_annotations = nullptr;
  }
  if (target_module) {
    *target_module = nullptr;
  }
  if (sanitize_stacks) {
    *sanitize_stacks = true;
  }
}
