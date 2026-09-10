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

package dev.cobalt.util;

import static com.google.common.truth.Truth.assertThat;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.chromium.base.ContextUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;

/** Unit tests for {@link JavaSwitches}. */
@RunWith(RobolectricTestRunner.class)
public class JavaSwitchesTest {

  @Before
  public void setUp() {
    ContextUtils.initApplicationContextForTests(RuntimeEnvironment.getApplication());
    clearConfigFiles();
    JavaSwitches.setOverrideForTesting(null);
  }

  @After
  public void tearDown() {
    JavaSwitches.setOverrideForTesting(null);
    clearConfigFiles();
  }

  private void clearConfigFiles() {
    if (ContextUtils.getApplicationContext() != null) {
      File cacheDir = ContextUtils.getApplicationContext().getCacheDir();
      if (cacheDir != null) {
        new File(cacheDir, CobaltPrefNames.VARIATIONS_BEACON_FILENAME).delete();
        new File(cacheDir, CobaltPrefNames.METRICS_CONFIG_FILENAME).delete();
        new File(cacheDir, CobaltPrefNames.EXPERIMENT_CONFIG_FILENAME).delete();
      }
    }
  }

  private void writeVariationsBeacon(int crashStreak, boolean exitedCleanly) throws IOException {
    File cacheDir = ContextUtils.getApplicationContext().getCacheDir();
    File file = new File(cacheDir, CobaltPrefNames.VARIATIONS_BEACON_FILENAME);
    String json =
        "{\""
            + CobaltPrefNames.VARIATIONS_CRASH_STREAK
            + "\":"
            + crashStreak
            + ",\""
            + CobaltPrefNames.STABILITY_EXITED_CLEANLY
            + "\":"
            + exitedCleanly
            + "}";
    try (FileOutputStream fos = new FileOutputStream(file)) {
      fos.write(json.getBytes(StandardCharsets.UTF_8));
    }
  }

  private void writeMetricsConfig(int crashStreak) throws IOException {
    File cacheDir = ContextUtils.getApplicationContext().getCacheDir();
    File file = new File(cacheDir, CobaltPrefNames.METRICS_CONFIG_FILENAME);
    String json = "{\"" + CobaltPrefNames.VARIATIONS_CRASH_STREAK + "\":" + crashStreak + "}";
    try (FileOutputStream fos = new FileOutputStream(file)) {
      fos.write(json.getBytes(StandardCharsets.UTF_8));
    }
  }

  private void writeExperimentConfigWithThreshold(int threshold) throws IOException {
    File cacheDir = ContextUtils.getApplicationContext().getCacheDir();
    File file = new File(cacheDir, CobaltPrefNames.EXPERIMENT_CONFIG_FILENAME);
    String json =
        "{\""
            + CobaltExperimentNames.FINCH_PARAMETERS
            + "\":{\""
            + CobaltExperimentNames.CRASH_STREAK_EMPTY_CONFIG_THRESHOLD
            + "\":"
            + threshold
            + "}}";
    try (FileOutputStream fos = new FileOutputStream(file)) {
      fos.write(json.getBytes(StandardCharsets.UTF_8));
    }
  }

  @Test
  public void testShouldApplyExperimentConfigs_DefaultWhenNotInitialized() {
    // When override is not set and no config files exist, defaults to true.
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isTrue();
  }

  @Test
  public void testShouldApplyExperimentConfigs_LowCrashStreak() throws IOException {
    writeMetricsConfig(1);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isTrue();
  }

  @Test
  public void testShouldApplyExperimentConfigs_HighCrashStreak_DefaultThreshold()
      throws IOException {
    writeMetricsConfig(CobaltCrashStreakThreshold.DEFAULT_CRASH_STREAK_EMPTY_CONFIG_THRESHOLD - 1);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isTrue();

    writeMetricsConfig(CobaltCrashStreakThreshold.DEFAULT_CRASH_STREAK_EMPTY_CONFIG_THRESHOLD);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isFalse();

    writeMetricsConfig(CobaltCrashStreakThreshold.DEFAULT_CRASH_STREAK_EMPTY_CONFIG_THRESHOLD + 1);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isFalse();
  }

  @Test
  public void testShouldApplyExperimentConfigs_CustomThresholdFromExperimentConfig()
      throws IOException {
    // Custom threshold is 2. Crash streak 2 triggers safe mode.
    writeExperimentConfigWithThreshold(2);
    writeMetricsConfig(1);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isTrue();

    writeMetricsConfig(2);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isFalse();
  }

