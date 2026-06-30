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

#include "media/base/starboard/experimental_features.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace media {
namespace {

constexpr ExperimentalFeatureKey<bool> kFooFeatureA("Foo.FeatureA");
constexpr ExperimentalFeatureKey<bool> kFooFeatureB("Foo.FeatureB");
constexpr ExperimentalFeatureKey<bool> kFooFeatureC("Foo.FeatureC");
constexpr ExperimentalFeatureKey<int> kFooIntFeature("Foo.IntFeature");
constexpr ExperimentalFeatureKey<std::string> kFooStringFeature(
    "Foo.StringFeature");

TEST(ExperimentalFeaturesTest, GetBool) {
  ExperimentalFeatures::Map map;
  map["Foo.FeatureA"] = static_cast<int64_t>(1);
  map["Foo.FeatureB"] = static_cast<int64_t>(0);
  ExperimentalFeatures settings(map);

  EXPECT_TRUE(settings.GetBool(kFooFeatureA));
  EXPECT_FALSE(settings.GetBool(kFooFeatureB));
  EXPECT_FALSE(settings.GetBool(kFooFeatureC));
}

TEST(ExperimentalFeaturesTest, GenericGet) {
  ExperimentalFeatures::Map map;
  map["Foo.FeatureA"] = static_cast<int64_t>(1);
  map["Foo.FeatureB"] = static_cast<int64_t>(0);
  map["Foo.IntFeature"] = static_cast<int64_t>(50);
  map["Foo.StringFeature"] = std::string("bar");
  ExperimentalFeatures settings(map);

  EXPECT_EQ(settings.Get(kFooFeatureA), std::optional<bool>(true));
  EXPECT_EQ(settings.Get(kFooFeatureB), std::optional<bool>(false));
  EXPECT_EQ(settings.Get(kFooIntFeature), std::optional<int>(50));
  EXPECT_EQ(settings.Get(kFooStringFeature), std::optional<std::string>("bar"));
}

TEST(ExperimentalFeaturesTest, IntSentinelReturnsNullopt) {
  ExperimentalFeatures::Map map;
  map["Foo.IntFeature"] = static_cast<int64_t>(0);  // 0 is unset sentinel
  ExperimentalFeatures settings(map);

  EXPECT_EQ(settings.Get(kFooIntFeature), std::nullopt);
}

TEST(ExperimentalFeaturesTest, IntOutOfBoundsReturnsNullopt) {
  ExperimentalFeatures::Map map;
  map["Foo.IntFeature"] =
      static_cast<int64_t>(3000000000LL);  // Exceeds 32-bit int
  ExperimentalFeatures settings(map);

  EXPECT_EQ(settings.Get(kFooIntFeature), std::nullopt);
}

TEST(ExperimentalFeaturesTest, MissingKeyReturnsNullopt) {
  ExperimentalFeatures settings;

  EXPECT_EQ(settings.Get(kFooFeatureA), std::nullopt);
  EXPECT_EQ(settings.Get(kFooIntFeature), std::nullopt);
  EXPECT_EQ(settings.Get(kFooStringFeature), std::nullopt);
}

TEST(ExperimentalFeaturesTest, TypeMismatchReturnsNullopt) {
  ExperimentalFeatures::Map map;
  map["Foo.IntFeature"] = std::string("not_an_int");
  map["Foo.StringFeature"] = static_cast<int64_t>(123);
  ExperimentalFeatures settings(map);

  EXPECT_EQ(settings.Get(kFooIntFeature), std::nullopt);
  EXPECT_EQ(settings.Get(kFooStringFeature), std::nullopt);
}

}  // namespace
}  // namespace media
