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

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.StringJoiner;
import org.chromium.base.BuildInfo;
import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.json.JSONObject;

/** Defines the constant names for feature switches used in Kimono. */
@JNINamespace("cobalt")
public class JavaSwitches {
  private static final String TAG = "JavaSwitches";

  /** Default command line constants launched from A/B experiments. */
  public static final String DEFAULT_DISABLE_QUIC = "--disable-quic";

  public static final String DEFAULT_INITIAL_OLD_SPACE_SIZE = "64";
  public static final String DEFAULT_MAX_OLD_SPACE_SIZE = "512";
  public static final String DEFAULT_FORCE_GPU_MEM_AVAILABLE_MB = "64";

  public static final String ENABLE_QUIC = "EnableQUIC";

  /**
   * Java switch key set via Intent or Android metadata bundle to enable Starboard lifecycle
   * migration.
   */
  public static final String USE_STARBOARD_LIFECYCLE = "UseStarboardLifeCycle";

  /**
   * Command-line switch name passed to CommandLine when USE_STARBOARD_LIFECYCLE is set. Allows C++
   * code and non-Activity Java classes (e.g. NetworkStatus) to query CommandLine.
   */
  public static final String USE_STARBOARD_LIFECYCLE_SWITCH = "use-starboard-lifecycle";

  public static final String DISABLE_STARTUP_GUARD = "DisableStartupGuard";
  public static final String STARTUP_GUARD_INTERVAL_IN_SECONDS = "StartupGuardIntervalInSeconds";

  /** flag to enable auto-retrying URL load on network recovery before splash screen is hidden. */
  public static final String ENABLE_AUTO_RETRY_ON_NETWORK_RECOVERY =
      "EnableAutoRetryOnNetworkRecovery";

  /** flag to enable deferred V8 bytecode serialization in background/idle */
  public static final String DEFER_V8_CODE_CACHE_WRITE = "DeferV8CodeCacheWrite";

  /** flag to allow caching CSS and WebAssembly resources in the HTTP disk cache. */
  public static final String ENABLE_CSS_AND_WASM_FOR_HTTP_CACHE = "EnableCssAndWasmForHttpCache";

  /** flag to enable aggressive HTTP disk cache and V8 generated code cache tuning exclusions. */
  public static final String ENABLE_HTTP_AND_V8_CACHE_TUNING = "EnableHttpAndV8CacheTuning";

  /** flag to re-enable freeze and resume events */
  public static final String ENABLE_FREEZE = "EnableFreeze";

  public static final String USE_MINOR_MS_FOR_MINOR_GC = "UseMinorMSForMinorGC";

  /** flag to enable smart flushing for DOM storage (0ms delay and onStop flush). */
  public static final String ENABLE_DOM_STORAGE_SMART_FLUSHING = "EnableDomStorageSmartFlushing";

  /** flag to tune compositor offscreen interest area size in pixels. */
  public static final String INTEREST_AREA_SIZE_IN_PIXELS = "InterestAreaSizeInPixels";

  /** flag to tune delay in seconds before reclaiming prepaint tiles when idle. */
  public static final String RECLAIM_DELAY_IN_SECONDS = "ReclaimDelayInSeconds";

  /** flag to disable GPU memory buffer compositor resources. */
  public static final String DISABLE_GPU_MEMORY_BUFFER_COMPOSITOR_RESOURCES =
      "DisableGpuMemoryBufferCompositorResources";

  /** flag to enable the GPU shader disk cache. */
  public static final String ENABLE_GPU_SHADER_DISK_CACHE = "EnableGpuShaderDiskCache";

  /** flag to limit GPU image cache items */
  public static final String GPU_IMAGE_CACHE_LIMIT_ITEMS = "GpuImageCacheLimitItems";

  /** flag to limit GPU image cache bytes, resuing LimitImageDecodeCacheSizeMb */
  public static final String LIMIT_IMAGE_DECODE_CACHE_SIZE_MB = "LimitImageDecodeCacheSizeMb";

  /** flag to globally configure max HTTP cache size ceiling in bytes. */
  public static final String MAX_HTTP_CACHE_SIZE = "MaxHttpCacheSize";

  /** flag to limit GPU image cache working set budget bytes */
  public static final String DECODED_IMAGE_WORKING_SET_BUDGET_BYTES =
      "DecodedImageWorkingSetBudgetBytes";