  @Test
  public void testShouldApplyExperimentConfigs_CustomThresholdHigher() throws IOException {
    // Custom threshold is 5. Crash streak 3 does not trigger safe mode.
    writeExperimentConfigWithThreshold(5);
    writeMetricsConfig(3);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isTrue();

    writeMetricsConfig(5);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isFalse();
  }

  @Test
  public void testShouldApplyExperimentConfigs_VariationsBeacon_CleanExit_BelowThreshold()
      throws IOException {
    // Stored streak 3, clean exit -> effective streak 3 < threshold 4 -> true.
    writeVariationsBeacon(3, true);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isTrue();
  }

  @Test
  public void testShouldApplyExperimentConfigs_VariationsBeacon_DirtyExit_ReachesThreshold()
      throws IOException {
    // Stored streak 3, dirty exit (crash) -> pending increment 3 + 1 = 4 >= threshold 4 -> false.
    writeVariationsBeacon(3, false);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isFalse();
  }

  @Test
  public void testShouldApplyExperimentConfigs_VariationsBeacon_DirtyExit_BelowThreshold()
      throws IOException {
    // Stored streak 2, dirty exit (crash) -> pending increment 2 + 1 = 3 < threshold 4 -> true.
    writeVariationsBeacon(2, false);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isTrue();
  }

  @Test
  public void testShouldApplyExperimentConfigs_VariationsBeaconTakesPrecedenceOverMetricsConfig()
      throws IOException {
    // Metrics Config says 0, but Variations beacon says 4 (threshold reached).
    writeMetricsConfig(0);
    writeVariationsBeacon(4, true);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isFalse();
  }

  @Test
  public void testShouldApplyExperimentConfigs_MalformedJson() throws IOException {
    File cacheDir = ContextUtils.getApplicationContext().getCacheDir();
    File file = new File(cacheDir, CobaltPrefNames.METRICS_CONFIG_FILENAME);
    try (FileOutputStream fos = new FileOutputStream(file)) {
      fos.write("invalid json content".getBytes(StandardCharsets.UTF_8));
    }
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isTrue();
  }

  @Test
  public void testShouldApplyExperimentConfigs_WithOverride() {
    JavaSwitches.setOverrideForTesting(true);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isTrue();

    JavaSwitches.setOverrideForTesting(false);
    assertThat(JavaSwitches.shouldApplyExperimentConfigs()).isFalse();
  }

  @Test
  public void testGetExtraCommandLineArgs_NullSwitches() {
    List<String> args = JavaSwitches.getExtraCommandLineArgs(null);
    assertThat(args).contains("--disable-quic");
    assertThat(args).contains("--js-flags=--initial-old-space-size=64;--max-old-space-size=512");
  }

  @Test
  public void testGetExtraCommandLineArgs_EmptySwitches() {
    List<String> args = JavaSwitches.getExtraCommandLineArgs(new HashMap<>());
    assertThat(args).contains("--disable-quic");
    assertThat(args).contains("--js-flags=--initial-old-space-size=64;--max-old-space-size=512");
  }

