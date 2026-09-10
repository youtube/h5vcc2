// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

import android.app.Activity;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.hardware.display.DisplayManager.DisplayListener;
import android.os.Build;
import android.util.DisplayMetrics;
import android.util.Size;
import android.view.Display;
import android.view.WindowManager;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicInteger;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

/** Utility functions for querying display attributes. */
public class DisplayUtil {

  /** A simple wrapper for display DPI to allow JNI generation for its methods. */
  public static class DisplayDpi {
    private final float mX;
    private final float mY;

    public DisplayDpi(float xdpi, float ydpi) {
      this.mX = xdpi;
      this.mY = ydpi;
    }

    @CalledByNative("DisplayDpi")
    public float getX() {
      return mX;
    }

    @CalledByNative("DisplayDpi")
    public float getY() {
      return mY;
    }
  }

  private DisplayUtil() {}

  @JNINamespace("starboard")
  @NativeMethods
  interface Natives {
    void onDisplayChanged();
  }

  private static Display sDefaultDisplay;
  private static DisplayMetrics sCachedDisplayMetrics = null;

  public static final double DISPLAY_REFRESH_RATE_UNKNOWN = -1;

  /** Returns the physical pixels per inch of the screen in the X and Y dimensions. */
  @CalledByNative
  public static DisplayDpi getDisplayDpi() {
    DisplayMetrics metrics = getDisplayMetrics();
    return new DisplayDpi(metrics.xdpi, metrics.ydpi);
  }

  /** Return supported hdr types. */
  @CalledByNative
  @Nullable
  public static int[] getSupportedHdrTypes() {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
      return null;
    }
    Display display = getDefaultDisplay();
    if (display == null) {
      return null;
    }
    Display.HdrCapabilities hdrCapabilities = display.getHdrCapabilities();
    return hdrCapabilities != null ? hdrCapabilities.getSupportedHdrTypes() : null;
  }

  /** Returns the default display associated with a context. */
  @Nullable
  public static Display getDefaultDisplay() {
    synchronized (DisplayUtil.class) {
      if (sDefaultDisplay != null && !sDefaultDisplay.isValid()) {
        return null;
      }

      return sDefaultDisplay;
    }
  }

  /** Returns the refresh rate of the default display. */
  public static double getDefaultDisplayRefreshRate() {
    Display defaultDisplay = getDefaultDisplay();
    return defaultDisplay != null ? defaultDisplay.getRefreshRate() : DISPLAY_REFRESH_RATE_UNKNOWN;
  }

  /**
   * Cache the default display and display metrics, this will be triggered when a NativeActivity
   * starts.
   */
  public static void cacheDefaultDisplay(Context context) {
    Display display = getDisplayFromContext(context);
    synchronized (DisplayUtil.class) {
      sDefaultDisplay = display;
      sCachedDisplayMetrics = ((Activity) context).getResources().getDisplayMetrics();
    }
  }

  /**
   * Returns the default display associated with a context. When API Level >= 30, pass {@link
   * android.app.Activity} for the context parameter.
   */
  @Nullable
  public static Display getDisplayFromContext(Context context) {
    if (context == null) {
      return null;
    }
    if (android.os.Build.VERSION.SDK_INT >= 30) {
      return getDefaultDisplayV30(context);
    } else {
      return getDefaultDisplayDeprecated(context);
    }
  }

  @Nullable
  @SuppressWarnings("deprecation")
  private static Display getDefaultDisplayDeprecated(Context context) {
    WindowManager wm = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
    return (wm == null) ? null : wm.getDefaultDisplay();
  }

  @Nullable
  @RequiresApi(30)
  private static Display getDefaultDisplayV30(Context context) {
    return context.getDisplay();
  }

  /**
   * Returns the size of the physical display size in pixels.
   *
   * <p>This differs from {@link #getSystemDisplaySize()} because it only uses {@link
   * DisplayMetrics}.
   */
  public static Size getDisplaySize() {
    DisplayMetrics metrics = getDisplayMetrics();
    return new Size(metrics.widthPixels, metrics.heightPixels);
  }

  /**
   * Returns the size of the current physical display size in pixels.
   *
   * <p>This differs from {@link #getDisplaySize()} because it allows the system property
   * "sys.display-size" to override {@link DisplayMetrics}.
   */
  public static Size getSystemDisplaySize() {
    Size widthAndHeightPx = getSystemDisplayWidthAndHeightPxInternal();
    if (widthAndHeightPx == null) {
      widthAndHeightPx = getDisplaySize();
    }
    return widthAndHeightPx;
  }

  /**
   * Returns the size of the current physical display size in pixels. or {@code null} if
   * unavailable.
   */
  private static Size getSystemDisplayWidthAndHeightPxInternal() {
    final String displaySize = SystemPropertiesHelper.getString("sys.display-size");
    if (displaySize != null) {
      final String[] widthAndHeightPx = displaySize.split("x");
      if (widthAndHeightPx.length == 2) {
        try {
          return new Size(
              Integer.parseInt(widthAndHeightPx[0]), Integer.parseInt(widthAndHeightPx[1]));
        } catch (NumberFormatException exception) {
          // pass
        }
      }
    }
    return null;
  }

  private static DisplayMetrics getDisplayMetrics() {
    return sCachedDisplayMetrics;
  }

  public interface Listener {
    void onDisplayChanged(int displayId);
  }

  private static final CopyOnWriteArrayList<Listener> sListeners = new CopyOnWriteArrayList<>();

  public static void registerListener(Listener listener) {
    sListeners.add(listener);
  }

  public static void unregisterListener(Listener listener) {
    sListeners.remove(listener);
  }

  private static DisplayListener sDisplayerListener =
      new DisplayListener() {
        private void notifyListeners(int displayId) {
          for (Listener listener : sListeners) {
            listener.onDisplayChanged(displayId);
          }
        }

        @Override
        public void onDisplayAdded(int displayId) {
          DisplayUtilJni.get().onDisplayChanged();
          notifyListeners(displayId);
        }

        @Override
        public void onDisplayChanged(int displayId) {
          DisplayUtilJni.get().onDisplayChanged();
          notifyListeners(displayId);
        }

        @Override
        public void onDisplayRemoved(int displayId) {
          DisplayUtilJni.get().onDisplayChanged();
          notifyListeners(displayId);
        }
      };

  private static final AtomicInteger sListenerRefCount = new AtomicInteger(0);

  public static void addDisplayListener(Context context) {
    if (context == null) {
      return;
    }

    Context appContext = context.getApplicationContext();
    if (appContext == null) {
      appContext = context;
    }
    DisplayManager displayManager = appContext.getSystemService(DisplayManager.class);
    if (displayManager == null) {
      return;
    }
    if (sListenerRefCount.getAndIncrement() == 0) {
      displayManager.registerDisplayListener(sDisplayerListener, null);

      // Call nativeOnDisplayChanged() to reload supported hdr types here after a default
      // Display created.
      DisplayUtilJni.get().onDisplayChanged();
    }
  }

  public static void removeDisplayListener(Context context) {
    if (context == null) {
      return;
    }

    Context appContext = context.getApplicationContext();
    if (appContext == null) {
      appContext = context;
    }
    DisplayManager displayManager = appContext.getSystemService(DisplayManager.class);
    if (displayManager == null) {
      return;
    }
    int previousCount;
    do {
      previousCount = sListenerRefCount.get();
      if (previousCount == 0) {
        return;
      }
    } while (!sListenerRefCount.compareAndSet(previousCount, previousCount - 1));
    if (previousCount == 1) {
      displayManager.unregisterDisplayListener(sDisplayerListener);
    }
  }
}