  /** flag to allow scaling clipped images in GpuImageDecodeCache */
  public static final String ENABLE_SCALING_CLIPPED_IMAGES = "EnableScalingClippedImages";

  /** flag to enable dynamic mojo pipe sizing. */
  public static final String ENABLE_COBALT_DYNAMIC_MOJO_PIPE_SIZING =
      "EnableCobaltDynamicMojoPipeSizing";

  /** flag to tune cobalt dynamic mojo pipe sizing subresource size in bytes. */
  public static final String COBALT_DYNAMIC_MOJO_PIPE_SUBRESOURCE_SIZE =
      "CobaltDynamicMojoPipeSubresourceSize";

  /** flag to tune cobalt dynamic mojo pipe sizing media size in bytes. */
  public static final String COBALT_DYNAMIC_MOJO_PIPE_MEDIA_SIZE = "CobaltDynamicMojoPipeMediaSize";

  /** Avoid reuse resource. */
  public static final String AVOID_CC_REUSE_RESOURCE = "AvoidCCReuseResource";

  /** flag to bypass ResourceLoadScheduler subresource queueing and throttling. */
  public static final String COBALT_BYPASS_RESOURCE_LOAD_SCHEDULER =
      "CobaltBypassResourceLoadScheduler";

  /** flag to bypass Blink HTMLPreloadScanner and HTMLResourcePreloader. */
  public static final String COBALT_BYPASS_HTML_PRELOAD_SCANNER = "CobaltBypassHTMLPreloadScanner";

  /** flag to enable mmap-backed WOFF2 font decompression disk cache. */
  public static final String ENABLE_COBALT_MMAP_FONT_CACHE = "EnableCobaltMmapFontCache";

  /** flag to aggressively flush v8 bytecode after a configurable old time. */
  public static final String V8_SET_BYTECODE_OLD_TIME = "V8SetBytecodeOldTime";

  /** Flag to force SurfaceView for UI rendering (legacy fallback). */
  // We keep this fallback for emergency brake.
  // TODO: b/542337082 - Remove this after 09/17 (2-weeks after full-launch).
  public static final String SURFACE_VIEW_UI_RENDERING = "SurfaceViewUiRendering";

  public static final String V8_INITIAL_OLD_SPACE_SIZE = "V8InitialOldSpaceSize";
  public static final String V8_MAX_OLD_SPACE_SIZE = "V8MaxOldSpaceSize";

  /** flag to force GPU memory available in MB. */
  public static final String FORCE_GPU_MEM_AVAILABLE_MB = "ForceGpuMemAvailableMb";

  /** flag to enable area based buffer budget experiment. */
  public static final String AREA_BASED_VIDEO_BUFFER_BUDGET = "AreaBasedVideoBufferBudget";

  /** flag to allow critical memory pressure handling in foreground for V8. */
  public static final String ALLOW_CRITICAL_MEMORY_PRESSURE_HANDLING_IN_FOREGROUND =
      "AllowCriticalMemoryPressureHandlingInForeground";

  /** flag to evict blink memory cache on critical memory pressure. */
  public static final String EVICT_MEMORY_CACHE_ON_CRITICAL_MEMORY_PRESSURE =
      "EvictMemoryCacheOnCriticalMemoryPressure";

  /**
   * Flag to disable LessAggressiveParkableString feature to unpause foreground compression and use
   * a 2-second aging interval.
   */
  public static final String DISABLE_LESS_AGGRESSIVE_PARKABLE_STRING =
      "DisableLessAggressiveParkableString";

  /** Flag to disable BackForwardCache for WebContents. */
  public static final String DISABLE_BACK_FORWARD_CACHE = "DisableBackForwardCache";

  /** Flag to disable v8 baseline compiler sparkplug. */
  public static final String V8_DISABLE_SPARKPLUG = "V8DisableSparkplug";

  /** flag to enable moderate memory pressure handling on Android. */
  public static final String ENABLE_MODERATE_MEMORY_PRESSURE = "EnableModerateMemoryPressure";

  /** flag to configure memory pressure throttling cooldown in seconds. */
  public static final String MEMORY_PRESSURE_COOLDOWN_IN_SECONDS =
      "MemoryPressureCooldownInSeconds";

