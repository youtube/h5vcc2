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

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.StringJoiner;

/**
 * Defines the constant names for feature switches used in Kimono.
 */
public class JavaSwitches {
  public static final String ENABLE_QUIC = "EnableQUIC";
  public static final String DISABLE_STARTUP_GUARD = "DisableStartupGuard";
  public static final String STARTUP_GUARD_INTERVAL_IN_SECONDS = "StartupGuardIntervalInSeconds";
  public static final String DISABLE_HTTP_CACHE = "DisableHttpCache";

  /** flag to re-enable freeze and resume events */
  public static final String ENABLE_FREEZE = "EnableFreeze";

  /** flag to enable a 1.5s delay before firing the freeze event on background. */
  public static final String DELAY_FREEZE_ON_BACKGROUND = "DelayFreezeOnBackground";

  /** flag to force use IPv4 for system host resolution. */
  public static final String USE_IPV4_FOR_DNS = "UseIPv4ForDNS";

  public static final String USE_MINOR_MS_FOR_MINOR_GC = "UseMinorMSForMinorGC";

  /** flag to delete stale leveldb LOCK file on startup. */
  public static final String LOCAL_STORAGE_DELETE_LOCK_FILE = "LocalStorageDeleteLockFile";

  /** flag to tune compositor offscreen interest area size in pixels. */
  public static final String INTEREST_AREA_SIZE_IN_PIXELS = "InterestAreaSizeInPixels";

  /** flag to tune delay in seconds before reclaiming prepaint tiles when idle. */
  public static final String RECLAIM_DELAY_IN_SECONDS = "ReclaimDelayInSeconds";

  /** flag to disable GPU memory buffer compositor resources. */
  public static final String DISABLE_GPU_MEMORY_BUFFER_COMPOSITOR_RESOURCES =
      "DisableGpuMemoryBufferCompositorResources";

  /** flag to disable v8 optimizing compilers (turbofan, maglev, sparkplug) */
  public static final String DISABLE_V8_OPTIMIZING_COMPILERS = "DisableV8OptimizingCompilers";

  /** flag to enable concurrent marking for v8 garbage collection */
  public static final String ENABLE_V8_CONCURRENT_MARKING = "EnableV8ConcurrentMarking";

  /** flag to limit GPU image cache items */
  public static final String GPU_IMAGE_CACHE_LIMIT_ITEMS = "GpuImageCacheLimitItems";

  /** flag to limit GPU image cache working set budget bytes */
  public static final String DECODED_IMAGE_WORKING_SET_BUDGET_BYTES = "DecodedImageWorkingSetBudgetBytes";

  /** flag to allow scaling clipped images in GpuImageDecodeCache */
  public static final String ENABLE_SCALING_CLIPPED_IMAGES = "EnableScalingClippedImages";

  /** flag to enable dynamic mojo pipe sizing. */
  public static final String ENABLE_COBALT_DYNAMIC_MOJO_PIPE_SIZING =
      "EnableCobaltDynamicMojoPipeSizing";

  /** flag to tune cobalt dynamic mojo pipe sizing subresource size in bytes. */
  public static final String COBALT_DYNAMIC_MOJO_PIPE_SUBRESOURCE_SIZE =
      "CobaltDynamicMojoPipeSubresourceSize";

  /** flag to disable FontSrcLocalMatching lookup table. */
  public static final String DISABLE_FONT_SRC_LOCAL_MATCHING =
      "DisableFontSrcLocalMatching";

  public static List<String> getExtraCommandLineArgs(Map<String, String> javaSwitches) {
    List<String> extraCommandLineArgs = new ArrayList<>();
    StringJoiner jsFlags = new StringJoiner(";");

    if (javaSwitches.containsKey(JavaSwitches.USE_IPV4_FOR_DNS)) {
      extraCommandLineArgs.add("--enable-features=UseIPv4ForDNS");
    }

    if (javaSwitches.containsKey(JavaSwitches.LOCAL_STORAGE_DELETE_LOCK_FILE)) {
      extraCommandLineArgs.add("--enable-features=LocalStorageDeleteLockFile");
    }

    if (!javaSwitches.containsKey(JavaSwitches.ENABLE_QUIC)) {
      extraCommandLineArgs.add("--disable-quic");
    }

    if (javaSwitches.containsKey(JavaSwitches.DISABLE_HTTP_CACHE)) {
      extraCommandLineArgs.add("--disable-http-cache");
    }

    if (javaSwitches.containsKey(JavaSwitches.DISABLE_V8_OPTIMIZING_COMPILERS)) {
      jsFlags.add("--disable-optimizing-compilers");
      jsFlags.add("--no-sparkplug");
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_V8_CONCURRENT_MARKING)) {
      jsFlags.add("--concurrent-marking");
    }

