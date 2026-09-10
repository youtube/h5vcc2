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

package dev.cobalt.coat;

import static com.google.common.truth.Truth.assertThat;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import org.chromium.base.CommandLine;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/** CommandLineOverrideHelperTest. */
@RunWith(RobolectricTestRunner.class)
public class CommandLineOverrideHelperTest {
  @Before
  public void setUp() {
    // Create a fresh CommandLine instance for each test.
    CommandLine.init(null);
  }

  @Test
  public void testDefaultCommandLineOverridesList() {
    List<String> overrides = CommandLineOverrideHelper.getDefaultCommandLineOverridesList();
    assertThat(overrides.contains("--enable-low-end-device-mode")).isTrue();
    assertThat(overrides.contains("--use-custom-android-fonts-xml")).isTrue();
  }

  @Test
  public void testDefaultJsFlagOverridesList() {
    String overrides = CommandLineOverrideHelper.getDefaultJsFlagOverridesList().toString();
    assertThat(overrides.contains("--optimize-for-size")).isTrue();
  }

  @Test
  public void testDefaultEnableFeatureOverridesList() {
    String overrides = CommandLineOverrideHelper.getDefaultEnableFeatureOverridesList().toString();
    assertThat(overrides.contains("LogJsConsoleMessages")).isTrue();
    assertThat(overrides.contains("LimitImageDecodeCacheSize:mb/24")).isTrue();
  }

  @Test
  public void testDefaultDisableFeatureOverridesList() {
    String overrides = CommandLineOverrideHelper.getDefaultDisableFeatureOverridesList().toString();
    assertThat(overrides.contains("PartitionAllocBackupRefPtr")).isTrue();
    assertThat(overrides.contains("UseAAudioInput")).isTrue();
    assertThat(overrides.contains("DeferAudioFocusUntilAudible")).isTrue();
  }

  @Test
  public void testDefaultBlinkEnableFeatureOverridesList() {
    String overrides =
        CommandLineOverrideHelper.getDefaultBlinkEnableFeatureOverridesList().toString();
    assertThat(overrides.contains("PreciseMemoryInfo")).isTrue();
  }

  @Test
  public void testFlagOverrides_EmptyArgs() {
    CommandLineOverrideHelper.getFlagOverrides(Collections.emptyList());

    Assert.assertTrue(CommandLine.getInstance().hasSwitch("single-process"));
    Assert.assertTrue(CommandLine.getInstance().hasSwitch("force-video-overlays"));
    Assert.assertTrue(CommandLine.getInstance().hasSwitch("enable-low-end-device-mode"));
    Assert.assertTrue(CommandLine.getInstance().hasSwitch("disable-rgba-4444-textures"));
    Assert.assertTrue(CommandLine.getInstance().hasSwitch("disable-accelerated-video-decode"));
    Assert.assertTrue(CommandLine.getInstance().hasSwitch("disable-accelerated-video-encode"));
    Assert.assertTrue(CommandLine.getInstance().hasSwitch("enable-zero-copy"));
    Assert.assertTrue(CommandLine.getInstance().hasSwitch("hide-scrollbars"));
    Assert.assertTrue(CommandLine.getInstance().hasSwitch("use-custom-android-fonts-xml"));

    String expected = "no-user-gesture-required";
    String actual = CommandLine.getInstance().getSwitchValue("autoplay-policy");
    Assert.assertEquals(expected, actual);

    expected = "1";
    actual = CommandLine.getInstance().getSwitchValue("force-device-scale-factor");
    Assert.assertEquals(expected, actual);

    actual = CommandLine.getInstance().getSwitchValue("enable-features");
    expected = CommandLineOverrideHelper.getDefaultEnableFeatureOverridesList().toString();
    Assert.assertEquals(expected, actual);

    actual = CommandLine.getInstance().getSwitchValue("disable-features");
    expected = CommandLineOverrideHelper.getDefaultDisableFeatureOverridesList().toString();
    Assert.assertEquals(expected, actual);

    actual = CommandLine.getInstance().getSwitchValue("enable-blink-features");
    expected = CommandLineOverrideHelper.getDefaultBlinkEnableFeatureOverridesList().toString();
    Assert.assertEquals(expected, actual);

    Assert.assertFalse(CommandLine.getInstance().hasSwitch("enable-h5vcc-settings"));
  }

  @Test
  public void testFlagOverrides_SingleArg() {
    List<String> commandLineArgs = Arrays.asList("--enable-features=TestFeature1;TestFeature2");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    String actual = CommandLine.getInstance().getSwitchValue("enable-features");
    String expected =
        CommandLineOverrideHelper.getDefaultEnableFeatureOverridesList().toString()
            + ",TestFeature1,TestFeature2";
    Assert.assertEquals(expected, actual);
  }