  /**
   * Flag to enable activity lifecycle coordination and safe surface teardown across overlapping
   * activities.
   */
  public static final String ENABLE_ACTIVITY_LIFECYCLE_COORDINATION =
      "EnableActivityLifecycleCoordination";

  private static Boolean sOverrideForTesting;

  public static void setOverrideForTesting(Boolean override) {
    sOverrideForTesting = override;
  }

  @CalledByNative
  public static boolean shouldApplyExperimentConfigs() {
    if (sOverrideForTesting != null) {
      return sOverrideForTesting;
    }
    // Read persisted crash streak and threshold directly from Variations beacon and
    // Experiment Config in the cache directory.
    // This allows Java to determine whether safe mode / empty config is active during early
    // startup before native singletons and libraries are initialized, with zero extra disk writes.
    try {
      if (ContextUtils.getApplicationContext() == null) {
        return true;
      }
      File cacheDir = ContextUtils.getApplicationContext().getCacheDir();
      if (cacheDir == null) {
        return true;
      }

      // Check the Variations beacon file first, where CleanExitBeacon synchronously records
      // exit state and crash streak on Android. Fall back to Metrics Config.
      File beaconFile = new File(cacheDir, CobaltPrefNames.VARIATIONS_BEACON_FILENAME);
      boolean isBeaconFormat = true;
      if (!beaconFile.exists()) {
        beaconFile = new File(cacheDir, CobaltPrefNames.METRICS_CONFIG_FILENAME);
        isBeaconFormat = false;
        if (!beaconFile.exists()) {
          return true;
        }
      }
      String content = readFileToString(beaconFile);
      if (content == null || content.isEmpty()) {
        return true;
      }

      JSONObject json = new JSONObject(content);
      int crashStreak = json.optInt(CobaltPrefNames.VARIATIONS_CRASH_STREAK, 0);

      boolean exitedCleanly = true;
      if (isBeaconFormat) {
        exitedCleanly = json.optBoolean(CobaltPrefNames.STABILITY_EXITED_CLEANLY, true);
      } else {
        JSONObject userExp = json.optJSONObject("user_experience_metrics");
        if (userExp != null) {
          JSONObject stability = userExp.optJSONObject("stability");
          if (stability != null) {
            exitedCleanly = stability.optBoolean("exited_cleanly", true);
          }
        }
      }

      // If the previous session crashed (did not exit cleanly), native C++
      // CleanExitBeacon::Initialize() will increment the crash streak on startup.
      // Java must account for this pending increment so that Java and C++ evaluate
      // the exact same threshold during early startup.
      if (!exitedCleanly) {
        crashStreak++;
      }

      int threshold = readCrashStreakEmptyConfigThreshold(cacheDir);
      if (crashStreak >= threshold) {
        return false;
      }
    } catch (Exception e) {
      Log.w(TAG, "Failed to read crash streak or experiment config from disk", e);
    }
    return true;
  }

  private static int readCrashStreakEmptyConfigThreshold(File cacheDir) {
    File expFile = new File(cacheDir, CobaltPrefNames.EXPERIMENT_CONFIG_FILENAME);
    if (!expFile.exists()) {
      return CobaltCrashStreakThreshold.DEFAULT_CRASH_STREAK_EMPTY_CONFIG_THRESHOLD;
    }
    String expContent = readFileToString(expFile);
    if (expContent == null || expContent.isEmpty()) {
      return CobaltCrashStreakThreshold.DEFAULT_CRASH_STREAK_EMPTY_CONFIG_THRESHOLD;
    }
    try {
      JSONObject expJson = new JSONObject(expContent);
      JSONObject finchParams = expJson.optJSONObject(CobaltExperimentNames.FINCH_PARAMETERS);
      if (finchParams == null) {
        return CobaltCrashStreakThreshold.DEFAULT_CRASH_STREAK_EMPTY_CONFIG_THRESHOLD;
      }
      return finchParams.optInt(
          CobaltExperimentNames.CRASH_STREAK_EMPTY_CONFIG_THRESHOLD,
          CobaltCrashStreakThreshold.DEFAULT_CRASH_STREAK_EMPTY_CONFIG_THRESHOLD);
    } catch (Exception e) {
      return CobaltCrashStreakThreshold.DEFAULT_CRASH_STREAK_EMPTY_CONFIG_THRESHOLD;
    }
  }

