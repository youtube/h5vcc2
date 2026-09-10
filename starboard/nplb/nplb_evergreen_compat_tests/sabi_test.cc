// Copyright 2020 The Cobalt Authors. All Rights Reserved.
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

#include <iostream>
#include <set>
#include <string>

#include "starboard/common/log.h"
#include "starboard/configuration.h"
#include "starboard/nplb/nplb_evergreen_compat_tests/checks.h"
#include "testing/gtest/include/gtest/gtest.h"

#if !BUILDFLAG(IS_STARBOARD)
#error These tests apply only to Starboard platforms.
#endif

namespace nplb {

namespace {

#if SB_API_VERSION == 18
const char* kSabiJsonIdArmHardfp =
    "{\"alignment_char\":1,\"alignment_double\":8,\"alignment_float\":4,"
    "\"alignment_int\":4,\"alignment_llong\":8,\"alignment_long\":4,"
    "\"alignment_pointer\":4,\"alignment_short\":2,\"calling_convention\":"
    "\"eabi\",\"endianness\":\"little\",\"floating_point_abi\":\"hard\","
    "\"floating_point_fpu\":\"vfpv3\",\"sb_api_version\":18,\"signedness_of_"
    "char\":\"signed\",\"signedness_of_enum\":\"unsigned\",\"size_of_char\":1,"
    "\"size_of_double\":8,\"size_of_enum\":4,\"size_of_float\":4,\"size_of_"
    "int\":4,\"size_of_llong\":8,\"size_of_long\":4,\"size_of_pointer\":4,"
    "\"size_of_short\":2,\"target_arch\":\"arm\",\"target_arch_sub\":\"v7a\","
    "\"word_size\":32}";

const char* kSabiJsonIdArmSoftfp =
    "{\"alignment_char\":1,\"alignment_double\":8,\"alignment_float\":4,"
    "\"alignment_int\":4,\"alignment_llong\":8,\"alignment_long\":4,"
    "\"alignment_pointer\":4,\"alignment_short\":2,\"calling_convention\":"
    "\"eabi\",\"endianness\":\"little\",\"floating_point_abi\":\"softfp\","
    "\"floating_point_fpu\":\"vfpv3\",\"sb_api_version\":18,\"signedness_of_"
    "char\":\"signed\",\"signedness_of_enum\":\"unsigned\",\"size_of_char\":1,"
    "\"size_of_double\":8,\"size_of_enum\":4,\"size_of_float\":4,\"size_of_"
    "int\":4,\"size_of_llong\":8,\"size_of_long\":4,\"size_of_pointer\":4,"
    "\"size_of_short\":2,\"target_arch\":\"arm\",\"target_arch_sub\":\"v7a\","
    "\"word_size\":32}";

const char* kSabiJsonIdArm64 =
    "{\"alignment_char\":1,\"alignment_double\":8,\"alignment_float\":4,"
    "\"alignment_int\":4,\"alignment_llong\":8,\"alignment_long\":8,"
    "\"alignment_pointer\":8,\"alignment_short\":2,\"calling_convention\":"
    "\"aarch64\",\"endianness\":\"little\",\"floating_point_abi\":\"\","
    "\"floating_point_fpu\":\"\",\"sb_api_version\":18,\"signedness_of_char\":"
    "\"signed\",\"signedness_of_enum\":\"unsigned\",\"size_of_char\":1,\"size_"
    "of_"
    "double\":8,\"size_of_enum\":4,\"size_of_float\":4,\"size_of_int\":4,"
    "\"size_of_llong\":8,\"size_of_long\":8,\"size_of_pointer\":8,\"size_of_"
    "short\":2,\"target_arch\":\"arm64\",\"target_arch_sub\":\"v8a\",\"word_"
    "size\":64}";

const char* kSabiJsonIdX64Sysv =
    "{\"alignment_char\":1,\"alignment_double\":8,\"alignment_float\":4,"
    "\"alignment_int\":4,\"alignment_llong\":8,\"alignment_long\":8,"
    "\"alignment_pointer\":8,\"alignment_short\":2,\"calling_convention\":"
    "\"sysv\",\"endianness\":\"little\",\"floating_point_abi\":\"\",\"floating_"
    "point_fpu\":\"\",\"sb_api_version\":18,\"signedness_of_char\":\"signed\","
    "\"signedness_of_enum\":\"unsigned\",\"size_of_char\":1,\"size_of_double\":"
    "8,"
    "\"size_of_enum\":4,\"size_of_float\":4,\"size_of_int\":4,\"size_of_"
    "llong\":8,\"size_of_long\":8,\"size_of_pointer\":8,\"size_of_short\":2,"
    "\"target_arch\":\"x64\",\"target_arch_sub\":\"\",\"word_size\":64}";
#endif  // SB_API_VERSION == 18

#if SB_API_VERSION == 17
const char* kSabiJsonIdArmHardfp =
    "{\"alignment_char\":1,\"alignment_double\":8,\"alignment_float\":4,"
    "\"alignment_int\":4,\"alignment_llong\":8,\"alignment_long\":4,"
    "\"alignment_pointer\":4,\"alignment_short\":2,\"calling_convention\":"
    "\"eabi\",\"endianness\":\"little\",\"floating_point_abi\":\"hard\","
    "\"floating_point_fpu\":\"vfpv3\",\"sb_api_version\":17,\"signedness_of_"
    "char\":\"signed\",\"signedness_of_enum\":\"signed\",\"size_of_char\":1,"
    "\"size_of_double\":8,\"size_of_enum\":4,\"size_of_float\":4,\"size_of_"
    "int\":4,\"size_of_llong\":8,\"size_of_long\":4,\"size_of_pointer\":4,"
    "\"size_of_short\":2,\"target_arch\":\"arm\",\"target_arch_sub\":\"v7a\","
    "\"word_size\":32}";

const char* kSabiJsonIdArmSoftfp =
    "{\"alignment_char\":1,\"alignment_double\":8,\"alignment_float\":4,"
    "\"alignment_int\":4,\"alignment_llong\":8,\"alignment_long\":4,"
    "\"alignment_pointer\":4,\"alignment_short\":2,\"calling_convention\":"
    "\"eabi\",\"endianness\":\"little\",\"floating_point_abi\":\"softfp\","
    "\"floating_point_fpu\":\"vfpv3\",\"sb_api_version\":17,\"signedness_of_"
    "char\":\"signed\",\"signedness_of_enum\":\"signed\",\"size_of_char\":1,"
    "\"size_of_double\":8,\"size_of_enum\":4,\"size_of_float\":4,\"size_of_"
    "int\":4,\"size_of_llong\":8,\"size_of_long\":4,\"size_of_pointer\":4,"
    "\"size_of_short\":2,\"target_arch\":\"arm\",\"target_arch_sub\":\"v7a\","
    "\"word_size\":32}";

const char* kSabiJsonIdArm64 =
    "{\"alignment_char\":1,\"alignment_double\":8,\"alignment_float\":4,"
    "\"alignment_int\":4,\"alignment_llong\":8,\"alignment_long\":8,"
    "\"alignment_pointer\":8,\"alignment_short\":2,\"calling_convention\":"
    "\"aarch64\",\"endianness\":\"little\",\"floating_point_abi\":\"\","
    "\"floating_point_fpu\":\"\",\"sb_api_version\":17,\"signedness_of_char\":"
    "\"signed\",\"signedness_of_enum\":\"signed\",\"size_of_char\":1,\"size_of_"
    "double\":8,\"size_of_enum\":4,\"size_of_float\":4,\"size_of_int\":4,"
    "\"size_of_llong\":8,\"size_of_long\":8,\"size_of_pointer\":8,\"size_of_"
    "short\":2,\"target_arch\":\"arm64\",\"target_arch_sub\":\"v8a\",\"word_"
    "size\":64}";

const char* kSabiJsonIdX64Sysv =
    "{\"alignment_char\":1,\"alignment_double\":8,\"alignment_float\":4,"
    "\"alignment_int\":4,\"alignment_llong\":8,\"alignment_long\":8,"
    "\"alignment_pointer\":8,\"alignment_short\":2,\"calling_convention\":"
    "\"sysv\",\"endianness\":\"little\",\"floating_point_abi\":\"\",\"floating_"
    "point_fpu\":\"\",\"sb_api_version\":17,\"signedness_of_char\":\"signed\","
    "\"signedness_of_enum\":\"signed\",\"size_of_char\":1,\"size_of_double\":8,"
    "\"size_of_enum\":4,\"size_of_float\":4,\"size_of_int\":4,\"size_of_"
    "llong\":8,\"size_of_long\":8,\"size_of_pointer\":8,\"size_of_short\":2,"
    "\"target_arch\":\"x64\",\"target_arch_sub\":\"\",\"word_size\":64}";
#endif  // SB_API_VERSION == 17

TEST(SabiTest, VerifySABI) {
  SB_LOG(INFO) << "Using SB_API_VERSION=" << SB_API_VERSION;
  SB_LOG(INFO) << "Using SABI=" << SB_SABI_JSON_ID;

  ASSERT_LE(SB_API_VERSION, SB_MAXIMUM_API_VERSION)
      << "Evergreen should use SB_API_VERSION <= SB_MAXIMUM_API_VERSION";

  std::set<std::string> sabi_set;
  sabi_set.insert(kSabiJsonIdArmHardfp);
  sabi_set.insert(kSabiJsonIdArmSoftfp);
  sabi_set.insert(kSabiJsonIdArm64);
  sabi_set.insert(kSabiJsonIdX64Sysv);

  ASSERT_NE(sabi_set.find(SB_SABI_JSON_ID), sabi_set.end())
      << "Unsupported SABI configuration. " << std::endl
      << "The platform should use one of the predefined SABI json files!"
      << std::endl
      << "Currently supported are: " << std::endl
      << "  starboard/sabi/arm/hardfp/sabi-v" << SB_API_VERSION << ".json"
      << std::endl
      << "  starboard/sabi/arm/softfp/sabi-v" << SB_API_VERSION << ".json"
      << std::endl
      << "  starboard/sabi/arm64/sabi-v" << SB_API_VERSION << ".json"
      << std::endl
      << "  starboard/sabi/x64/sysv/sabi-v" << SB_API_VERSION << ".json"
      << std::endl;
}

}  // namespace
}  // namespace nplb
