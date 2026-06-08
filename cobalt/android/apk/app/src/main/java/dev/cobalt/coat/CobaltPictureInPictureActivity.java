// Copyright 2026 The Cobalt Authors. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dev.cobalt.coat;

import android.app.Activity;
import android.app.PictureInPictureParams;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.content.Context;
import android.util.Rational;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;
import org.chromium.base.ContextUtils;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;

/**
 * A simple Activity to host the Video Picture-in-Picture window on Android TV.
 * This Activity is launched by C++ CobaltVideoOverlayWindow and immediately
 * enters PiP mode.
 */
@JNINamespace("cobalt")
public class CobaltPictureInPictureActivity extends Activity {

  private static final String TAG = "CobaltPiPActivity";
  private long mNativeCobaltVideoOverlayWindow = 0;

  @CalledByNative
  public static void launchActivity(WebContents webContents, long nativePointer) {
    android.util.Log.i("CobaltPiP", "CoAT PiP Step 13: Reached Java launchActivity! nativePtr=" + nativePointer);
    Activity activity = null;
    WindowAndroid window = webContents.getTopLevelNativeWindow();
    if (window != null) {
      activity = window.getActivity().get();
    }
    Context context = (activity != null) ? activity : ContextUtils.getApplicationContext();

    Intent intent = new Intent(context, CobaltPictureInPictureActivity.class);
    intent.putExtra("native_pointer", nativePointer);
    intent.putExtra("web_contents", webContents);
    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    context.startActivity(intent);
  }

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

    Intent intent = getIntent();
    mNativeCobaltVideoOverlayWindow = intent.getLongExtra("native_pointer", 0);

    // Register this Java instance with the native C++ controller.
    if (mNativeCobaltVideoOverlayWindow != 0) {
      CobaltPictureInPictureActivityJni.get().setJavaActivity(
          mNativeCobaltVideoOverlayWindow, CobaltPictureInPictureActivity.this);
    }

    // Attempt to enter PiP mode immediately.
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      PictureInPictureParams params = new PictureInPictureParams.Builder()
          .setAspectRatio(new Rational(16, 9)) // Default aspect ratio
          .build();
      enterPictureInPictureMode(params);
    } else {
      // Fallback for older devices (should not happen on modern ATV)
      finish();
    }
  }

  @CalledByNative
  public void closeActivity() {
    finish();
  }

  @Override
  protected void onDestroy() {
    super.onDestroy();
    if (mNativeCobaltVideoOverlayWindow != 0) {
      CobaltPictureInPictureActivityJni.get().onActivityDestroyed(mNativeCobaltVideoOverlayWindow);
      mNativeCobaltVideoOverlayWindow = 0;
    }
  }

  @NativeMethods
  interface Interface {
    void setJavaActivity(long nativeCobaltVideoOverlayWindow, CobaltPictureInPictureActivity caller);
    void onActivityDestroyed(long nativeCobaltVideoOverlayWindow);
  }
}