    if (javaSwitches.containsKey(JavaSwitches.USE_MINOR_MS_FOR_MINOR_GC)) {
      jsFlags.add("--minor-ms");
      jsFlags.add("--minor-ms-min-new-space-capacity-for-concurrent-marking-mb=0");
    }

    if (javaSwitches.containsKey(JavaSwitches.DISABLE_GPU_MEMORY_BUFFER_COMPOSITOR_RESOURCES)) {
      extraCommandLineArgs.add("--disable-gpu-memory-buffer-compositor-resources");
    }

    if (javaSwitches.containsKey(JavaSwitches.GPU_IMAGE_CACHE_LIMIT_ITEMS)) {
      String limit = javaSwitches.get(JavaSwitches.GPU_IMAGE_CACHE_LIMIT_ITEMS).replaceAll("[^0-9]", "");
      extraCommandLineArgs.add("--cc-image-cache-limit-items=" + limit);
    }
    if (javaSwitches.containsKey(JavaSwitches.DECODED_IMAGE_WORKING_SET_BUDGET_BYTES)) {
      String budget = javaSwitches.get(JavaSwitches.DECODED_IMAGE_WORKING_SET_BUDGET_BYTES).replaceAll("[^0-9]", "");
      extraCommandLineArgs.add("--decoded-image-working-set-budget-bytes=" + budget);
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_SCALING_CLIPPED_IMAGES)) {
      extraCommandLineArgs.add("--enable-scaling-clipped-images");
    }

    StringJoiner mojoPipeParams = new StringJoiner("/");
    if (javaSwitches.containsKey(JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_SUBRESOURCE_SIZE)) {
      String size = javaSwitches.get(JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_SUBRESOURCE_SIZE).replaceAll("[^0-9]", "");
      mojoPipeParams.add("subresource_size/" + size);
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_COBALT_DYNAMIC_MOJO_PIPE_SIZING) || mojoPipeParams.length() > 0) {
      if (mojoPipeParams.length() > 0) {
        extraCommandLineArgs.add("--enable-features=CobaltDynamicMojoPipeSizing:" + mojoPipeParams.toString());
      } else {
        extraCommandLineArgs.add("--enable-features=CobaltDynamicMojoPipeSizing");
      }
    }

    StringJoiner featureParams = new StringJoiner("/");
    if (javaSwitches.containsKey(JavaSwitches.INTEREST_AREA_SIZE_IN_PIXELS)) {
      String size = javaSwitches.get(JavaSwitches.INTEREST_AREA_SIZE_IN_PIXELS).replaceAll("[^0-9]", "");
      featureParams.add("size_in_pixels/" + size);
    }

    if (javaSwitches.containsKey(JavaSwitches.RECLAIM_DELAY_IN_SECONDS)) {
      String delay = javaSwitches.get(JavaSwitches.RECLAIM_DELAY_IN_SECONDS).replaceAll("[^0-9]", "");
      featureParams.add("reclaim_delay_s/" + delay);
    }

    if (featureParams.length() > 0) {
      extraCommandLineArgs.add(
          "--enable-features=SmallerInterestArea:" + featureParams.toString());
    }

    if (javaSwitches.containsKey(JavaSwitches.DISABLE_FONT_SRC_LOCAL_MATCHING)) {
      extraCommandLineArgs.add("--disable-features=FontSrcLocalMatching");
    }

    if (jsFlags.length() > 0 ) {
      extraCommandLineArgs.add("--js-flags=" + jsFlags.toString());
    }

    return extraCommandLineArgs;
  }
}
