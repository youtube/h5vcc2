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

#include "media/base/starboard/ipc_param_traits.h"

#include "ipc/ipc_message.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

TEST(IpcParamTraitsTest, SerializeAndDeserialize) {
  ExperimentalFeatures::Map map;
  map["Foo.BoolFeature"] = static_cast<int64_t>(1);
  map["Foo.IntFeature"] = static_cast<int64_t>(42);
  map["Foo.StringFeature"] = std::string("bar");

  ExperimentalFeatures original_features(map);

  base::Pickle pickle;
  IPC::ParamTraits<ExperimentalFeatures>::Write(&pickle, original_features);

  base::PickleIterator iter(pickle);
  ExperimentalFeatures deserialized_features;
  EXPECT_TRUE(IPC::ParamTraits<ExperimentalFeatures>::Read(
      &pickle, &iter, &deserialized_features));

  constexpr ExperimentalFeatureKey<bool> kFooBoolFeature("Foo.BoolFeature");
  constexpr ExperimentalFeatureKey<int> kFooIntFeature("Foo.IntFeature");
  constexpr ExperimentalFeatureKey<std::string> kFooStringFeature(
      "Foo.StringFeature");

  EXPECT_TRUE(deserialized_features.GetBool(kFooBoolFeature));
  EXPECT_EQ(deserialized_features.Get(kFooIntFeature), std::optional<int>(42));
  EXPECT_EQ(deserialized_features.Get(kFooStringFeature),
            std::optional<std::string>("bar"));
}

}  // namespace media
