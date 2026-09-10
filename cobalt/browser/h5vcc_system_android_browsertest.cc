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

#include "base/android/build_info.h"
#include "base/android/jni_android.h"
#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/metrics/statistics_recorder.h"
#include "base/path_service.h"
#include "base/process/process_handle.h"
#include "base/time/time.h"
#include "cobalt/browser/metrics/cobalt_stability_metrics_helper.h"
#include "cobalt/testing/browser_tests/browser/test_shell.h"
#include "cobalt/testing/browser_tests/content_browser_test.h"
#include "cobalt/testing/browser_tests/content_browsertests_jni_headers/MockProcessExitReasonHelper_jni.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace cobalt {

class H5vccSystemAndroidBrowserTest : public content::ContentBrowserTest {
 public:
  H5vccSystemAndroidBrowserTest() = default;
  ~H5vccSystemAndroidBrowserTest() override = default;

  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    base::StatisticsRecorder::ForgetHistogramForTesting(
        kSystemExitReasonHistogram);
  }

  void TearDownOnMainThread() override {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_MockProcessExitReasonHelper_resetForTesting(env);
    base::StatisticsRecorder::ForgetHistogramForTesting(
        kSystemExitReasonHistogram);
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 protected:
  void SimulatePriorSessionExit(base::ProcessId pid, int exit_reason) {
    base::FilePath base_dir;
    ASSERT_TRUE(base::PathService::Get(base::DIR_ANDROID_APP_DATA, &base_dir));
    base::FilePath metrics_dir =
        base_dir.AppendASCII(kBrowserStabilityMetricsName);
    ASSERT_TRUE(base::CreateDirectory(metrics_dir));

    base::FilePath pma_path =
        base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
            metrics_dir, kBrowserStabilityMetricsName, base::Time::Now(), pid);
    ASSERT_TRUE(base::WriteFile(pma_path, ""));

    JNIEnv* env = base::android::AttachCurrentThread();
    Java_MockProcessExitReasonHelper_setMockExitReasonForTesting(
        env, static_cast<jint>(pid), exit_reason);

    cobalt::RecordPriorSessionExitReasons();
    base::DeleteFile(pma_path);
  }

  bool QueryWasLowMemoryKilledFromJs() {
    return content::EvalJs(
               shell()->web_contents(),
               "(async () => await window.h5vcc.system.wasLowMemoryKilled())()")
        .ExtractBool();
  }
};

IN_PROC_BROWSER_TEST_F(H5vccSystemAndroidBrowserTest,
                       VerifyNonLmkExitReasonResolvesFalse) {
  if (base::android::BuildInfo::GetInstance()->sdk_int() <
      base::android::SDK_VERSION_R) {
    GTEST_SKIP()
        << "Historical process exit reasons are only available on Android R+.";
  }

  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(NavigateToURL(shell()->web_contents(), url));

  EXPECT_FALSE(QueryWasLowMemoryKilledFromJs());

  // Simulate a prior session terminated due to REASON_EXIT_SELF (reason 5).
  SimulatePriorSessionExit(/*pid=*/9991, /*REASON_EXIT_SELF=*/5);

  EXPECT_FALSE(QueryWasLowMemoryKilledFromJs());
}

IN_PROC_BROWSER_TEST_F(H5vccSystemAndroidBrowserTest,
                       VerifyLowMemoryKillExitReasonResolvesTrue) {
  if (base::android::BuildInfo::GetInstance()->sdk_int() <
      base::android::SDK_VERSION_R) {
    GTEST_SKIP()
        << "Historical process exit reasons are only available on Android R+.";
  }

  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(NavigateToURL(shell()->web_contents(), url));

  EXPECT_FALSE(QueryWasLowMemoryKilledFromJs());

  // Simulate a prior session terminated due to REASON_LOW_MEMORY (reason 7).
  SimulatePriorSessionExit(/*pid=*/9992, /*REASON_LOW_MEMORY=*/7);

  EXPECT_TRUE(QueryWasLowMemoryKilledFromJs());
}

}  // namespace cobalt
