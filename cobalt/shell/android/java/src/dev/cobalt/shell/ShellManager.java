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

package dev.cobalt.shell;

import android.content.Context;
import org.chromium.base.ThreadUtils;
import org.chromium.build.annotations.Initializer;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.build.annotations.RequiresNonNull;
import org.chromium.content_public.browser.Visibility;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

/** Copied from org.chromium.content_shell.ShellManager. Container and generator of ShellViews. */
@JNINamespace("content")
@NullMarked
public class ShellManager {
  private @Nullable WindowAndroid mWindow;
  private @Nullable Shell mActiveShell;

  private Shell.@Nullable OnWebContentsReadyListener mNextWebContentsReadyListener;

  // The target for all content rendering.
  private @Nullable ContentViewRenderView mContentViewRenderView;

  private @Nullable Context mContext;

  private boolean mIsActivityVisible;
  private boolean mDestroyed;

  /** Constructor for inflating via XML. */
  public ShellManager(final Context context) {
    mContext = context;
    if (sNatives == null) {
      sNatives = ShellManagerJni.get();
    }
    sNatives.init(this);
  }

  public void onActivityVisible(boolean visible) {
    mIsActivityVisible = visible;
    if (mActiveShell != null) {
      mActiveShell.onActivityVisible(visible);
    }
  }

  public @Nullable Context getContext() {
    return mContext;
  }

  /**
   * @param window The window used to generate all shells.
   */
  @Initializer
  public void setWindow(WindowAndroid window) {
    if (mDestroyed) {
      throw new IllegalStateException("Cannot attach a window to a destroyed ShellManager");
    }
    Context context = mContext;
    if (context == null) {
      throw new IllegalStateException("ShellManager has no context");
    }
    assert window != null;
    mWindow = window;
    mContentViewRenderView = new ContentViewRenderView(context);
    mContentViewRenderView.onNativeLibraryLoaded(window);
  }

  /**
   * @return The window used to generate all shells.
   */
  public @Nullable WindowAndroid getWindow() {
    return mWindow;
  }

  /** Get the ContentViewRenderView. */
  public @Nullable ContentViewRenderView getContentViewRenderView() {
    return mContentViewRenderView;
  }

  /**
   * @return The currently visible shell view or null if one is not showing.
   */
  public @Nullable Shell getActiveShell() {
    return mActiveShell;
  }

  /**
   * Creates a new shell pointing to the specified URL.
   *
   * @param url The URL the shell should load upon creation.
   * @param deepLinkUrl The URL from the DeepLink URL.
   * @param listener The listener to be notified when WebContents is ready.
   */
  public void launchShell(
      String url, String deepLinkUrl, Shell.OnWebContentsReadyListener listener) {
    if (mDestroyed) {
      throw new IllegalStateException("Cannot launch a shell after ShellManager is destroyed");
    }
    ThreadUtils.assertOnUiThread();
    mNextWebContentsReadyListener = listener;
    Shell previousShell = mActiveShell;
    sNatives.launchShell(this, url, deepLinkUrl);
    if (previousShell != null) previousShell.close();
  }

  @CalledByNative
  private Object createShell(long nativeShellPtr) {
    Context context = getContext();
    WindowAndroid window = getWindow();
    if (context == null || window == null) {
      throw new IllegalStateException("Cannot create shell after ShellManager is destroyed");
    }
    if (mContentViewRenderView == null) {
      mContentViewRenderView = new ContentViewRenderView(context);
      mContentViewRenderView.onNativeLibraryLoaded(window);
    }

    Shell shellView = new Shell(context);
    shellView.initialize(nativeShellPtr, window);
    shellView.onActivityVisible(mIsActivityVisible);
    shellView.setWebContentsReadyListener(mNextWebContentsReadyListener);
    mNextWebContentsReadyListener = null;

    // TODO(tedchoc): Allow switching back to these inactive shells.
    if (mActiveShell != null) removeShell(mActiveShell);

    showShell(shellView);
    return shellView;
  }

  @RequiresNonNull("mContentViewRenderView")
  private void showShell(Shell shellView) {
    if (mActiveShell != null) {
      mActiveShell.setContentViewRenderView(null);
    }
    shellView.setContentViewRenderView(mContentViewRenderView);
    mActiveShell = shellView;
    WebContents webContents = mActiveShell.getWebContents();
    if (webContents != null) {
      mContentViewRenderView.setCurrentWebContents(webContents);
      if (mIsActivityVisible) {
        webContents.updateWebContentsVisibility(Visibility.VISIBLE);
      }
    }
  }

  @CalledByNative
  private void removeShell(Shell shellView) {
    if (shellView == mActiveShell) mActiveShell = null;
    shellView.setContentViewRenderView(null);
  }

  /**
   * Destroys the Shell manager and associated components. Always called at activity exit, and
   * potentially called by native in cases where we need to control the timing of
   * mContentViewRenderView destruction. Must handle being called twice.
   */
  @CalledByNative
  public void destroy() {
    if (mDestroyed) return;
    mDestroyed = true;

    // Remove active shell (Currently single shell support only available).
    Shell activeShell = mActiveShell;
    if (activeShell != null) {
      removeShell(activeShell);
    }
    if (mContentViewRenderView != null) {
      mContentViewRenderView.destroy();
      mContentViewRenderView = null;
    }
    // Release compositor resources before closing the last native Shell, which
    // can shut down the platform and reenter destroy(). Keep the manager's JNI
    // reference alive until the native Shell has finished its cleanup callbacks.
    if (activeShell != null) {
      activeShell.close();
    }
    sNatives.destroy(this);
    mNextWebContentsReadyListener = null;
    mWindow = null;
    mContext = null;
  }

  private static Natives sNatives;

  public static void setNativesForTesting(Natives natives) {
    sNatives = natives;
  }

  /** Interface for the native implementation of ShellManager. */
  @NativeMethods
  public interface Natives {
    /**
     * Creates the native ShellManager object.
     *
     * @param shellManagerInstance The Java instance of the ShellManager.
     */
    void init(Object shellManagerInstance);

    /**
     * Creates a new shell pointing to the specified URL.
     *
     * @param url The URL the shell should load upon creation.
     * @param deepLinkUrl The topic URL from the DeepLink URL.
     */
    void launchShell(Object shellManagerInstance, String url, String deepLinkUrl);

    /** Releases the native global reference to this ShellManager. */
    void destroy(Object shellManagerInstance);

    /**
     * Appends the migration status parameter to the given URL.
     *
     * @param url The URL to append the migration status to.
     * @return The updated URL.
     */
    String appendMigrationStatus(String url);
  }
}
