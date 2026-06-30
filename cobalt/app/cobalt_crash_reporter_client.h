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

#ifndef COBALT_APP_COBALT_CRASH_REPORTER_CLIENT_H_
#define COBALT_APP_COBALT_CRASH_REPORTER_CLIENT_H_

#include <string>

#include "base/files/file_path.h"
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "client/crash_report_database.h"
#include "components/crash/core/app/crash_reporter_client.h"

class CobaltCrashReporterClient : public crash_reporter::CrashReporterClient {
 public:
  static void Create();

  CobaltCrashReporterClient(const CobaltCrashReporterClient&) = delete;
  CobaltCrashReporterClient& operator=(const CobaltCrashReporterClient&) =
      delete;

  void GetProductInfo(ProductInfo* product_info) override;

  bool GetCrashDumpLocation(base::FilePath* crash_dir) override;
  bool IsRunningUnattended() override;

  std::string GetUploadUrl() override;

  void GetSanitizationInformation(const char* const** allowed_annotations,
                                  void** target_module,
                                  bool* sanitize_stacks) override;

 private:
  friend class base::NoDestructor<CobaltCrashReporterClient>;

  CobaltCrashReporterClient();
  ~CobaltCrashReporterClient() override;
};

#endif  // COBALT_APP_COBALT_CRASH_REPORTER_CLIENT_H_
