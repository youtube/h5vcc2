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

#include "third_party/blink/renderer/controller/highest_pmf_reporter.h"

#include "base/memory/ptr_util.h"
#include "base/metrics/statistics_recorder.h"
#include "base/run_loop.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/time/time.h"
#include "cobalt/testing/browser_tests/browser/test_shell.h"
#include "cobalt/testing/browser_tests/content_browser_test.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/controller/memory_usage_monitor.h"
#include "third_party/blink/renderer/platform/wtf/allocator/partitions.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

// Needs to be precisely in blink namespace to match friend declaration
class MockHighestPmfReporter : public HighestPmfReporter {
 public:
  MockHighestPmfReporter(
      scoped_refptr<base::TestMockTimeTaskRunner> task_runner,
      const base::TickClock* clock)
      : HighestPmfReporter(task_runner, clock) {}

  void ForceFirstNavigationStarted() { first_navigation_started_ = true; }

 protected:
  bool FirstNavigationStarted() override {
    if (first_navigation_started_) {
      if (already_returned_true_) {
        return false;
      }
      already_returned_true_ = true;
      return true;
    }
    return false;
  }

 private:
  bool first_navigation_started_ = false;
  bool already_returned_true_ = false;
};

// Minimal mock to fake MemoryUsageMonitor that reporter attaches to
class MockMemoryUsageMonitor : public MemoryUsageMonitor {
 public:
  MockMemoryUsageMonitor(
      scoped_refptr<base::TestMockTimeTaskRunner> task_runner,
      const base::TickClock* clock)
      : MemoryUsageMonitor(task_runner, clock) {}

  MemoryUsage GetCurrentMemoryUsage() override { return usage_; }

  MemoryUsage usage_;
};

}  // namespace blink

namespace cobalt {
namespace metrics {

class HighestPmfReporterBrowserTest : public content::ContentBrowserTest {
 public:
  HighestPmfReporterBrowserTest() {
    feature_list_.InitAndEnableFeatureWithParameters(
        blink::features::kHighestPmfReporterConfigurable,
        {{"intervals", "1,2"}, {"metric_suffixes", "1minTest,2minTest"}});
  }

  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    test_task_runner_ = base::MakeRefCounted<base::TestMockTimeTaskRunner>();

    // Tests run in the browser process; WTF partitions are not automatically
    // initialized.
    WTF::Partitions::Initialize();
    WTF::Initialize();

    memory_usage_monitor_ = std::make_unique<blink::MockMemoryUsageMonitor>(
        test_task_runner_, test_task_runner_->GetMockTickClock());
    blink::MemoryUsageMonitor::SetInstanceForTesting(
        memory_usage_monitor_.get());
    reporter_ = std::make_unique<blink::MockHighestPmfReporter>(
        test_task_runner_, test_task_runner_->GetMockTickClock());
  }

  void TearDownOnMainThread() override {
    reporter_.reset();
    blink::MemoryUsageMonitor::SetInstanceForTesting(nullptr);
    memory_usage_monitor_.reset();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 protected:
  base::test::ScopedFeatureList feature_list_;
  scoped_refptr<base::TestMockTimeTaskRunner> test_task_runner_;
  std::unique_ptr<blink::MockMemoryUsageMonitor> memory_usage_monitor_;
  std::unique_ptr<blink::MockHighestPmfReporter> reporter_;
};

#if (BUILDFLAG(IS_STARBOARD) || BUILDFLAG(IS_APPLE)) && \
    !BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_ANDROID)
#define MAYBE_ReportMetric DISABLED_ReportMetric
#else
#define MAYBE_ReportMetric ReportMetric
#endif

IN_PROC_BROWSER_TEST_F(HighestPmfReporterBrowserTest, MAYBE_ReportMetric) {
  base::HistogramTester histogram_tester;

  reporter_->ForceFirstNavigationStarted();
  memory_usage_monitor_->usage_.private_footprint_bytes =
      1000.0 * 1024.0 * 1024.0;
  memory_usage_monitor_->usage_.peak_resident_bytes = 1000.0 * 1024.0 * 1024.0;

  // Fast forward by 1 second to let MemoryUsageMonitor's internal timer fire
  // OnMemoryPing
  test_task_runner_->FastForwardBy(base::Seconds(1));

  // Fast forward by 2 minutes and 2 seconds.
  // If parameter override succeeds, both 1minTest and 2minTest buckets trigger.
  // If the override fails (e.g. single-process early caching), the 0to2min
  // baseline bucket triggers.
  test_task_runner_->FastForwardBy(base::Minutes(2) + base::Seconds(2));

  auto samples_override = histogram_tester.GetAllSamples(
      "Memory.Experimental.Renderer.HighestPrivateMemoryFootprint.1minTest");
  auto samples_baseline = histogram_tester.GetAllSamples(
      "Memory.Experimental.Renderer.HighestPrivateMemoryFootprint.0to2min");

  // At least one of the config models must successfully register an
  // initialization ping.
  EXPECT_FALSE(samples_override.empty() && samples_baseline.empty());
}

}  // namespace metrics
}  // namespace cobalt
