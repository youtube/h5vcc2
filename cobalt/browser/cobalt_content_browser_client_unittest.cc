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

#include "base/command_line.h"
#include "base/numerics/safe_conversions.h"
#include "base/test/scoped_command_line.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "cobalt/browser/global_features.h"
#include "content/public/browser/overlay_window.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "services/network/public/mojom/network_context.mojom.h"
#if BUILDFLAG(IS_STARBOARD)
#include "starboard/configuration_constants.h"
#endif
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace cobalt {
namespace {

class CobaltContentBrowserClientTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
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

TEST_F(CobaltContentBrowserClientTest,
       ConfigureNetworkContextParams_DefaultHttpCacheMaxSize) {
#if BUILDFLAG(IS_STARBOARD)
  CobaltContentBrowserClient client(/*startup_timestamp=*/absl::nullopt,
                                    /*deep_link=*/"",
                                    /*is_visible=*/true);
  content::TestBrowserContext context;
  auto params = network::mojom::NetworkContextParams::New();
  client.ConfigureNetworkContextParams(
      &context, /*in_memory=*/false, base::FilePath(), params.get(),
      /*cert_verifier_creation_params=*/nullptr);
  EXPECT_EQ(params->http_cache_max_size,
            base::saturated_cast<int32_t>(kSbMaxSystemPathCacheDirectorySize));
#else
  GTEST_SKIP() << "kSbMaxSystemPathCacheDirectorySize is not defined for "
                  "non-Starboard builds.";
#endif
}

TEST_F(CobaltContentBrowserClientTest,
       ConfigureNetworkContextParams_HttpCacheMaxSizeFromCommandLineOverride) {
  base::test::ScopedCommandLine scoped_command_line;
  scoped_command_line.GetProcessCommandLine()->AppendSwitchASCII(
      "max-http-cache-size", "12345");
  CobaltContentBrowserClient client(/*startup_timestamp=*/absl::nullopt,
                                    /*deep_link=*/"",
                                    /*is_visible=*/true);
  content::TestBrowserContext context;
  auto params = network::mojom::NetworkContextParams::New();
  client.ConfigureNetworkContextParams(
      &context, /*in_memory=*/false, base::FilePath(), params.get(),
      /*cert_verifier_creation_params=*/nullptr);
  EXPECT_EQ(params->http_cache_max_size, 12345);
}

TEST_F(CobaltContentBrowserClientTest,
       ConfigureNetworkContextParams_HttpCacheMaxSizeZeroWhenInMemory) {
  CobaltContentBrowserClient client(/*startup_timestamp=*/absl::nullopt,
                                    /*deep_link=*/"",
                                    /*is_visible=*/true);
  auto params = network::mojom::NetworkContextParams::New();
  client.ConfigureNetworkContextParams(
      /*context=*/nullptr, /*in_memory=*/true, base::FilePath(), params.get(),
      /*cert_verifier_creation_params=*/nullptr);
  EXPECT_EQ(params->http_cache_max_size, 0);
}

}  // namespace
}  // namespace cobalt
