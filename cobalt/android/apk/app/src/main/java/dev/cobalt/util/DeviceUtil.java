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

import android.content.Context;
import android.util.DisplayMetrics;
import androidx.annotation.VisibleForTesting;
import org.chromium.base.ContextUtils;
import org.chromium.base.SysUtils;

/**
 * Utility methods for querying device hardware capabilities and constraints.
 *
 * <p>This class is used to determine hardware-based configurations at runtime, such as adjusting
 * the UI scale factor on low-memory devices.
 *
 * <p>This is a non-instantiable utility class with no instance lifetime or ownership; its static
 * helper methods exist for the lifetime of the application.
 *
 * <p>This class is thread-safe and its static methods can be called from any thread.
 */
public final class DeviceUtil {
  private DeviceUtil() {} // Prevent instantiation.

  // Physical RAM threshold for 1GB RAM devices in KB (1024 * 1024 KB).
  public static final int ONE_GB_RAM_THRESHOLD_KB = 1024 * 1024;

  // Threshold (in pixels) for the larger display dimension to qualify as at least 1080p.
  // 1600 is chosen as the midpoint between 720p (1280px) and 1080p (1920px). Using the
  // maximum dimension is orientation-independent (landscape vs. portrait) and provides
  // a safe margin for system decor (e.g. navigation/status bars) reducing available pixels.
  public static final int DISPLAY_1080P_MIN_MAX_DIMENSION_PX = 1600;

  private static Boolean sIs1GbDeviceForTesting;
  private static Boolean sIsDisplayAtLeast1080pForTesting;

  @VisibleForTesting
  public static void setIs1GbDeviceForTesting(Boolean is1Gb) {
    sIs1GbDeviceForTesting = is1Gb;
  }

  @VisibleForTesting
  public static void setIsDisplayAtLeast1080pForTesting(Boolean is1080p) {
    sIsDisplayAtLeast1080pForTesting = is1080p;
  }

  @VisibleForTesting
  public static void resetForTesting() {
    sIs1GbDeviceForTesting = null;
    sIsDisplayAtLeast1080pForTesting = null;
  }

  /**
   * Returns true if the device has 1 GB RAM or less.
   *
   * <p>Note: SysUtils.isLowEndDevice() cannot be used because Cobalt enables
   * --enable-low-end-device-mode by default, which causes isLowEndDevice() to return true for all
   * devices. We query SysUtils.amountOfPhysicalMemoryKB() directly instead.
   */
  public static boolean is1GbDevice() {
    if (sIs1GbDeviceForTesting != null) {
      return sIs1GbDeviceForTesting;
    }
    int physicalRamKb = SysUtils.amountOfPhysicalMemoryKB();
    return physicalRamKb > 0 && physicalRamKb <= ONE_GB_RAM_THRESHOLD_KB;
  }

  /** Returns true if the display resolution is at least 1080p. */
  public static boolean isDisplayAtLeast1080p() {
    if (sIsDisplayAtLeast1080pForTesting != null) {
      return sIsDisplayAtLeast1080pForTesting;
    }
    try {
      Context context = ContextUtils.getApplicationContext();
      if (context != null) {
        DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        return metrics != null
            && Math.max(metrics.widthPixels, metrics.heightPixels)
                >= DISPLAY_1080P_MIN_MAX_DIMENSION_PX;
      }
    } catch (Throwable t) {
      // Ignored if context is not available.
    }
    return false;
  }
}
