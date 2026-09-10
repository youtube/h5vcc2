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

package org.chromium.content_browsertests_apk;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.os.Build;
import java.util.Collections;
import org.chromium.components.crash.browser.ProcessExitReasonFromSystem;
import org.jni_zero.CalledByNativeForTesting;
import org.jni_zero.JNINamespace;
import org.mockito.ArgumentMatchers;
import org.mockito.Mockito;

/**
 * Helper to mock ActivityManager and simulate system ApplicationExitInfo exit reasons for Cobalt
 * browsertests.
 */
@JNINamespace("cobalt")
public class MockProcessExitReasonHelper {

  @CalledByNativeForTesting
  public static void setMockExitReasonForTesting(int pid, int reason) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
      return;
    }
    ApplicationExitInfo mockInfo = Mockito.mock(ApplicationExitInfo.class);
    Mockito.doReturn(pid).when(mockInfo).getPid();
    Mockito.doReturn(reason).when(mockInfo).getReason();

    ActivityManager mockAm = Mockito.mock(ActivityManager.class);
    Mockito.doReturn(Collections.singletonList(mockInfo))
        .when(mockAm)
        .getHistoricalProcessExitReasons(
            ArgumentMatchers.nullable(String.class),
            ArgumentMatchers.eq(pid),
            ArgumentMatchers.anyInt());

    ProcessExitReasonFromSystem.setActivityManagerForTest(mockAm);
  }

  @CalledByNativeForTesting
  public static void resetForTesting() {
    ProcessExitReasonFromSystem.setActivityManagerForTest(null);
  }
}
