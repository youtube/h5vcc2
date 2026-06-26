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

#include "cobalt/browser/hang_watcher_delegate_impl.h"

#include <memory>
#include <string>

#include "base/time/time.h"
#include "cobalt/browser/global_features.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cobalt {
namespace browser {

class HangWatcherDelegateImplTest : public testing::Test {
 protected:
  void SetUp() override {
    delegate_ = std::make_unique<CobaltHangWatcherDelegate>();
  }

  void TearDown() override {
    // Clear GlobalFeatures settings. By storing a std::string value for the
    // keys, the std::get_if<int64_t> checks in the implementation will
    // naturally fail, simulating an "unset" state for the numerical parameters
    // without modifying global_features.h.
    auto* global_features = GlobalFeatures::GetInstance();
    if (global_features) {
      global_features->SetSettings("EnableHangReporting", std::string("unset"));
      global_features->SetSettings("HangWatchTimeSeconds",
                                   std::string("unset"));
      global_features->SetSettings("HangWatchMonitoringPeriodSeconds",
                                   std::string("unset"));
      global_features->SetSettings("EnableHangWatchMainThreadDump",
                                   std::string("unset"));
      global_features->SetSettings("EnableHangWatchIOThreadDump",
                                   std::string("unset"));
      global_features->SetSettings("EnableHangWatchThreadPoolDump",
                                   std::string("unset"));
      global_features->SetSettings("EnableHangWatchRendererThreadDump",
                                   std::string("unset"));
    }
  }

  std::unique_ptr<CobaltHangWatcherDelegate> delegate_;
};

TEST_F(HangWatcherDelegateImplTest, IsHangReportingEnabled_Default) {
  // Should default to base::FeatureList value (which we can't easily mock here,
  // but it shouldn't crash).
  delegate_->IsHangReportingEnabled();
}

TEST_F(HangWatcherDelegateImplTest, IsHangReportingEnabled_GlobalFeatures_Int) {
  auto* global_features = GlobalFeatures::GetInstance();
  global_features->SetSettings("EnableHangReporting", int64_t(1));
  EXPECT_TRUE(delegate_->IsHangReportingEnabled());

  global_features->SetSettings("EnableHangReporting", int64_t(0));
  EXPECT_FALSE(delegate_->IsHangReportingEnabled());
}

TEST_F(HangWatcherDelegateImplTest, GetHangWatchTime_GlobalFeatures) {
  auto* global_features = GlobalFeatures::GetInstance();
  global_features->SetSettings("HangWatchTimeSeconds", int64_t(15));

  auto timeout = delegate_->GetHangWatchTime();
  ASSERT_TRUE(timeout.has_value());
  EXPECT_EQ(timeout->InSeconds(), 15);
}

TEST_F(HangWatcherDelegateImplTest,
       GetHangWatchMonitoringPeriod_GlobalFeatures) {
  auto* global_features = GlobalFeatures::GetInstance();
  global_features->SetSettings("HangWatchMonitoringPeriodSeconds", int64_t(8));
  auto period = delegate_->GetHangWatchMonitoringPeriod();
  ASSERT_TRUE(period.has_value());
  EXPECT_EQ(period->InSeconds(), 8);
}

TEST_F(HangWatcherDelegateImplTest, IsThreadDumpingEnabled_GlobalFeatures) {
  auto* global_features = GlobalFeatures::GetInstance();

  global_features->SetSettings("EnableHangWatchIOThreadDump", int64_t(1));
  auto io_enabled = delegate_->IsThreadDumpingEnabled(
      base::HangWatcher::ThreadType::kIOThread);
  ASSERT_TRUE(io_enabled.has_value());
  EXPECT_TRUE(*io_enabled);

  global_features->SetSettings("EnableHangWatchIOThreadDump", int64_t(0));
  io_enabled = delegate_->IsThreadDumpingEnabled(
      base::HangWatcher::ThreadType::kIOThread);
  ASSERT_TRUE(io_enabled.has_value());
  EXPECT_FALSE(*io_enabled);

  global_features->SetSettings("EnableHangWatchRendererThreadDump", int64_t(1));
  auto renderer_enabled = delegate_->IsThreadDumpingEnabled(
      base::HangWatcher::ThreadType::kRendererThread);
  ASSERT_TRUE(renderer_enabled.has_value());
  EXPECT_TRUE(*renderer_enabled);
}

}  // namespace browser
}  // namespace cobalt