  @Test
  public void testFlagOverrides_MultipleArgs() {
    List<String> commandLineArgs =
        Arrays.asList(
            "--enable-features=TestFeature1;TestFeature2",
            "--disable-features=TestFeature3",
            "--js-flags=--test-flag;--another-flag",
            "--enable-h5vcc-settings=TestSetting1;TestSetting2");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    String enableFeatures = CommandLine.getInstance().getSwitchValue("enable-features");
    String expectedEnable =
        CommandLineOverrideHelper.getDefaultEnableFeatureOverridesList().toString()
            + ",TestFeature1,TestFeature2";
    Assert.assertEquals(expectedEnable, enableFeatures);

    String disableFeatures = CommandLine.getInstance().getSwitchValue("disable-features");
    String expectedDisable =
        CommandLineOverrideHelper.getDefaultDisableFeatureOverridesList().toString()
            + ",TestFeature3";
    Assert.assertEquals(expectedDisable, disableFeatures);

    String jsFlags = CommandLine.getInstance().getSwitchValue("js-flags");
    String expectedJs =
        CommandLineOverrideHelper.getDefaultJsFlagOverridesList().toString()
            + ",--test-flag,--another-flag";
    Assert.assertEquals(expectedJs, jsFlags);

    String h5vccSettings = CommandLine.getInstance().getSwitchValue("enable-h5vcc-settings");
    String expectedH5vcc = "TestSetting1;TestSetting2";
    Assert.assertEquals(expectedH5vcc, h5vccSettings);
  }

  @Test
  public void testFlagOverrides_WithRegularSwitch() {
    List<String> commandLineArgs = Arrays.asList("--some-other-switch=value");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    Assert.assertTrue(CommandLine.getInstance().hasSwitch("some-other-switch"));
    String actual = CommandLine.getInstance().getSwitchValue("some-other-switch");
    Assert.assertEquals("value", actual);
  }

  @Test
  public void testFlagOverrides_UserOverrideTakesPrecedence() {
    List<String> commandLineArgs =
        Arrays.asList(
            "--remote-allow-origins=https://chrome-devtools-frontend.appspot.com",
            "--remote-allow-origins=*");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    Assert.assertEquals("*", CommandLine.getInstance().getSwitchValue("remote-allow-origins"));
  }

  @Test
  public void testFlagOverrides_EmptyAndNullArgs() {
    List<String> commandLineArgs =
        Arrays.asList("--enable-features=TestFeature1;", null, "--disable-features=TestFeature2");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    String enableFeatures = CommandLine.getInstance().getSwitchValue("enable-features");
    String expectedEnable =
        CommandLineOverrideHelper.getDefaultEnableFeatureOverridesList().toString()
            + ",TestFeature1";
    Assert.assertEquals(expectedEnable, enableFeatures);

    String disableFeatures = CommandLine.getInstance().getSwitchValue("disable-features");
    String expectedDisable =
        CommandLineOverrideHelper.getDefaultDisableFeatureOverridesList().toString()
            + ",TestFeature2";
    Assert.assertEquals(expectedDisable, disableFeatures);
  }

  @Test
  public void testFlagOverrides_FeaturesWithValues() {
    List<String> commandLineArgs =
        Arrays.asList("--enable-features=TestFeature1=value1;TestFeature2=value2");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    String enableFeatures = CommandLine.getInstance().getSwitchValue("enable-features");
    String expectedEnable =
        CommandLineOverrideHelper.getDefaultEnableFeatureOverridesList().toString()
            + ",TestFeature1=value1,TestFeature2=value2";
    Assert.assertEquals(expectedEnable, enableFeatures);
  }

  @Test
  public void testFlagOverrides_EnableH5vccSettings() {
    List<String> commandLineArgs =
        Arrays.asList("--enable-h5vcc-settings=Setting1=val1;Setting2=val2");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    String h5vccSettings = CommandLine.getInstance().getSwitchValue("enable-h5vcc-settings");
    Assert.assertEquals("Setting1=val1;Setting2=val2", h5vccSettings);
  }

  @Test
  public void testFlagOverrides_TraceStartup() {
    List<String> commandLineArgs =
        Arrays.asList("--trace-startup=-*;disabled-by-default-memory-infra");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    String traceStartup = CommandLine.getInstance().getSwitchValue("trace-startup");
    Assert.assertEquals("-*,disabled-by-default-memory-infra", traceStartup);
  }

  @Test
  public void testFlagOverrides_TraceStartupEmpty() {
    List<String> commandLineArgs = Arrays.asList("--trace-startup");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    Assert.assertTrue(CommandLine.getInstance().hasSwitch("trace-startup"));
  }

  @Test
  public void testFlagOverrides_HeapProfilingAdbArgs() {
    List<String> commandLineArgs =
        Arrays.asList(
            "--enable-heap-profiling",
            "--memlog=all",
            "--memlog-stack-mode=native-with-thread-names",
            "--trace-startup=-*;disabled-by-default-memory-infra",
            "--trace-startup-duration=60",
            "--trace-startup-file=/sdcard/Download/trace_atv.pftrace");
    CommandLineOverrideHelper.getFlagOverrides(commandLineArgs);

    Assert.assertTrue(CommandLine.getInstance().hasSwitch("enable-heap-profiling"));
    Assert.assertEquals("all", CommandLine.getInstance().getSwitchValue("memlog"));
    Assert.assertEquals(
        "native-with-thread-names", CommandLine.getInstance().getSwitchValue("memlog-stack-mode"));
    Assert.assertEquals(
        "-*,disabled-by-default-memory-infra",
        CommandLine.getInstance().getSwitchValue("trace-startup"));
    Assert.assertEquals("60", CommandLine.getInstance().getSwitchValue("trace-startup-duration"));
    Assert.assertEquals(
        "/sdcard/Download/trace_atv.pftrace",
        CommandLine.getInstance().getSwitchValue("trace-startup-file"));
  }
}
