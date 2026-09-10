// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CONTROLLER_HIGHEST_PMF_REPORTER_H_
#define THIRD_PARTY_BLINK_RENDERER_CONTROLLER_HIGHEST_PMF_REPORTER_H_

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "third_party/blink/renderer/controller/controller_export.h"
#include "third_party/blink/renderer/controller/memory_usage_monitor.h"
#include "third_party/blink/renderer/platform/timer.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#if BUILDFLAG(IS_COBALT)
#include "base/cancelable_callback.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"
#endif

namespace base {
class SingleThreadTaskRunner;
class TickClock;
}

namespace blink {

// Reports the highest private memory footprint in [X mins after the first page
// navigation, Y mins after the first page navigation].
class CONTROLLER_EXPORT HighestPmfReporter
    : public MemoryUsageMonitor::Observer {
  USING_FAST_MALLOC(HighestPmfReporter);

 public:
  // Returns the shared instance.
  static void Initialize(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);
#if BUILDFLAG(IS_COBALT)
  static HighestPmfReporter* Instance();
  ~HighestPmfReporter() override;

  void OnProcessForegrounded();
  void OnProcessBackgrounded();
#endif

 private:
  explicit HighestPmfReporter(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  // The constructor for testing.
  HighestPmfReporter(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner_for_testing,
      const base::TickClock* clock);

  friend class MockHighestPmfReporter;

  // MemoryUsageMonitor::Observer:
  void OnMemoryPing(MemoryUsage) override;
  void OnReportMetrics();

  // Make the following methods virtual for testing.
  virtual bool FirstNavigationStarted();
  virtual void ReportMetrics();

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  raw_ptr<const base::TickClock> clock_;

  bool first_navigation_detected_ = false;
  double current_highest_pmf_ = 0.0;
  double peak_resident_bytes_at_current_highest_pmf_ = 0.0;
  unsigned webpage_counts_at_current_highest_pmf_ = 0;
  unsigned report_count_ = 0;
#if BUILDFLAG(IS_COBALT)
  static HighestPmfReporter* instance_;

  struct MetricInfo {
    base::TimeDelta time_to_report;
    WTF::String pmf_name;
    WTF::String pmf_foregrounded_name;
    WTF::String peak_rss_name;
    WTF::String peak_rss_foregrounded_name;
  };
  WTF::Vector<MetricInfo> metrics_;

  // True after the process has been backgrounded at least once. When false,
  // metrics are reported for the initial startup navigation.
  bool has_been_backgrounded_once_ = false;
  base::CancelableOnceClosure cancelable_report_task_;
#endif
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CONTROLLER_HIGHEST_PMF_REPORTER_H_