  @Test
  public void testGetExtraCommandLineArgs_ExperimentsAllowed_AppliesAllConfigs() {
    Map<String, String> switches = new HashMap<>();
    switches.put(JavaSwitches.ENABLE_DOM_STORAGE_SMART_FLUSHING, "1");
    switches.put(JavaSwitches.ENABLE_QUIC, "1");
    switches.put(JavaSwitches.USE_MINOR_MS_FOR_MINOR_GC, "1");
    switches.put(JavaSwitches.V8_SET_BYTECODE_OLD_TIME, "10");
    switches.put(JavaSwitches.V8_INITIAL_OLD_SPACE_SIZE, "128");
    switches.put(JavaSwitches.V8_DISABLE_SPARKPLUG, "1");
    switches.put(JavaSwitches.V8_MAX_OLD_SPACE_SIZE, "1024");
    switches.put(JavaSwitches.FORCE_GPU_MEM_AVAILABLE_MB, "256");
    switches.put(JavaSwitches.DISABLE_GPU_MEMORY_BUFFER_COMPOSITOR_RESOURCES, "1");
    switches.put(JavaSwitches.GPU_IMAGE_CACHE_LIMIT_ITEMS, "500");
    switches.put(JavaSwitches.LIMIT_IMAGE_DECODE_CACHE_SIZE_MB, "32");
    switches.put(JavaSwitches.DECODED_IMAGE_WORKING_SET_BUDGET_BYTES, "1000000");
    switches.put(JavaSwitches.ENABLE_SCALING_CLIPPED_IMAGES, "1");
    switches.put(JavaSwitches.ENABLE_COBALT_DYNAMIC_MOJO_PIPE_SIZING, "1");
    switches.put(JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_SUBRESOURCE_SIZE, "1024");
    switches.put(JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_MEDIA_SIZE, "2048");
    switches.put(JavaSwitches.INTEREST_AREA_SIZE_IN_PIXELS, "400");
    switches.put(JavaSwitches.RECLAIM_DELAY_IN_SECONDS, "5");
    switches.put(JavaSwitches.DEFER_V8_CODE_CACHE_WRITE, "1");
    switches.put(JavaSwitches.ENABLE_GPU_SHADER_DISK_CACHE, "1");
    switches.put(JavaSwitches.MAX_HTTP_CACHE_SIZE, "50000000");
    switches.put(JavaSwitches.ENABLE_CSS_AND_WASM_FOR_HTTP_CACHE, "1");
    switches.put(JavaSwitches.ENABLE_HTTP_AND_V8_CACHE_TUNING, "1");
    switches.put(JavaSwitches.AVOID_CC_REUSE_RESOURCE, "1");
    switches.put(JavaSwitches.COBALT_BYPASS_RESOURCE_LOAD_SCHEDULER, "1");
    switches.put(JavaSwitches.COBALT_BYPASS_HTML_PRELOAD_SCANNER, "1");
    switches.put(JavaSwitches.ENABLE_COBALT_MMAP_FONT_CACHE, "1");
    switches.put(JavaSwitches.SURFACE_VIEW_UI_RENDERING, "1");
    switches.put(JavaSwitches.AREA_BASED_VIDEO_BUFFER_BUDGET, "1");
    switches.put(JavaSwitches.ALLOW_CRITICAL_MEMORY_PRESSURE_HANDLING_IN_FOREGROUND, "1");
    switches.put(JavaSwitches.EVICT_MEMORY_CACHE_ON_CRITICAL_MEMORY_PRESSURE, "1");
    switches.put(JavaSwitches.DISABLE_LESS_AGGRESSIVE_PARKABLE_STRING, "1");
    switches.put(JavaSwitches.DISABLE_BACK_FORWARD_CACHE, "1");

    JavaSwitches.setOverrideForTesting(true);
    List<String> args = JavaSwitches.getExtraCommandLineArgs(switches);

    assertThat(args).doesNotContain("--disable-quic");
    assertThat(args).contains("--enable-features=DomStorageSmartFlushing");
    assertThat(args).contains("--disable-gpu-memory-buffer-compositor-resources");
    assertThat(args).contains("--force-gpu-mem-available-mb=256");
    assertThat(args).contains("--cc-image-cache-limit-items=500");
    assertThat(args).contains("--cc-image-cache-limit-mbs=32");
    assertThat(args).contains("--decoded-image-working-set-budget-bytes=1000000");
    assertThat(args).contains("--enable-scaling-clipped-images");
    assertThat(args)
        .contains(
            "--enable-features=CobaltDynamicMojoPipeSizing:subresource_size/1024/media_size/2048");
    assertThat(args)
        .contains("--enable-features=SmallerInterestArea:size_in_pixels/400/reclaim_delay_s/5");
    assertThat(args).contains("--defer-v8-code-cache-write");
    assertThat(args).contains("--enable-gpu-shader-disk-cache");
    assertThat(args).contains("--max-http-cache-size=50000000");
    assertThat(args).contains("--enable-css-and-wasm-for-http-cache");
    assertThat(args).contains("--enable-http-and-v8-cache-tuning");
    assertThat(args).contains("--avoid-cc-reuse-resource");
    assertThat(args).contains("--enable-features=CobaltBypassResourceLoadScheduler");
    assertThat(args).contains("--enable-features=CobaltBypassHTMLPreloadScanner");
    assertThat(args).contains("--enable-features=CobaltMmapFontCache");
    assertThat(args).contains("--use-surface-view-for-ui");
    assertThat(args).contains("--enable-features=AreaBasedVideoBufferBudget");
    assertThat(args).contains("--allow-critical-memory-pressure-handling-in-foreground");
    assertThat(args).contains("--enable-features=EvictMemoryCacheOnCriticalMemoryPressure");
    assertThat(args).contains("--disable-features=LessAggressiveParkableString");
    assertThat(args).contains("--disable-back-forward-cache");

    // Check js-flags
    boolean foundJsFlags = false;
    for (String arg : args) {
      if (arg.startsWith("--js-flags=")) {
        foundJsFlags = true;
        assertThat(arg).contains("--minor-ms");
        assertThat(arg).contains("--minor-ms-min-new-space-capacity-for-concurrent-marking-mb=0");
        assertThat(arg).contains("--flush-bytecode");
        assertThat(arg).contains("--bytecode-old-time=10");
        assertThat(arg).contains("--initial-old-space-size=128");
        assertThat(arg).contains("--no-sparkplug");
        assertThat(arg).contains("--max-old-space-size=1024");
      }
    }
    assertThat(foundJsFlags).isTrue();
  }

