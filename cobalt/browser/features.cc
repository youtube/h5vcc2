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

#include "cobalt/browser/features.h"

#include "cobalt/build/configs/buildflags.h"

namespace cobalt {
namespace features {

// Enables the variations config expiration check.
BASE_FEATURE(kExperimentConfigExpiration,
             "ExperimentConfigExpiration",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Test feature, does not enable any features.
BASE_FEATURE(kTestFinchFeature,
             "TestFinchFeature",
             base::FEATURE_DISABLED_BY_DEFAULT);

<<<<<<< HEAD
const base::FeatureParam<std::string> kTestFinchFeatureParam{
    &kTestFinchFeature, "TestFinchFeatureParam", ""};
=======
// Test conflict marker for AI Rebase Agent verification.
const base::FeatureParam<std::string> kTestFinchFeatureParam{
    &kTestFinchFeature, "TestFinchFeatureParam", ""};
>>>>>>> upstream/main

BASE_FEATURE(kEnableUserAgentFinchToken,
             "EnableUserAgentFinchToken",
             base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<std::string> kUserAgentFinchTokenParam{
    &kEnableUserAgentFinchToken, "token", ""};

BASE_FEATURE(kHangReporting,
             "HangReporting",
             base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<int> kHangWatchTimeSeconds{&kHangReporting,
                                                    "HangWatchTimeSeconds", 10};

const base::FeatureParam<int> kHangWatchMonitoringPeriodSeconds{
    &kHangReporting, "HangWatchMonitoringPeriodSeconds", 10};

BASE_FEATURE(kHangWatchMainThreadDump,
             "HangWatchMainThreadDump",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kHangWatchIOThreadDump,
             "HangWatchIOThreadDump",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kHangWatchThreadPoolDump,
             "HangWatchThreadPoolDump",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kHangWatchRendererThreadDump,
             "HangWatchRendererThreadDump",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kHangWatcherLongHangDetection,
             "HangWatcherLongHangDetection",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kHangWatcherLongHangKill,
             "HangWatcherLongHangKill",
             base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<int> kLongHangTimeoutSeconds{
    &kHangWatcherLongHangDetection, "LongHangTimeoutSeconds", 20};

BASE_FEATURE(kCobaltMetricsIntervalFeature,
             "CobaltMetricsInterval",
             base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<int> kCpuMetricsIntervalParam{
    &kCobaltMetricsIntervalFeature, "cpu-metrics-interval", 300};

const base::FeatureParam<int> kMemoryMetricsIntervalParam{
    &kCobaltMetricsIntervalFeature, "memory-metrics-interval", 300};

BASE_FEATURE(kCobaltMemoryAttributionManager,
             "CobaltMemoryAttributionManager",
             base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<int> kCobaltMemoryAttributionReportIntervalParam{
    &kCobaltMemoryAttributionManager, "report-interval", 600};

// Enables custom memory buffer size for in-memory updates.
BASE_FEATURE(kInMemoryUpdatesMemoryBuffer,
             "InMemoryUpdatesMemoryBuffer",
             base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<int> kInMemoryUpdatesMemoryBufferParam{
    &kInMemoryUpdatesMemoryBuffer, "memory_buffer_bytes", 35 * 1024 * 1024};

BASE_FEATURE(kDisableSplashScreen,
             "DisableSplashScreen",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_IOS_TVOS)
BASE_FEATURE(kForceVideoSplashScreen,
             "ForceVideoSplashScreen",
             base::FEATURE_ENABLED_BY_DEFAULT);
#else   // BUILDFLAG(IS_IOS_TVOS)
BASE_FEATURE(kForceVideoSplashScreen,
             "ForceVideoSplashScreen",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_IOS_TVOS)

#if BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kEnablePictureInPicture,
             "PictureInPicture",
             base::FEATURE_ENABLED_BY_DEFAULT);
#else   // BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kEnablePictureInPicture,
             "PictureInPicture",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_ANDROID)

BASE_FEATURE(kCobaltNativeMemoryAblation,
             "CobaltNativeMemoryAblation",
             base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<int> kMemoryAblationSizeMBParam{
    &kCobaltNativeMemoryAblation, "ablation_size_mb", 0};

const base::FeatureParam<base::TimeDelta> kMemoryAblationDelayParam{
    &kCobaltNativeMemoryAblation, "ablation_delay", base::Seconds(0)};

}  // namespace features
}  // namespace cobalt
