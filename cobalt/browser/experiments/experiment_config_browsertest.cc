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

#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "cobalt/browser/constants/cobalt_experiment_names.h"
#include "cobalt/browser/constants/cobalt_pref_names.h"
#include "cobalt/browser/experiments/experiment_config_manager.h"
#include "cobalt/browser/global_features.h"
#include "cobalt/testing/browser_tests/browser/test_shell.h"
#include "cobalt/testing/browser_tests/content_browser_test.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/jni_android.h"
#include "cobalt/android/jni_headers/JavaSwitches_jni.h"
#endif

namespace cobalt {

class ExperimentConfigBrowserTest : public content::ContentBrowserTest {
 public:
  ExperimentConfigBrowserTest() = default;
  ~ExperimentConfigBrowserTest() override = default;
};

IN_PROC_BROWSER_TEST_F(ExperimentConfigBrowserTest,
                       ExperimentConfigManagerInitialized) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  auto* global_features = GlobalFeatures::GetInstance();
  ASSERT_NE(global_features, nullptr);

  auto* experiment_config_manager =
      global_features->experiment_config_manager();
  ASSERT_NE(experiment_config_manager, nullptr);

  ExperimentConfigType config_type =
      experiment_config_manager->GetExperimentConfigType();
  EXPECT_TRUE(config_type == ExperimentConfigType::kRegularConfig ||
              config_type == ExperimentConfigType::kSafeConfig ||
              config_type == ExperimentConfigType::kEmptyConfig);
}

IN_PROC_BROWSER_TEST_F(ExperimentConfigBrowserTest, StoreSafeConfigBehavior) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  auto* global_features = GlobalFeatures::GetInstance();
  ASSERT_NE(global_features, nullptr);

  auto* experiment_config_manager =
      global_features->experiment_config_manager();
  ASSERT_NE(experiment_config_manager, nullptr);

  ExperimentConfigType config_type =
      experiment_config_manager->GetExperimentConfigType();
  experiment_config_manager->StoreSafeConfig();

  // StoreSafeConfig only executes when config type is kRegularConfig.
  if (config_type == ExperimentConfigType::kRegularConfig) {
    EXPECT_TRUE(
        experiment_config_manager->has_called_store_safe_config_for_testing());
  } else {
    EXPECT_FALSE(
        experiment_config_manager->has_called_store_safe_config_for_testing());
  }
}

#if (BUILDFLAG(IS_STARBOARD) || BUILDFLAG(IS_APPLE)) && \
    !BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_ANDROID)
#define MAYBE_NavigationWithExperimentConfig \
  DISABLED_NavigationWithExperimentConfig
#else
#define MAYBE_NavigationWithExperimentConfig NavigationWithExperimentConfig
#endif

IN_PROC_BROWSER_TEST_F(ExperimentConfigBrowserTest,
                       MAYBE_NavigationWithExperimentConfig) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(NavigateToURL(shell()->web_contents(), url));

  // Verify page loads and JavaScript executes properly.
  EXPECT_EQ(
      "This page has no title.",
      content::EvalJs(shell()->web_contents(), "document.body.innerText"));
}

IN_PROC_BROWSER_TEST_F(ExperimentConfigBrowserTest,
                       PrefFilePathEquivalenceSanityCheck) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath cache_dir;
  ASSERT_TRUE(base::PathService::Get(base::DIR_CACHE, &cache_dir));

  base::FilePath exp_macro =
      cache_dir.Append(FILE_PATH_LITERAL("Experiment Config"));
  base::FilePath exp_ascii = cache_dir.AppendASCII(kExperimentConfigFilename);
  EXPECT_EQ(exp_macro, exp_ascii);
  EXPECT_EQ(exp_macro.value(), exp_ascii.value());

  base::FilePath metrics_macro =
      cache_dir.Append(FILE_PATH_LITERAL("Metrics Config"));
  base::FilePath metrics_ascii = cache_dir.AppendASCII(kMetricsConfigFilename);
  EXPECT_EQ(metrics_macro, metrics_ascii);
  EXPECT_EQ(metrics_macro.value(), metrics_ascii.value());
}