  @Test
  public void testGetExtraCommandLineArgs_ExperimentsNotAllowed_DisablesAllExperiments() {
    Map<String, String> switches = new HashMap<>();
    switches.put(JavaSwitches.ENABLE_DOM_STORAGE_SMART_FLUSHING, "1");
    switches.put(JavaSwitches.ENABLE_QUIC, "1");
    switches.put(JavaSwitches.USE_MINOR_MS_FOR_MINOR_GC, "1");
    switches.put(JavaSwitches.V8_SET_BYTECODE_OLD_TIME, "10");
    switches.put(JavaSwitches.V8_INITIAL_OLD_SPACE_SIZE, "128");
    switches.put(JavaSwitches.V8_DISABLE_SPARKPLUG, "1");
    switches.put(JavaSwitches.V8_MAX_OLD_SPACE_SIZE, "1024");
    switches.put(JavaSwitches.FORCE_GPU_MEM_AVAILABLE_MB, "256");
    switches.put(JavaSwitches.DISABLE_GPU_MEMORY_BUFFER_COMPOSITOR_RESOURCES, "1");
    switches.put(JavaSwitches.GPU_IMAGE_CACHE_LIMIT_ITEMS, "500");
    switches.put(JavaSwitches.LIMIT_IMAGE_DECODE_CACHE_SIZE_MB, "32");
    switches.put(JavaSwitches.DECODED_IMAGE_WORKING_SET_BUDGET_BYTES, "1000000");
    switches.put(JavaSwitches.ENABLE_SCALING_CLIPPED_IMAGES, "1");
    switches.put(JavaSwitches.ENABLE_COBALT_DYNAMIC_MOJO_PIPE_SIZING, "1");
    switches.put(JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_SUBRESOURCE_SIZE, "1024");
    switches.put(JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_MEDIA_SIZE, "2048");
    switches.put(JavaSwitches.INTEREST_AREA_SIZE_IN_PIXELS, "400");
    switches.put(JavaSwitches.RECLAIM_DELAY_IN_SECONDS, "5");
    switches.put(JavaSwitches.DEFER_V8_CODE_CACHE_WRITE, "1");
    switches.put(JavaSwitches.ENABLE_GPU_SHADER_DISK_CACHE, "1");
    switches.put(JavaSwitches.MAX_HTTP_CACHE_SIZE, "50000000");
    switches.put(JavaSwitches.ENABLE_CSS_AND_WASM_FOR_HTTP_CACHE, "1");
    switches.put(JavaSwitches.ENABLE_HTTP_AND_V8_CACHE_TUNING, "1");
    switches.put(JavaSwitches.AVOID_CC_REUSE_RESOURCE, "1");
    switches.put(JavaSwitches.COBALT_BYPASS_RESOURCE_LOAD_SCHEDULER, "1");
    switches.put(JavaSwitches.COBALT_BYPASS_HTML_PRELOAD_SCANNER, "1");
    switches.put(JavaSwitches.ENABLE_COBALT_MMAP_FONT_CACHE, "1");
    switches.put(JavaSwitches.SURFACE_VIEW_UI_RENDERING, "1");
    switches.put(JavaSwitches.AREA_BASED_VIDEO_BUFFER_BUDGET, "1");
    switches.put(JavaSwitches.ALLOW_CRITICAL_MEMORY_PRESSURE_HANDLING_IN_FOREGROUND, "1");
    switches.put(JavaSwitches.EVICT_MEMORY_CACHE_ON_CRITICAL_MEMORY_PRESSURE, "1");
    switches.put(JavaSwitches.DISABLE_LESS_AGGRESSIVE_PARKABLE_STRING, "1");
    switches.put(JavaSwitches.DISABLE_BACK_FORWARD_CACHE, "1");

    JavaSwitches.setOverrideForTesting(false);
    List<String> args = JavaSwitches.getExtraCommandLineArgs(switches);

    // Default QUIC is disabled when experiments are not allowed
    assertThat(args).contains("--disable-quic");

    // None of the experiment features or switches should be present
    assertThat(args).doesNotContain("--enable-features=DomStorageSmartFlushing");
    assertThat(args).doesNotContain("--disable-gpu-memory-buffer-compositor-resources");
    assertThat(args).doesNotContain("--force-gpu-mem-available-mb=256");
    assertThat(args).doesNotContain("--cc-image-cache-limit-items=500");
    assertThat(args).doesNotContain("--cc-image-cache-limit-mbs=32");
    assertThat(args).doesNotContain("--decoded-image-working-set-budget-bytes=1000000");
    assertThat(args).doesNotContain("--enable-scaling-clipped-images");
    assertThat(args).doesNotContain("--defer-v8-code-cache-write");
    assertThat(args).doesNotContain("--enable-gpu-shader-disk-cache");
    assertThat(args).doesNotContain("--max-http-cache-size=50000000");
    assertThat(args).doesNotContain("--enable-css-and-wasm-for-http-cache");
    assertThat(args).doesNotContain("--enable-http-and-v8-cache-tuning");
    assertThat(args).doesNotContain("--avoid-cc-reuse-resource");
    assertThat(args).doesNotContain("--use-surface-view-for-ui");
    assertThat(args).doesNotContain("--allow-critical-memory-pressure-handling-in-foreground");
    assertThat(args).doesNotContain("--disable-back-forward-cache");

    for (String arg : args) {
      assertThat(arg).doesNotContain("CobaltDynamicMojoPipeSizing");
      assertThat(arg).doesNotContain("SmallerInterestArea");
      assertThat(arg).doesNotContain("CobaltBypassResourceLoadScheduler");
      assertThat(arg).doesNotContain("CobaltBypassHTMLPreloadScanner");
      assertThat(arg).doesNotContain("CobaltMmapFontCache");
      assertThat(arg).doesNotContain("AreaBasedVideoBufferBudget");
      assertThat(arg).doesNotContain("EvictMemoryCacheOnCriticalMemoryPressure");
      assertThat(arg).doesNotContain("LessAggressiveParkableString");
    }

    // Default JS flags should still be present
    assertThat(args).contains("--js-flags=--initial-old-space-size=64;--max-old-space-size=512");
  }