  private static String readFileToString(File file) {
    StringBuilder sb = new StringBuilder();
    try (BufferedReader reader =
        new BufferedReader(
            new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
      String line;
      while ((line = reader.readLine()) != null) {
        sb.append(line);
      }
      return sb.toString();
    } catch (Exception e) {
      return null;
    }
  }

  public static List<String> getDefaultCommandLineArgs() {
    List<String> defaultArgs = new ArrayList<>();
    defaultArgs.add(DEFAULT_DISABLE_QUIC);
    if (!"arm64".equals(BuildInfo.getArch()) && !"x86_64".equals(BuildInfo.getArch())) {
      defaultArgs.add("--force-gpu-mem-available-mb=" + DEFAULT_FORCE_GPU_MEM_AVAILABLE_MB);
    }
    defaultArgs.add(
        "--js-flags=--initial-old-space-size="
            + DEFAULT_INITIAL_OLD_SPACE_SIZE
            + ";--max-old-space-size="
            + DEFAULT_MAX_OLD_SPACE_SIZE);
    return defaultArgs;
  }

  public static List<String> getExtraCommandLineArgs(Map<String, String> javaSwitches) {
    return getExtraCommandLineArgs(javaSwitches, shouldApplyExperimentConfigs());
  }

  public static List<String> getExtraCommandLineArgs(
      Map<String, String> javaSwitches, boolean shouldApplyExperimentConfigs) {
    if (!shouldApplyExperimentConfigs) {
      return getDefaultCommandLineArgs();
    }

    if (javaSwitches == null) {
      javaSwitches = Collections.emptyMap();
    }

    List<String> extraCommandLineArgs = new ArrayList<>();
    StringJoiner jsFlags = new StringJoiner(";");

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_DOM_STORAGE_SMART_FLUSHING)) {
      extraCommandLineArgs.add("--enable-features=DomStorageSmartFlushing");
    }

    if (!javaSwitches.containsKey(JavaSwitches.ENABLE_QUIC)) {
      extraCommandLineArgs.add(DEFAULT_DISABLE_QUIC);
    }

    if (javaSwitches.containsKey(JavaSwitches.USE_MINOR_MS_FOR_MINOR_GC)) {
      jsFlags.add("--minor-ms");
      jsFlags.add("--minor-ms-min-new-space-capacity-for-concurrent-marking-mb=0");
    }

    String oldTime = getSanitizedNumericValue(javaSwitches, JavaSwitches.V8_SET_BYTECODE_OLD_TIME);
    if (oldTime != null) {
      jsFlags.add("--flush-bytecode");
      jsFlags.add("--bytecode-old-time=" + oldTime);
    }

    String initialOldSpace =
        getSanitizedNumericValue(javaSwitches, JavaSwitches.V8_INITIAL_OLD_SPACE_SIZE);
    if (initialOldSpace != null) {
      jsFlags.add("--initial-old-space-size=" + initialOldSpace);
    } else {
      jsFlags.add("--initial-old-space-size=" + DEFAULT_INITIAL_OLD_SPACE_SIZE);
    }

    if (javaSwitches.containsKey(JavaSwitches.V8_DISABLE_SPARKPLUG)) {
      jsFlags.add("--no-sparkplug");
    }

    String maxOldSpace = getSanitizedNumericValue(javaSwitches, JavaSwitches.V8_MAX_OLD_SPACE_SIZE);
    if (maxOldSpace != null) {
      jsFlags.add("--max-old-space-size=" + maxOldSpace);
    } else {
      jsFlags.add("--max-old-space-size=" + DEFAULT_MAX_OLD_SPACE_SIZE);
    }

    String forceGpuMem =
        getSanitizedNumericValue(javaSwitches, JavaSwitches.FORCE_GPU_MEM_AVAILABLE_MB);
    if (forceGpuMem != null) {
      extraCommandLineArgs.add("--force-gpu-mem-available-mb=" + forceGpuMem);
    } else if (!"arm64".equals(BuildInfo.getArch()) && !"x86_64".equals(BuildInfo.getArch())) {
      extraCommandLineArgs.add(
          "--force-gpu-mem-available-mb=" + DEFAULT_FORCE_GPU_MEM_AVAILABLE_MB);
    }