#if BUILDFLAG(IS_ANDROID)
IN_PROC_BROWSER_TEST_F(ExperimentConfigBrowserTest,
                       NativeFileCreationReadByJava) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath cache_dir;
  ASSERT_TRUE(base::PathService::Get(base::DIR_CACHE, &cache_dir));

  base::FilePath metrics_file = cache_dir.AppendASCII(kMetricsConfigFilename);
  base::FilePath exp_file = cache_dir.AppendASCII(kExperimentConfigFilename);
  base::FilePath beacon_file = cache_dir.AppendASCII(kVariationsBeaconFilename);

  // Clean up any existing files first.
  base::DeleteFile(metrics_file);
  base::DeleteFile(exp_file);
  base::DeleteFile(beacon_file);

  JNIEnv* env = base::android::AttachCurrentThread();

  // 1. When no files exist, Java should return true (safe default).
  EXPECT_TRUE(Java_JavaSwitches_shouldApplyExperimentConfigs(env));

  // 2. Write low crash streak in native -> Java should return true.
  std::string low_streak_json =
      base::StringPrintf("{\"%s\": 1}", kVariationsCrashStreak);
  ASSERT_TRUE(base::WriteFile(metrics_file, low_streak_json));
  EXPECT_TRUE(Java_JavaSwitches_shouldApplyExperimentConfigs(env));

  // 3. Write crash streak >= default threshold (4) -> Java returns false.
  std::string high_streak_json =
      base::StringPrintf("{\"%s\": 4}", kVariationsCrashStreak);
  ASSERT_TRUE(base::WriteFile(metrics_file, high_streak_json));
  EXPECT_FALSE(Java_JavaSwitches_shouldApplyExperimentConfigs(env));

  // 4. Write custom threshold 2 in Experiment Config -> streak of 2 returns
  // false.
  std::string exp_config_json =
      base::StringPrintf("{\"%s\": {\"%s\": 2}}", kFinchParameters,
                         kCrashStreakEmptyConfigThreshold);
  ASSERT_TRUE(base::WriteFile(exp_file, exp_config_json));
  std::string custom_streak_json =
      base::StringPrintf("{\"%s\": 2}", kVariationsCrashStreak);
  ASSERT_TRUE(base::WriteFile(metrics_file, custom_streak_json));
  EXPECT_FALSE(Java_JavaSwitches_shouldApplyExperimentConfigs(env));

  // 5. Variations beacon with clean exit: streak 3 < threshold 4 -> returns
  // true.
  base::DeleteFile(metrics_file);
  base::DeleteFile(exp_file);
  std::string beacon_clean_json =
      base::StringPrintf("{\"%s\": 3, \"%s\": true}", kVariationsCrashStreak,
                         kStabilityExitedCleanly);
  ASSERT_TRUE(base::WriteFile(beacon_file, beacon_clean_json));
  EXPECT_TRUE(Java_JavaSwitches_shouldApplyExperimentConfigs(env));

  // 6. Variations beacon with dirty exit: streak 3 + 1 pending = 4 >= threshold
  // 4 -> returns false.
  std::string beacon_dirty_json =
      base::StringPrintf("{\"%s\": 3, \"%s\": false}", kVariationsCrashStreak,
                         kStabilityExitedCleanly);
  ASSERT_TRUE(base::WriteFile(beacon_file, beacon_dirty_json));
  EXPECT_FALSE(Java_JavaSwitches_shouldApplyExperimentConfigs(env));

  // 7. Clean up created files.
  base::DeleteFile(metrics_file);
  base::DeleteFile(exp_file);
  base::DeleteFile(beacon_file);
  EXPECT_TRUE(Java_JavaSwitches_shouldApplyExperimentConfigs(env));
}
#endif

}  // namespace cobalt
