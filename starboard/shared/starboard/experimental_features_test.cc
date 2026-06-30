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

#include "starboard/shared/starboard/experimental_features.h"

#include <optional>
#include <sstream>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {
namespace {

constexpr ExperimentalFeatureKey<bool> kTestBoolKey("Test.BoolKey");
constexpr ExperimentalFeatureKey<int> kTestIntKey("Test.IntKey");
constexpr ExperimentalFeatureKey<std::string> kTestStringKey("Test.StringKey");

TEST(StarboardExperimentalFeaturesTest, BasicGetters) {
  ExperimentalFeatures::Map map;
  map["Test.BoolKey"] = static_cast<int64_t>(1);
  map["Test.IntKey"] = static_cast<int64_t>(42);
  map["Test.StringKey"] = std::string("test_val");
  ExperimentalFeatures features(map);

  EXPECT_TRUE(features.GetBool(kTestBoolKey));
  EXPECT_EQ(features.Get(kTestBoolKey), std::optional<bool>(true));
  EXPECT_EQ(features.Get(kTestIntKey), std::optional<int>(42));
  EXPECT_EQ(features.Get(kTestStringKey),
            std::optional<std::string>("test_val"));
}

TEST(StarboardExperimentalFeaturesTest, IntSentinelReturnsNullopt) {
  ExperimentalFeatures::Map map;
  map["Test.IntKey"] = static_cast<int64_t>(0);  // 0 is sentinel for unset
  ExperimentalFeatures features(map);

  EXPECT_EQ(features.Get(kTestIntKey), std::nullopt);
}

TEST(StarboardExperimentalFeaturesTest, IntOutOfBoundsReturnsNullopt) {
  ExperimentalFeatures::Map map;
  map["Test.IntKey"] =
      static_cast<int64_t>(3000000000LL);  // Exceeds 32-bit int
  ExperimentalFeatures features(map);

  EXPECT_EQ(features.Get(kTestIntKey), std::nullopt);
}

TEST(StarboardExperimentalFeaturesTest, MissingKeysAndTypeMismatch) {
  ExperimentalFeatures::Map map;
  map["Test.IntKey"] = std::string("not_an_int");
  ExperimentalFeatures features(map);

  EXPECT_EQ(features.Get(kTestBoolKey), std::nullopt);
  EXPECT_EQ(features.Get(kTestIntKey), std::nullopt);
}

TEST(StarboardExperimentalFeaturesTest, OutputStreamOperatorFormatsCorrectly) {
  ExperimentalFeatures::Map map;
  map["Test.IntKey"] = static_cast<int64_t>(42);
  ExperimentalFeatures features(map);

  std::ostringstream ss;
  ss << features;
  EXPECT_EQ(ss.str(), "{Test.IntKey=42}");
}

}  // namespace
}  // namespace starboard