    if (javaSwitches.containsKey(JavaSwitches.DISABLE_GPU_MEMORY_BUFFER_COMPOSITOR_RESOURCES)) {
      extraCommandLineArgs.add("--disable-gpu-memory-buffer-compositor-resources");
    }

    String limit = getSanitizedNumericValue(javaSwitches, JavaSwitches.GPU_IMAGE_CACHE_LIMIT_ITEMS);
    if (limit != null) {
      extraCommandLineArgs.add("--cc-image-cache-limit-items=" + limit);
    }

    String decodeLimit =
        getSanitizedNumericValue(javaSwitches, JavaSwitches.LIMIT_IMAGE_DECODE_CACHE_SIZE_MB);
    if (decodeLimit != null) {
      extraCommandLineArgs.add("--cc-image-cache-limit-mbs=" + decodeLimit);
    }

    String budget =
        getSanitizedNumericValue(javaSwitches, JavaSwitches.DECODED_IMAGE_WORKING_SET_BUDGET_BYTES);
    if (budget != null) {
      extraCommandLineArgs.add("--decoded-image-working-set-budget-bytes=" + budget);
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_SCALING_CLIPPED_IMAGES)) {
      extraCommandLineArgs.add("--enable-scaling-clipped-images");
    }

    StringJoiner mojoPipeParams = new StringJoiner("/");
    String subresourceSize =
        getSanitizedNumericValue(
            javaSwitches, JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_SUBRESOURCE_SIZE);
    if (subresourceSize != null) {
      mojoPipeParams.add("subresource_size/" + subresourceSize);
    }

    String mediaSize =
        getSanitizedNumericValue(javaSwitches, JavaSwitches.COBALT_DYNAMIC_MOJO_PIPE_MEDIA_SIZE);
    if (mediaSize != null) {
      mojoPipeParams.add("media_size/" + mediaSize);
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_COBALT_DYNAMIC_MOJO_PIPE_SIZING)
        || mojoPipeParams.length() > 0) {
      if (mojoPipeParams.length() > 0) {
        extraCommandLineArgs.add(
            "--enable-features=CobaltDynamicMojoPipeSizing:" + mojoPipeParams.toString());
      } else {
        extraCommandLineArgs.add("--enable-features=CobaltDynamicMojoPipeSizing");
      }
    }

    StringJoiner featureParams = new StringJoiner("/");
    String interestAreaSize =
        getSanitizedNumericValue(javaSwitches, JavaSwitches.INTEREST_AREA_SIZE_IN_PIXELS);
    if (interestAreaSize != null) {
      featureParams.add("size_in_pixels/" + interestAreaSize);
    }

    String reclaimDelay =
        getSanitizedNumericValue(javaSwitches, JavaSwitches.RECLAIM_DELAY_IN_SECONDS);
    if (reclaimDelay != null) {
      featureParams.add("reclaim_delay_s/" + reclaimDelay);
    }

    if (featureParams.length() > 0) {
      extraCommandLineArgs.add("--enable-features=SmallerInterestArea:" + featureParams.toString());
    }

    if (javaSwitches.containsKey(JavaSwitches.DEFER_V8_CODE_CACHE_WRITE)) {
      extraCommandLineArgs.add("--defer-v8-code-cache-write");
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_GPU_SHADER_DISK_CACHE)) {
      extraCommandLineArgs.add("--enable-gpu-shader-disk-cache");
    }

    String maxHttpCacheSize =
        getSanitizedNumericValue(javaSwitches, JavaSwitches.MAX_HTTP_CACHE_SIZE);
    if (maxHttpCacheSize != null) {
      extraCommandLineArgs.add("--max-http-cache-size=" + maxHttpCacheSize);
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_CSS_AND_WASM_FOR_HTTP_CACHE)) {
      extraCommandLineArgs.add("--enable-css-and-wasm-for-http-cache");
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_HTTP_AND_V8_CACHE_TUNING)) {
      extraCommandLineArgs.add("--enable-http-and-v8-cache-tuning");
    }

    if (jsFlags.length() > 0) {
      extraCommandLineArgs.add("--js-flags=" + jsFlags.toString());
    }

    if (javaSwitches.containsKey(JavaSwitches.AVOID_CC_REUSE_RESOURCE)) {
      extraCommandLineArgs.add("--avoid-cc-reuse-resource");
    }

    if (javaSwitches.containsKey(JavaSwitches.COBALT_BYPASS_RESOURCE_LOAD_SCHEDULER)) {
      extraCommandLineArgs.add(
          "--enable-features=" + JavaSwitches.COBALT_BYPASS_RESOURCE_LOAD_SCHEDULER);
    }

    if (javaSwitches.containsKey(JavaSwitches.COBALT_BYPASS_HTML_PRELOAD_SCANNER)) {
      extraCommandLineArgs.add(
          "--enable-features=" + JavaSwitches.COBALT_BYPASS_HTML_PRELOAD_SCANNER);
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_COBALT_MMAP_FONT_CACHE)) {
      extraCommandLineArgs.add("--enable-features=CobaltMmapFontCache");
    }

    if (javaSwitches.containsKey(JavaSwitches.SURFACE_VIEW_UI_RENDERING)) {
      extraCommandLineArgs.add("--use-surface-view-for-ui");
    }

    if (javaSwitches.containsKey(JavaSwitches.AREA_BASED_VIDEO_BUFFER_BUDGET)) {
      extraCommandLineArgs.add("--enable-features=AreaBasedVideoBufferBudget");
    }

    if (javaSwitches.containsKey(
        JavaSwitches.ALLOW_CRITICAL_MEMORY_PRESSURE_HANDLING_IN_FOREGROUND)) {
      extraCommandLineArgs.add("--allow-critical-memory-pressure-handling-in-foreground");
    }

    if (javaSwitches.containsKey(JavaSwitches.EVICT_MEMORY_CACHE_ON_CRITICAL_MEMORY_PRESSURE)) {
      extraCommandLineArgs.add(
          "--enable-features=" + JavaSwitches.EVICT_MEMORY_CACHE_ON_CRITICAL_MEMORY_PRESSURE);
    }

    if (javaSwitches.containsKey(JavaSwitches.DISABLE_LESS_AGGRESSIVE_PARKABLE_STRING)) {
      extraCommandLineArgs.add("--disable-features=LessAggressiveParkableString");
    }

    if (javaSwitches.containsKey(JavaSwitches.DISABLE_BACK_FORWARD_CACHE)) {
      extraCommandLineArgs.add("--disable-back-forward-cache");
    }

    List<String> enabledMemoryPressureFeatures = new ArrayList<>();
    if (javaSwitches.containsKey(JavaSwitches.ENABLE_MODERATE_MEMORY_PRESSURE)) {
      enabledMemoryPressureFeatures.add("CobaltEnableModerateMemoryPressure");
    }
    if (javaSwitches.containsKey(JavaSwitches.MEMORY_PRESSURE_COOLDOWN_IN_SECONDS)) {
      String cooldown =
          javaSwitches.get(JavaSwitches.MEMORY_PRESSURE_COOLDOWN_IN_SECONDS);
      if (cooldown != null) {
        String cooldownVal = cooldown.replaceAll("[^0-9]", "");
        if (!cooldownVal.isEmpty()) {
          enabledMemoryPressureFeatures.add(
              "CobaltMemoryPressureCooldown:cooldown-seconds/" + cooldownVal);
        }
      }
    }
    if (!enabledMemoryPressureFeatures.isEmpty()) {
      extraCommandLineArgs.add(
          "--enable-features=" + String.join(",", enabledMemoryPressureFeatures));
    }

    // Convert the Java switch to a command-line flag so C++ code and non-Activity Java components
    // (such as NetworkStatus) can query
    // CommandLine.getInstance().hasSwitch("use-starboard-lifecycle").
    if (javaSwitches.containsKey(JavaSwitches.USE_STARBOARD_LIFECYCLE)) {
      extraCommandLineArgs.add("--" + USE_STARBOARD_LIFECYCLE_SWITCH);
    }

    if (javaSwitches.containsKey(JavaSwitches.ENABLE_ACTIVITY_LIFECYCLE_COORDINATION)) {
      extraCommandLineArgs.add("--enable-activity-lifecycle-coordination");
    }

    return extraCommandLineArgs;
  }

  private static String getSanitizedNumericValue(Map<String, String> javaSwitches, String key) {
    if (javaSwitches == null) {
      return null;
    }
    String val = javaSwitches.get(key);
    if (val == null) {
      return null;
    }
    String digits = val.replaceAll("[^0-9]", "");
    return digits.isEmpty() ? null : digits;
  }
}
