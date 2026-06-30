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
import org.chromium.components.thinwebview.CompositorView;
import org.chromium.components.thinwebview.CompositorViewFactory;
import org.chromium.components.thinwebview.ThinWebViewConstraints;
import android.view.View;
import android.view.ViewGroup;
import org.chromium.ui.base.ActivityWindowAndroid;
import org.chromium.ui.base.IntentRequestTracker;
import static dev.cobalt.util.Log.TAG;

/**
 * A simple Activity to host the Video Picture-in-Picture window on Android TV.
 * This Activity is launched by C++ CobaltVideoOverlayWindow and immediately
 * enters PiP mode.
 */
@JNINamespace("cobalt")
public class CobaltPictureInPictureActivity extends Activity {
  private long mNativeCobaltVideoOverlayWindow = 0;
  private CompositorView mCompositorView;
  private ActivityWindowAndroid mWindowAndroid;

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

    mWindowAndroid = new ActivityWindowAndroid(
        this, /* listenToActivityState= */ true,
        IntentRequestTracker.createFromActivity(this), null, /* trackOcclusion= */ false);

    mCompositorView = CompositorViewFactory.create(this, mWindowAndroid, new ThinWebViewConstraints());
    addContentView(mCompositorView.getView(),
        new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

    mCompositorView.getView().addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
      @Override
      public void onLayoutChange(View v, int left, int top, int right, int bottom,
                                 int oldLeft, int oldTop, int oldRight, int oldBottom) {
        if (mNativeCobaltVideoOverlayWindow == 0) return;
        if (top == bottom || left == right) return;
        CobaltPictureInPictureActivityJni.get().onViewSizeChanged(
            mNativeCobaltVideoOverlayWindow, right - left, bottom - top);
      }
    });

    if (mNativeCobaltVideoOverlayWindow != 0) {
      CobaltPictureInPictureActivityJni.get().compositorViewCreated(
          mNativeCobaltVideoOverlayWindow, mCompositorView);
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
  public WindowAndroid getWindowAndroid() {
    return mWindowAndroid;
  }

  @CalledByNative
  public void closeActivity() {
    mNativeCobaltVideoOverlayWindow = 0;
    finish();
  }

  @Override
  protected void onDestroy() {
    super.onDestroy();
    if (mCompositorView != null) {
      mCompositorView.destroy();
      mCompositorView = null;
    }
    if (mWindowAndroid != null) {
      mWindowAndroid.destroy();
      mWindowAndroid = null;
    }
    if (mNativeCobaltVideoOverlayWindow != 0) {
      CobaltPictureInPictureActivityJni.get().onActivityDestroyed(mNativeCobaltVideoOverlayWindow);
      mNativeCobaltVideoOverlayWindow = 0;
    }
  }

  @NativeMethods
  interface Interface {
    void setJavaActivity(long nativeCobaltVideoOverlayWindow, CobaltPictureInPictureActivity caller);
    void onActivityDestroyed(long nativeCobaltVideoOverlayWindow);
    void onViewSizeChanged(long nativeCobaltVideoOverlayWindow, int width, int height);
    void compositorViewCreated(long nativeCobaltVideoOverlayWindow, CompositorView compositorView);
  }
}
