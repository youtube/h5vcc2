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

#include "cobalt/browser/metrics/cobalt_stability_metrics_helper.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/process/process_handle.h"
#include "base/time/time.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cobalt {
namespace {

constexpr char kExpectedAllocatorName[] = "BrowserStabilityMetrics";

class CobaltStabilityMetricsHelperTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  const base::FilePath& metrics_dir() const { return temp_dir_.GetPath(); }

  base::ScopedTempDir temp_dir_;
};

TEST_F(CobaltStabilityMetricsHelperTest,
       HandlesEmptyAndNonExistentDirectories) {
  base::FilePath non_existent = metrics_dir().AppendASCII("non_existent_dir");
  EXPECT_TRUE(ExtractPriorSessionPids(non_existent, kExpectedAllocatorName,
                                      /*current_pid=*/100)
                  .empty());

  base::FilePath empty_dir = metrics_dir().AppendASCII("empty_dir");
  ASSERT_TRUE(base::CreateDirectory(empty_dir));
  EXPECT_TRUE(ExtractPriorSessionPids(empty_dir, kExpectedAllocatorName,
                                      /*current_pid=*/100)
                  .empty());
}

TEST_F(CobaltStabilityMetricsHelperTest, ExtractsAndDeduplicatesPriorPids) {
  base::Time stamp1 = base::Time::FromTimeT(1700000000);
  base::Time stamp2 = base::Time::FromTimeT(1700000100);

  // Two files with the same PID 1234 but different timestamps.
  base::FilePath f1 =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp1, 1234);
  base::FilePath f2 =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp2, 1234);
  ASSERT_TRUE(base::WriteFile(f1, ""));
  ASSERT_TRUE(base::WriteFile(f2, ""));

  // One file with a distinct PID 5678.
  base::FilePath f3 =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp1, 5678);
  ASSERT_TRUE(base::WriteFile(f3, ""));

  std::vector<base::ProcessId> pids = ExtractPriorSessionPids(
      metrics_dir(), kExpectedAllocatorName, /*current_pid=*/9999);
  EXPECT_THAT(pids, ::testing::UnorderedElementsAre(1234, 5678));
}

TEST_F(CobaltStabilityMetricsHelperTest, FiltersOutCurrentProcessPid) {
  base::Time stamp = base::Time::FromTimeT(1700000000);
  base::ProcessId current_pid = 4321;

  base::FilePath current_file =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp, current_pid);
  ASSERT_TRUE(base::WriteFile(current_file, ""));

  base::FilePath prior_file =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp, 1111);
  ASSERT_TRUE(base::WriteFile(prior_file, ""));

  std::vector<base::ProcessId> pids = ExtractPriorSessionPids(
      metrics_dir(), kExpectedAllocatorName, current_pid);
  EXPECT_THAT(pids, ::testing::ElementsAre(1111));
}

TEST_F(CobaltStabilityMetricsHelperTest, RejectsMismatchedAllocatorNames) {
  base::Time stamp = base::Time::FromTimeT(1700000000);

  base::FilePath other_allocator_file =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), "OtherAllocator", stamp, 1234);
  ASSERT_TRUE(base::WriteFile(other_allocator_file, ""));

  base::FilePath expected_file =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp, 5678);
  ASSERT_TRUE(base::WriteFile(expected_file, ""));

  std::vector<base::ProcessId> pids = ExtractPriorSessionPids(
      metrics_dir(), kExpectedAllocatorName, /*current_pid=*/9999);
  EXPECT_THAT(pids, ::testing::ElementsAre(5678));
}

TEST_F(CobaltStabilityMetricsHelperTest, IgnoresCorruptFilenamesAndSubdirs) {
  base::Time stamp = base::Time::FromTimeT(1700000000);

  // Non-.pma extension.
  ASSERT_TRUE(
      base::WriteFile(metrics_dir().AppendASCII("not_a_pma_file.txt"), ""));
  ASSERT_TRUE(base::WriteFile(
      metrics_dir().AppendASCII("BrowserStabilityMetrics-65550000-1234.tmp"),
      ""));

  // Malformed PMA filenames that fail ParseFilePath.
  ASSERT_TRUE(
      base::WriteFile(metrics_dir().AppendASCII("corrupt_name.pma"), ""));
  ASSERT_TRUE(base::WriteFile(
      metrics_dir().AppendASCII("BrowserStabilityMetrics-invalidhex-1234.pma"),
      ""));
  ASSERT_TRUE(base::WriteFile(
      metrics_dir().AppendASCII("BrowserStabilityMetrics-65550000-nothex.pma"),
      ""));

  // Subdirectories should not be traversed or counted as files.
  ASSERT_TRUE(base::CreateDirectory(metrics_dir().AppendASCII("subdir.pma")));
  ASSERT_TRUE(base::CreateDirectory(
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp, 9876)));

  // Valid file.
  base::FilePath valid_file =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp, 7777);
  ASSERT_TRUE(base::WriteFile(valid_file, ""));

  std::vector<base::ProcessId> pids = ExtractPriorSessionPids(
      metrics_dir(), kExpectedAllocatorName, /*current_pid=*/9999);
  EXPECT_THAT(pids, ::testing::ElementsAre(7777));
}

TEST_F(CobaltStabilityMetricsHelperTest, RejectsZeroAndNegativePids) {
  base::Time stamp = base::Time::FromTimeT(1700000000);

  // Zero PID.
  base::FilePath zero_pid_file =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp, 0);
  ASSERT_TRUE(base::WriteFile(zero_pid_file, ""));

  // Negative PID in filename.
  ASSERT_TRUE(base::WriteFile(
      metrics_dir().AppendASCII("BrowserStabilityMetrics-65550000--1.pma"),
      ""));

  // Valid PID.
  base::FilePath valid_file =
      base::GlobalHistogramAllocator::ConstructFilePathForUploadDir(
          metrics_dir(), kExpectedAllocatorName, stamp, 8888);
  ASSERT_TRUE(base::WriteFile(valid_file, ""));

  std::vector<base::ProcessId> pids = ExtractPriorSessionPids(
      metrics_dir(), kExpectedAllocatorName, /*current_pid=*/9999);
  EXPECT_THAT(pids, ::testing::ElementsAre(8888));
}

}  // namespace
}  // namespace cobalt
