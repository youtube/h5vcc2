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

package dev.cobalt.util;

import static com.google.common.truth.Truth.assertThat;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;

/** Unit tests for {@link JavaSwitches}. */
@RunWith(RobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class JavaSwitchesTest {

  @Test
  public void testGetExtraCommandLineArgs_NullSwitches() {
    List<String> args = JavaSwitches.getExtraCommandLineArgs(null);
    assertThat(args).isNotNull();
    assertThat(args).contains("--disable-quic");
  }

  @Test
  public void testGetExtraCommandLineArgs_EmptySwitches() {
    Map<String, String> switches = new HashMap<>();
    List<String> args = JavaSwitches.getExtraCommandLineArgs(switches);
    assertThat(args).isNotNull();
    assertThat(args).contains("--disable-quic");
  }

  @Test
  public void testGetExtraCommandLineArgs_AppliesAllConfigs() {
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
  public void testGetExtraCommandLineArgs_NonNumericValuesInMap() {
    Map<String, String> switches = new HashMap<>();
    switches.put(JavaSwitches.V8_INITIAL_OLD_SPACE_SIZE, "invalid_128mb");
    switches.put(JavaSwitches.MAX_HTTP_CACHE_SIZE, "xyz");

    List<String> args = JavaSwitches.getExtraCommandLineArgs(switches);

    boolean foundJsFlags = false;
    for (String arg : args) {
      if (arg.startsWith("--js-flags=")) {
        foundJsFlags = true;
        assertThat(arg).contains("--initial-old-space-size=128");
      }
    }
    assertThat(foundJsFlags).isTrue();
    for (String arg : args) {
      assertThat(arg).doesNotContain("--max-http-cache-size=");
    }
  }

  @Test
  public void testGetExtraCommandLineArgs_NullValuesInMap() {
    Map<String, String> switches = new HashMap<>();
    switches.put(JavaSwitches.V8_INITIAL_OLD_SPACE_SIZE, null);
    switches.put(JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_SUBRESOURCE_SIZE, null);

    List<String> args = JavaSwitches.getExtraCommandLineArgs(switches);
    boolean foundJsFlags = false;
    for (String arg : args) {
      if (arg.startsWith("--js-flags=")) {
        foundJsFlags = true;
        assertThat(arg)
            .contains("--initial-old-space-size=" + JavaSwitches.DEFAULT_INITIAL_OLD_SPACE_SIZE);
      }
    }
    assertThat(foundJsFlags).isTrue();
  }
}
