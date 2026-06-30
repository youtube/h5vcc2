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

#include <array>
#include <string>
#include <vector>

#include "base/base_switches.h"
#include "build/buildflag.h"
#include "cobalt/browser/switches.h"
#include "cobalt/shell/common/shell_switches.h"
#include "cobalt_switch_defaults.h"
#include "content/public/common/content_switches.h"
#include "gpu/config/gpu_switches.h"
#include "media/base/media_switches.h"
#include "sandbox/policy/switches.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gl/gl_switches.h"

namespace cobalt {
namespace {

bool HasSwitch(const CommandLinePreprocessor& cmd_line_pxr,
               const char* switch_str) {
  return cmd_line_pxr.get_cmd_line_for_test().HasSwitch(switch_str);
}

std::string GetSwitchValue(const CommandLinePreprocessor& cmd_line_pxr,
                           const char* switch_str) {
  if (HasSwitch(cmd_line_pxr, switch_str)) {
    return cmd_line_pxr.get_cmd_line_for_test().GetSwitchValueASCII(switch_str);
  }
  return "";
}

TEST(CobaltSwitchDefaultsTest, MergeDisabledFeatures) {
  const auto input_argv = std::to_array<const char*>(
      {"PROGRAM", "--disable-features=PersistentOriginTrials"});
  const int input_argc = static_cast<int>(input_argv.size());
  CommandLinePreprocessor cmd_line_pxr(input_argc, input_argv.data());

  std::string disabled_features =
      GetSwitchValue(cmd_line_pxr, ::switches::kDisableFeatures);
  EXPECT_EQ(
      std::string("PersistentOriginTrials,Vulkan,MemoryCacheStrongReference"),
      disabled_features);
}

TEST(CobaltSwitchDefaultsTest, ConsistentWindowSizes) {
  const auto input_argv = std::to_array<const char*>({
      "PROGRAM",
      "--window-size=1280,1024",
  });
  const int input_argc = static_cast<int>(input_argv.size());
  CommandLinePreprocessor cmd_line_pxr(input_argc, input_argv.data());

  std::string content_shell_size =
      (GetSwitchValue(cmd_line_pxr, ::switches::kContentShellHostWindowSize));
  EXPECT_EQ(std::string("1280x1024"), content_shell_size);
}

TEST(CobaltSwitchDefaultsTest, ConsistentWindowSizesOverride) {
  const auto input_argv = std::to_array<const char*>({
      "PROGRAM",
      "--window-size=1280,1024",
      "--content-shell-host-window-size=1920x1080",
  });
  const int input_argc = static_cast<int>(input_argv.size());
  CommandLinePreprocessor cmd_line_pxr(input_argc, input_argv.data());

  std::string content_shell_size =
      (GetSwitchValue(cmd_line_pxr, ::switches::kContentShellHostWindowSize));
  EXPECT_EQ(std::string("1280x1024"), content_shell_size);
}

TEST(CobaltSwitchDefaultsTest, GfxAngleOverride) {
  const auto input_argv = std::to_array<const char*>({
      "PROGRAM",
      "--use-angle=swiftshader",
  });
  const int input_argc = static_cast<int>(input_argv.size());
  CommandLinePreprocessor cmd_line_pxr(input_argc, input_argv.data());

  std::string use_angle_setting =
      GetSwitchValue(cmd_line_pxr, ::switches::kUseANGLE);
  EXPECT_EQ(std::string("swiftshader"), use_angle_setting);
  // Note: This swiftshader argument being respected by the defaults is required
  // for running in Forge environments.
}

TEST(CobaltSwitchDefaultsTest, AlwaysEnabledSwitches) {
  const auto input_argv = std::to_array<const char*>({"PROGRAM"});
  const int input_argc = static_cast<int>(input_argv.size());
  CommandLinePreprocessor cmd_line_pxr(input_argc, input_argv.data());

  std::vector<const char*> always_on_switches{
      ::switches::kForceVideoOverlays,
      ::switches::kSingleProcess,
      ::switches::kIgnoreGpuBlocklist,
#if BUILDFLAG(IS_ANDROID)
      ::switches::kUserLevelMemoryPressureSignalParams,
#endif  // BUILDFLAG(IS_ANDROID)
      sandbox::policy::switches::kNoSandbox,
      ::switches::kHideScrollbars};

  for (const auto& switch_key : always_on_switches) {
    EXPECT_TRUE(HasSwitch(cmd_line_pxr, switch_key));
  }
  // Other default switches are subject to changes later down the line.
}

TEST(CobaltSwitchDefaultsTest, StartupURLSwitch) {
  const auto input_argv = std::to_array<const char*>({
      "PROGRAM",
      "--url=foo",
  });
  const int input_argc = static_cast<int>(input_argv.size());
  CommandLinePreprocessor cmd_line_pxr(input_argc, input_argv.data());

  EXPECT_EQ("foo", cmd_line_pxr.get_startup_url_for_test());
  EXPECT_EQ("foo", GetSwitchValue(cmd_line_pxr, cobalt::switches::kInitialURL));
}

TEST(CobaltSwitchDefaultsTest, StartupURLSwitchAndArg) {
  const auto input_argv =
      std::to_array<const char*>({"PROGRAM", "--url=foo", "bar"});
  const int input_argc = static_cast<int>(input_argv.size());
  CommandLinePreprocessor cmd_line_pxr(input_argc, input_argv.data());

  EXPECT_EQ("bar", cmd_line_pxr.get_startup_url_for_test());
  EXPECT_EQ("bar", GetSwitchValue(cmd_line_pxr, cobalt::switches::kInitialURL));
}

TEST(CobaltSwitchDefaultsTest, StartupURLArg) {
  const auto input_argv = std::to_array<const char*>({"PROGRAM", "data:,"});
  const int input_argc = static_cast<int>(input_argv.size());
  CommandLinePreprocessor cmd_line_pxr(input_argc, input_argv.data());

  EXPECT_EQ("data:,", cmd_line_pxr.get_startup_url_for_test());
  EXPECT_EQ("data:,",
            GetSwitchValue(cmd_line_pxr, cobalt::switches::kInitialURL));
}

TEST(CobaltSwitchDefaultsTest, StartupURLDefault) {
  const auto input_argv = std::to_array<const char*>({"PROGRAM"});
  const int input_argc = static_cast<int>(input_argv.size());
  CommandLinePreprocessor cmd_line_pxr(input_argc, input_argv.data());

  EXPECT_EQ("https://www.youtube.com/tv",
            cmd_line_pxr.get_startup_url_for_test());
  EXPECT_EQ("", GetSwitchValue(cmd_line_pxr, cobalt::switches::kInitialURL));
}

}  // namespace
}  // namespace cobalt
