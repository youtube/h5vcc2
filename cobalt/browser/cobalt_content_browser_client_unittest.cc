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

#include "cobalt/browser/cobalt_content_browser_client.h"

#include <string>
#include <variant>

#include "base/files/file_path.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "cobalt/browser/global_features.h"
#include "content/public/browser/overlay_window.h"
#include "starboard/configuration_constants.h"
#include "storage/browser/quota/quota_device_info_helper.h"
#include "storage/browser/quota/quota_settings.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace cobalt {
namespace {

class MockQuotaDeviceInfoHelper : public storage::QuotaDeviceInfoHelper {
 public:
  MockQuotaDeviceInfoHelper() = default;
  MOCK_METHOD(int64_t,
              AmountOfTotalDiskSpace,
              (const base::FilePath&),
              (const, override));
  MOCK_METHOD(uint64_t, AmountOfPhysicalMemory, (), (const, override));
};

class CobaltContentBrowserClientTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::DEFAULT,
      base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
};

TEST_F(CobaltContentBrowserClientTest, ParseAndApplyH5vccSettingsForTesting) {
  auto* instance = GlobalFeatures::GetInstance();
  ASSERT_NE(instance, nullptr);

  ParseAndApplyH5vccSettingsForTesting("Foo=1234;Bar=Baz", instance);

  const auto& settings = instance->GetSettings();
  auto it1 = settings.find("Foo");
  ASSERT_NE(it1, settings.end());
  EXPECT_EQ(std::get<int64_t>(it1->second), 1234);

  auto it2 = settings.find("Bar");
  ASSERT_NE(it2, settings.end());
  EXPECT_EQ(std::get<std::string>(it2->second), "Baz");
}

TEST_F(CobaltContentBrowserClientTest,
       CreateWindowForVideoPictureInPicturePlatformBehavior) {
  CobaltContentBrowserClient client(/*startup_timestamp=*/absl::nullopt,
                                    /*deep_link=*/"",
                                    /*is_visible=*/true);
  std::unique_ptr<content::VideoOverlayWindow> window =
      client.CreateWindowForVideoPictureInPicture(/*controller=*/nullptr);
// TODO: b/532158001 - Support PiP on Linux.
#if BUILDFLAG(IS_ANDROID)
  EXPECT_NE(window, nullptr);
#else
  EXPECT_EQ(window, nullptr);
#endif
}

TEST_F(CobaltContentBrowserClientTest, GetCacheQuotaSettings_LargeDiskSpace) {
  MockQuotaDeviceInfoHelper device_info_helper;
  const int64_t kTotalDiskSpace = 500LL * 1024 * 1024 * 1024;  // 500 GB
  EXPECT_CALL(device_info_helper, AmountOfTotalDiskSpace(testing::_))
      .WillRepeatedly(testing::Return(kTotalDiskSpace));

  auto settings =
      CobaltContentBrowserClient::CalculateCacheQuotaSettingsForTesting(
          base::FilePath(FILE_PATH_LITERAL("/dummy/cache")),
          &device_info_helper);

  ASSERT_TRUE(settings.has_value());
  const int64_t expected_pool_size =
      static_cast<int64_t>(kSbMaxSystemPathCacheDirectorySize);
  EXPECT_EQ(settings->pool_size, expected_pool_size);
  EXPECT_EQ(settings->per_storage_key_quota, expected_pool_size);
  EXPECT_EQ(settings->session_only_per_storage_key_quota, expected_pool_size);
  // Must remain available should be min(1GB, 500GB * 0.01 = 5GB) = 1GB.
  EXPECT_EQ(settings->must_remain_available, 1024LL * 1024 * 1024);
  // Should remain available should be min(2GB, 500GB * 0.10 = 50GB) = 2GB.
  EXPECT_EQ(settings->should_remain_available, 2048LL * 1024 * 1024);
}

TEST_F(CobaltContentBrowserClientTest, GetCacheQuotaSettings_SmallDiskSpace) {
  MockQuotaDeviceInfoHelper device_info_helper;
  const int64_t kTotalDiskSpace = 10LL * 1024 * 1024;  // 10 MB (< 24 MiB)
  EXPECT_CALL(device_info_helper, AmountOfTotalDiskSpace(testing::_))
      .WillRepeatedly(testing::Return(kTotalDiskSpace));

  auto settings =
      CobaltContentBrowserClient::CalculateCacheQuotaSettingsForTesting(
          base::FilePath(FILE_PATH_LITERAL("/dummy/cache")),
          &device_info_helper);

  ASSERT_TRUE(settings.has_value());
  EXPECT_EQ(settings->pool_size, kTotalDiskSpace);
  EXPECT_EQ(settings->per_storage_key_quota, kTotalDiskSpace);
  EXPECT_EQ(settings->session_only_per_storage_key_quota, kTotalDiskSpace);
  // Must remain available should be min(1GB, 10MB * 0.01) = 104857 bytes (~100
  // KB).
  EXPECT_EQ(settings->must_remain_available,
            static_cast<int64_t>(kTotalDiskSpace * 0.01));
  // Should remain available should be min(2GB, 10MB * 0.10) = 1048576 bytes (1
  // MB).
  EXPECT_EQ(settings->should_remain_available,
            static_cast<int64_t>(kTotalDiskSpace * 0.10));
}

TEST_F(CobaltContentBrowserClientTest, GetCacheQuotaSettings_DiskSpaceError) {
  MockQuotaDeviceInfoHelper device_info_helper;
  EXPECT_CALL(device_info_helper, AmountOfTotalDiskSpace(testing::_))
      .WillRepeatedly(testing::Return(-1));

  auto settings =
      CobaltContentBrowserClient::CalculateCacheQuotaSettingsForTesting(
          base::FilePath(FILE_PATH_LITERAL("/dummy/cache")),
          &device_info_helper);

  EXPECT_FALSE(settings.has_value());
}

TEST_F(CobaltContentBrowserClientTest, GetCacheQuotaSettings_AsyncDispatch) {
  CobaltContentBrowserClient client(/*startup_timestamp=*/absl::nullopt,
                                    /*deep_link=*/"",
                                    /*is_visible=*/true);
  base::test::TestFuture<std::optional<storage::QuotaSettings>> future;
  client.GetCacheQuotaSettings(/*browser_context=*/nullptr,
                               base::FilePath(FILE_PATH_LITERAL("/tmp")),
                               future.GetCallback());
  task_environment_.RunUntilIdle();
  auto settings = future.Take();
  ASSERT_TRUE(settings.has_value());
  EXPECT_GT(settings->pool_size, 0);
  EXPECT_EQ(settings->per_storage_key_quota, settings->pool_size);
}

}  // namespace
}  // namespace cobalt
