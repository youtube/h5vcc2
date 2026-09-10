/*
 * Copyright 2022 The Cobalt Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package dev.cobalt.coat;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.MockitoAnnotations.initMocks;

import android.app.Activity;
import android.widget.FrameLayout;
import dev.cobalt.shell.ContentViewRenderView;
import dev.cobalt.shell.Shell;
import dev.cobalt.shell.ShellManager;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InOrder;
import org.mockito.Mock;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import org.robolectric.util.ReflectionHelpers;

/** Tests for the ShellManager. */
@RunWith(RobolectricTestRunner.class)
@Config(
    shadows = {},
    manifest = Config.NONE)
public class ShellManagerTest {
  @Mock private ShellManager.Natives mMockShellManagerNatives;

  private Activity mActivity;
  private ShellManager mShellManager;
  private FrameLayout mRootView;
  @Mock private ContentViewRenderView mMockContentViewRenderView;

  @Before
  public void setUp() {
    initMocks(this);
    ShellManager.setNativesForTesting(mMockShellManagerNatives);
    mActivity = Robolectric.buildActivity(Activity.class).create().get();
    mRootView = new FrameLayout(mActivity);
    mActivity.setContentView(mRootView);
    mShellManager = new ShellManager(mActivity);
  }

  @Test
  public void testSetContentViewRenderView() {
    Shell shell = new Shell(mActivity);
    shell.setRootViewForTesting(mRootView);
    shell.setContentViewRenderView(mMockContentViewRenderView);

    assertEquals(1, mRootView.getChildCount());
    assertEquals(mMockContentViewRenderView, mRootView.getChildAt(0));

    shell.setContentViewRenderView(null);

    assertEquals(0, mRootView.getChildCount());
  }

  @Test
  public void testDestroyClosesActiveShellBeforeReleasingNativeManager() {
    Shell mockShell = mock(Shell.class);
    ReflectionHelpers.setField(mShellManager, "mActiveShell", mockShell);

    mShellManager.destroy();

    InOrder order = inOrder(mockShell, mMockShellManagerNatives);
    order.verify(mockShell).setContentViewRenderView(null);
    order.verify(mockShell).close();
    order.verify(mMockShellManagerNatives).destroy(mShellManager);
    assertNull(ReflectionHelpers.getField(mShellManager, "mActiveShell"));
    assertNull(ReflectionHelpers.getField(mShellManager, "mContext"));
    assertNull(mShellManager.getContext());
    assertNull(mShellManager.getWindow());
  }

  @Test
  public void testDestroyHandlesBeingCalledTwice() {
    Shell mockShell = mock(Shell.class);
    ReflectionHelpers.setField(mShellManager, "mActiveShell", mockShell);

    mShellManager.destroy();
    mShellManager.destroy();

    verify(mockShell, times(1)).close();
    verify(mMockShellManagerNatives, times(1)).destroy(mShellManager);
  }

  @Test
  public void testNativeCloseReenteringDestroyDoesNotCloseTwice() {
    Shell shell = mock(Shell.class);
    ReflectionHelpers.setField(mShellManager, "mActiveShell", shell);
    doAnswer(
            invocation -> {
              mShellManager.destroy();
              return null;
            })
        .when(shell)
        .close();

    mShellManager.destroy();

    verify(shell, times(1)).close();
    verify(mMockShellManagerNatives, times(1)).destroy(mShellManager);
    assertNull(mShellManager.getActiveShell());
  }

  @Test
  public void testDestroyReleasesRenderViewBeforeClosingLastNativeShell() {
    Shell shell = mock(Shell.class);
    ReflectionHelpers.setField(mShellManager, "mActiveShell", shell);
    ReflectionHelpers.setField(mShellManager, "mContentViewRenderView", mMockContentViewRenderView);

    mShellManager.destroy();

    InOrder order = inOrder(mMockContentViewRenderView, shell);
    order.verify(mMockContentViewRenderView).destroy();
    order.verify(shell).close();
    assertNull(mShellManager.getContentViewRenderView());
  }

  @Test
  public void testDestroyedManagerRejectsWindowReattachment() {
    mShellManager.destroy();
    assertThrows(
        IllegalStateException.class,
        () -> mShellManager.setWindow(mock(org.chromium.ui.base.WindowAndroid.class)));
    assertNull(mShellManager.getWindow());
  }

  @Test
  public void testDestroyedManagerRejectsLateLaunchWithoutRetainingCallback() {
    mShellManager.destroy();
    Shell.OnWebContentsReadyListener callback = mock(Shell.OnWebContentsReadyListener.class);
    assertThrows(
        IllegalStateException.class, () -> mShellManager.launchShell("about:blank", "", callback));
    assertNull(ReflectionHelpers.getField(mShellManager, "mNextWebContentsReadyListener"));
  }

  @Test
  public void testOldActivityDestroyDoesNotCloseReplacementShell() {
    Shell oldShell = mock(Shell.class);
    Shell newShell = mock(Shell.class);
    ReflectionHelpers.setField(mShellManager, "mActiveShell", oldShell);
    ShellManager replacement = new ShellManager(mActivity);
    ReflectionHelpers.setField(replacement, "mActiveShell", newShell);

    mShellManager.destroy();

    verify(oldShell).close();
    verify(newShell, org.mockito.Mockito.never()).close();
    assertEquals(newShell, replacement.getActiveShell());
    replacement.destroy();
    verify(newShell).close();
  }
}