  @Test
  public void testGetExtraCommandLineArgs_NullValuesInMap() {
    Map<String, String> switches = new HashMap<>();
    switches.put(JavaSwitches.V8_INITIAL_OLD_SPACE_SIZE, null);
    switches.put(JavaSwitches.V8_MAX_OLD_SPACE_SIZE, null);
    switches.put(JavaSwitches.FORCE_GPU_MEM_AVAILABLE_MB, null);
    switches.put(JavaSwitches.GPU_IMAGE_CACHE_LIMIT_ITEMS, null);
    switches.put(JavaSwitches.LIMIT_IMAGE_DECODE_CACHE_SIZE_MB, null);
    switches.put(JavaSwitches.DECODED_IMAGE_WORKING_SET_BUDGET_BYTES, null);
    switches.put(JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_SUBRESOURCE_SIZE, null);
    switches.put(JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_MEDIA_SIZE, null);
    switches.put(JavaSwitches.INTEREST_AREA_SIZE_IN_PIXELS, null);
    switches.put(JavaSwitches.RECLAIM_DELAY_IN_SECONDS, null);
    switches.put(JavaSwitches.MAX_HTTP_CACHE_SIZE, null);

    JavaSwitches.setOverrideForTesting(true);
    List<String> args = JavaSwitches.getExtraCommandLineArgs(switches);

    assertThat(args).contains("--disable-quic");
    assertThat(args).contains("--js-flags=--initial-old-space-size=64;--max-old-space-size=512");
  }

  @Test
  public void testGetExtraCommandLineArgs_NonNumericValuesInMap() {
    Map<String, String> switches = new HashMap<>();
    switches.put(JavaSwitches.V8_INITIAL_OLD_SPACE_SIZE, "invalid_non_numeric");
    switches.put(JavaSwitches.V8_MAX_OLD_SPACE_SIZE, "abc");
    switches.put(JavaSwitches.FORCE_GPU_MEM_AVAILABLE_MB, "none");
    switches.put(JavaSwitches.GPU_IMAGE_CACHE_LIMIT_ITEMS, "xyz");
    switches.put(JavaSwitches.MAX_HTTP_CACHE_SIZE, "unlimited");

    JavaSwitches.setOverrideForTesting(true);
    List<String> args = JavaSwitches.getExtraCommandLineArgs(switches);

    assertThat(args).contains("--disable-quic");
    assertThat(args).contains("--js-flags=--initial-old-space-size=64;--max-old-space-size=512");
    assertThat(args).doesNotContain("--force-gpu-mem-available-mb=");
    assertThat(args).doesNotContain("--cc-image-cache-limit-items=");
    assertThat(args).doesNotContain("--max-http-cache-size=");
  }
}
