// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/controller/highest_pmf_reporter.h"

#include <limits>
#include "base/metrics/histogram_functions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/task_runner.h"
#include "base/time/default_tick_clock.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/page/page.h"

#include "build/build_config.h"
#include "build/buildflag.h"

#if BUILDFLAG(IS_COBALT)
#include <array>
#include <string>

#include "base/feature_list.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#endif

namespace blink {

namespace {

#if BUILDFLAG(IS_COBALT)
constexpr size_t kBaselineReportCount = 4;

constexpr char kMetricBasePrefix[] = "Memory.Experimental.Renderer.";
constexpr char kHighestPmfMetricName[] = "HighestPrivateMemoryFootprint.";
constexpr char kHighestPmfForegroundedMetricName[] =
    "HighestPrivateMemoryFootprintWhenForegrounded.";
constexpr char kPeakResidentSetMetricName[] =
    "PeakResidentSet.AtHighestPrivateMemoryFootprint.";
constexpr char kPeakResidentSetForegroundedMetricName[] =
    "PeakResidentSet.AtHighestPrivateMemoryFootprintWhenForegrounded.";

constexpr std::array<const char*, kBaselineReportCount>
    kBaselineMetricSuffixes = {"0to2min", "2to4min", "4to8min", "8to16min"};

constexpr std::array<base::TimeDelta, kBaselineReportCount>
    kBaselineTimeToReport = {base::Minutes(2), base::Minutes(4),
                             base::Minutes(8), base::Minutes(16)};

#else
constexpr size_t kMaxReportCount = 4;

constexpr std::array<const char*, kMaxReportCount> kHighestPmfMetricNames = {
    "Memory.Experimental.Renderer.HighestPrivateMemoryFootprint.0to2min",
    "Memory.Experimental.Renderer.HighestPrivateMemoryFootprint.2to4min",
    "Memory.Experimental.Renderer.HighestPrivateMemoryFootprint.4to8min",
    "Memory.Experimental.Renderer.HighestPrivateMemoryFootprint.8to16min"};

constexpr std::array<base::TimeDelta, kMaxReportCount> kTimeToReport = {
    base::Minutes(2), base::Minutes(4), base::Minutes(8), base::Minutes(16)};
#endif

}  // namespace

#if BUILDFLAG(IS_COBALT)
HighestPmfReporter* HighestPmfReporter::instance_ = nullptr;

HighestPmfReporter* HighestPmfReporter::Instance() {
  return instance_;
}
#endif

void HighestPmfReporter::Initialize(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  DEFINE_STATIC_LOCAL(HighestPmfReporter, reporter, (std::move(task_runner)));
  (void)reporter;
}

HighestPmfReporter::HighestPmfReporter(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : HighestPmfReporter(std::move(task_runner),
                         base::DefaultTickClock::GetInstance()) {}

#if BUILDFLAG(IS_COBALT)
HighestPmfReporter::~HighestPmfReporter() {
  if (instance_ == this) {
    instance_ = nullptr;
  }
  MemoryUsageMonitor::Instance().RemoveObserver(this);
}
#endif

HighestPmfReporter::HighestPmfReporter(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    const base::TickClock* clock)
    : task_runner_(std::move(task_runner)), clock_(clock) {
#if BUILDFLAG(IS_COBALT)
  instance_ = this;
#endif
  MemoryUsageMonitor::Instance().AddObserver(this);

#if BUILDFLAG(IS_COBALT)
  auto create_metric_info = [](base::TimeDelta time_to_report,
                               const std::string& suffix) {
    auto make_metric_name = [&suffix](const char* metric_name) {
      return WTF::String(
          (std::string(kMetricBasePrefix) + metric_name + suffix).c_str());
    };
    return MetricInfo{
        time_to_report,
        make_metric_name(kHighestPmfMetricName),
        make_metric_name(kHighestPmfForegroundedMetricName),
        make_metric_name(kPeakResidentSetMetricName),
        make_metric_name(kPeakResidentSetForegroundedMetricName)};
  };

  bool use_baseline = true;
  if (base::FeatureList::IsEnabled(features::kHighestPmfReporterConfigurable)) {
    std::string intervals = features::kHighestPmfReporterIntervals.Get();
    std::string suffixes = features::kHighestPmfReporterMetricSuffixes.Get();

    std::vector<std::string> interval_strs = base::SplitString(
        intervals, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    std::vector<std::string> suffix_strs = base::SplitString(
        suffixes, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

    if (interval_strs.size() == suffix_strs.size() && !interval_strs.empty()) {
      bool success = true;
      int previous_interval = -1;
      for (size_t i = 0; i < interval_strs.size(); ++i) {
        int interval_min;
        if (!base::StringToInt(interval_strs[i], &interval_min)) {
          success = false;
          break;
        }

        if (interval_min <= 0 || interval_min <= previous_interval) {
          success = false;
          break;
        }
        previous_interval = interval_min;

        metrics_.push_back(
            create_metric_info(base::Minutes(interval_min), suffix_strs[i]));
      }
      if (success) {
        use_baseline = false;
      } else {
        metrics_.clear();
      }
    }
  }

  if (use_baseline) {
    for (size_t i = 0; i < kBaselineReportCount; ++i) {
      metrics_.push_back(create_metric_info(kBaselineTimeToReport[i],
                                            kBaselineMetricSuffixes[i]));
    }
  }
#endif
}

bool HighestPmfReporter::FirstNavigationStarted() {
  if (first_navigation_detected_)
    return false;

  for (Page* page : Page::OrdinaryPages()) {
    Frame* frame = page->MainFrame();
    if (!frame)
      continue;

    auto* local_frame = DynamicTo<LocalFrame>(frame);
    if (!local_frame)
      continue;

    DocumentLoader* loader = local_frame->Loader().GetDocumentLoader();
    if (!loader)
      continue;

    if (!loader->GetTiming().NavigationStart().is_null()) {
      first_navigation_detected_ = true;
      return true;
    }
  }
  return false;
}

void HighestPmfReporter::OnMemoryPing(MemoryUsage usage) {
  DCHECK(IsMainThread());
  if (FirstNavigationStarted()) {
#if BUILDFLAG(IS_COBALT)
    // Only schedule initial startup reporting if the process has not been
    // backgrounded.
    if (!has_been_backgrounded_once_) {
      cancelable_report_task_.Reset(WTF::BindOnce(
          &HighestPmfReporter::OnReportMetrics, WTF::Unretained(this)));
      task_runner_->PostDelayedTask(
          FROM_HERE, cancelable_report_task_.callback(), metrics_[0].time_to_report);
    }
#else
    task_runner_->PostDelayedTask(
        FROM_HERE,
        WTF::BindOnce(&HighestPmfReporter::OnReportMetrics,
                      WTF::Unretained(this)),
        kTimeToReport[0]);
#endif
  }

  if (current_highest_pmf_ > usage.private_footprint_bytes)
    return;

  current_highest_pmf_ = usage.private_footprint_bytes;
  peak_resident_bytes_at_current_highest_pmf_ = usage.peak_resident_bytes;
#if BUILDFLAG(IS_COBALT)
  webpage_counts_at_current_highest_pmf_ = 1;
#else
  webpage_counts_at_current_highest_pmf_ = Page::OrdinaryPages().size();
#endif

  // TODO(tasak): Report the highest memory footprint throughout renderer's
  // lifetime.
}

#if BUILDFLAG(IS_COBALT)
// Handles transition into background: cancel in-flight reporting tasks and
// stop observing memory usage.
void HighestPmfReporter::OnProcessBackgrounded() {
  DCHECK(IsMainThread());
  cancelable_report_task_.Cancel();
  has_been_backgrounded_once_ = true;
  MemoryUsageMonitor::Instance().RemoveObserver(this);
}

// Handles transition when resumed from background into foreground: reset
// peak tracking and start a new measuring window for foreground metrics.
void HighestPmfReporter::OnProcessForegrounded() {
  DCHECK(IsMainThread());

  cancelable_report_task_.Cancel();
  current_highest_pmf_ = 0.0;
  peak_resident_bytes_at_current_highest_pmf_ = 0.0;
  webpage_counts_at_current_highest_pmf_ = 0;
  report_count_ = 0;
  has_been_backgrounded_once_ = true;

  if (metrics_.empty()) {
    return;
  }

  if (!MemoryUsageMonitor::Instance().HasObserver(this)) {
    MemoryUsageMonitor::Instance().AddObserver(this);
  }

  cancelable_report_task_.Reset(WTF::BindOnce(
      &HighestPmfReporter::OnReportMetrics, WTF::Unretained(this)));
  task_runner_->PostDelayedTask(
      FROM_HERE, cancelable_report_task_.callback(), metrics_[0].time_to_report);
}
#endif

void HighestPmfReporter::OnReportMetrics() {
  DCHECK(IsMainThread());
  ReportMetrics();

  // The following code is not accurate, because OnReportMetrics will be late
  // when renderer is slow (e.g. caused by near-OOM or heavy tasks is running
  // or ...). However such signal getting late by minutes is unlikely, so it's
  // ok to say "this is good enough".
  current_highest_pmf_ = 0.0;
  peak_resident_bytes_at_current_highest_pmf_ = 0.0;
  webpage_counts_at_current_highest_pmf_ = 0;
  report_count_++;
#if BUILDFLAG(IS_COBALT)
  if (report_count_ >= metrics_.size()) {
#else
  if (report_count_ >= kMaxReportCount) {
#endif
    // Stop observing the MemoryUsageMonitor once there's no more histogram to
    // report.
    MemoryUsageMonitor::Instance().RemoveObserver(this);
    return;
  }

#if BUILDFLAG(IS_COBALT)
  const base::TimeDelta delay =
      metrics_[report_count_].time_to_report - metrics_[report_count_ - 1].time_to_report;
#else
  const base::TimeDelta delay =
      kTimeToReport[report_count_] - kTimeToReport[report_count_ - 1];
#endif
#if BUILDFLAG(IS_COBALT)
  cancelable_report_task_.Reset(WTF::BindOnce(
      &HighestPmfReporter::OnReportMetrics, WTF::Unretained(this)));
  task_runner_->PostDelayedTask(FROM_HERE, cancelable_report_task_.callback(),
                                delay);
#else
  task_runner_->PostDelayedTask(
      FROM_HERE,
      WTF::BindOnce(&HighestPmfReporter::OnReportMetrics,
                    WTF::Unretained(this)),
      delay);
#endif
}

void HighestPmfReporter::ReportMetrics() {
#if BUILDFLAG(IS_COBALT)
  const MetricInfo& metric_info = metrics_[report_count_];
  const auto highest_pmf_mb = base::saturated_cast<base::Histogram::Sample32>(
      current_highest_pmf_ / 1024 / 1024);
  const auto peak_resident_mb =
      base::saturated_cast<base::Histogram::Sample32>(
          peak_resident_bytes_at_current_highest_pmf_ / 1024 / 1024);

  if (has_been_backgrounded_once_) {
    // Resumed from background state.
    base::UmaHistogramMemoryMB(metric_info.pmf_foregrounded_name.Utf8(),
                               highest_pmf_mb);
    base::UmaHistogramMemoryMB(metric_info.peak_rss_foregrounded_name.Utf8(),
                               peak_resident_mb);
  } else {
    // Initial startup / navigation state.
    base::UmaHistogramMemoryMB(metric_info.pmf_name.Utf8(), highest_pmf_mb);
    base::UmaHistogramMemoryMB(metric_info.peak_rss_name.Utf8(),
                               peak_resident_mb);
  }
#else
  base::UmaHistogramMemoryMB(kHighestPmfMetricNames[report_count_],
                             base::saturated_cast<base::Histogram::Sample32>(
                                 current_highest_pmf_ / 1024 / 1024));
#endif
}

}  // namespace blink
