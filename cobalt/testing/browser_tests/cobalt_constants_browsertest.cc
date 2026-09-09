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

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "cobalt/browser/constants/cobalt_experiment_names.h"
#include "cobalt/browser/constants/cobalt_pref_names.h"
#include "cobalt/testing/browser_tests/content_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "cobalt/android/jni_headers/CobaltPrefNamesTestHelper_jni.h"
#endif

namespace cobalt {

class CobaltConstantsBrowserTest : public content::ContentBrowserTest {
#if BUILDFLAG(IS_ANDROID)
 protected:
  void TearDown() override {
    base::ScopedAllowBlockingForTesting allow_blocking;
    base::FilePath native_cache_dir;
    if (base::PathService::Get(base::DIR_CACHE, &native_cache_dir)) {
      base::DeleteFile(native_cache_dir.AppendASCII(kMetricsConfigFilename));
      base::DeleteFile(native_cache_dir.AppendASCII(kExperimentConfigFilename));
      base::DeleteFile(native_cache_dir.AppendASCII(kVariationsBeaconFilename));
    }
    content::ContentBrowserTest::TearDown();
  }
#endif
};

#if BUILDFLAG(IS_ANDROID)
IN_PROC_BROWSER_TEST_F(CobaltConstantsBrowserTest,
                       NativeWriteReadByJavaMatchesExactly) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  JNIEnv* env = base::android::AttachCurrentThread();

  // 1. Verify that Java cache directory path and C++ base::DIR_CACHE are
  // identical.
  base::FilePath native_cache_dir;
  ASSERT_TRUE(base::PathService::Get(base::DIR_CACHE, &native_cache_dir));

  base::android::ScopedJavaLocalRef<jstring> java_cache_dir_str =
      Java_CobaltPrefNamesTestHelper_getCacheDirAbsolutePath(env);
  std::string java_cache_dir =
      base::android::ConvertJavaStringToUTF8(env, java_cache_dir_str);

  EXPECT_EQ(native_cache_dir.value(), java_cache_dir);

  base::FilePath metrics_file =
      native_cache_dir.AppendASCII(kMetricsConfigFilename);
  base::FilePath experiment_file =
      native_cache_dir.AppendASCII(kExperimentConfigFilename);
  base::FilePath beacon_file =
      native_cache_dir.AppendASCII(kVariationsBeaconFilename);

  // Clean up any existing files first to avoid test pollution.
  base::DeleteFile(metrics_file);
  base::DeleteFile(experiment_file);
  base::DeleteFile(beacon_file);

  // 2. Native writes to kMetricsConfigFilename ("Metrics Config") in
  // base::DIR_CACHE.
  const std::string kMetricsPayload = "{\"variations_crash_streak\": 3}";
  ASSERT_TRUE(base::WriteFile(metrics_file, kMetricsPayload));

  // 3. Java reads the file using Java's
  // CobaltPrefNames.METRICS_CONFIG_FILENAME.
  base::android::ScopedJavaLocalRef<jstring> metrics_filename_java =
      Java_CobaltPrefNamesTestHelper_getMetricsConfigFilename(env);
  base::android::ScopedJavaLocalRef<jstring> read_metrics_content =
      Java_CobaltPrefNamesTestHelper_readCacheFile(env, metrics_filename_java);
  ASSERT_TRUE(read_metrics_content);

  std::string read_metrics_str =
      base::android::ConvertJavaStringToUTF8(env, read_metrics_content);
  EXPECT_EQ(kMetricsPayload, read_metrics_str);

  // 4. Native writes to kExperimentConfigFilename ("Experiment Config") in
  // base::DIR_CACHE.
  const std::string kExperimentPayload =
      "{\"finch_parameters\": {\"crash_streak_empty_config_threshold\": 5}}";
  ASSERT_TRUE(base::WriteFile(experiment_file, kExperimentPayload));

  // 5. Java reads the file using Java's
  // CobaltPrefNames.EXPERIMENT_CONFIG_FILENAME.
  base::android::ScopedJavaLocalRef<jstring> experiment_filename_java =
      Java_CobaltPrefNamesTestHelper_getExperimentConfigFilename(env);
  base::android::ScopedJavaLocalRef<jstring> read_experiment_content =
      Java_CobaltPrefNamesTestHelper_readCacheFile(env,
                                                   experiment_filename_java);
  ASSERT_TRUE(read_experiment_content);

  std::string read_experiment_str =
      base::android::ConvertJavaStringToUTF8(env, read_experiment_content);
  EXPECT_EQ(kExperimentPayload, read_experiment_str);

  // 6. Native writes to kVariationsBeaconFilename ("Variations") in
  // base::DIR_CACHE.
  const std::string kBeaconPayload =
      "{\"variations_crash_streak\": 2, "
      "\"user_experience_metrics.stability.exited_cleanly\": false}";
  ASSERT_TRUE(base::WriteFile(beacon_file, kBeaconPayload));

  // 7. Java reads the file using Java's
  // CobaltPrefNames.VARIATIONS_BEACON_FILENAME.
  base::android::ScopedJavaLocalRef<jstring> beacon_filename_java =
      Java_CobaltPrefNamesTestHelper_getVariationsBeaconFilename(env);
  base::android::ScopedJavaLocalRef<jstring> read_beacon_content =
      Java_CobaltPrefNamesTestHelper_readCacheFile(env, beacon_filename_java);
  ASSERT_TRUE(read_beacon_content);

  std::string read_beacon_str =
      base::android::ConvertJavaStringToUTF8(env, read_beacon_content);
  EXPECT_EQ(kBeaconPayload, read_beacon_str);
}
#endif  // BUILDFLAG(IS_ANDROID)

}  // namespace cobalt
