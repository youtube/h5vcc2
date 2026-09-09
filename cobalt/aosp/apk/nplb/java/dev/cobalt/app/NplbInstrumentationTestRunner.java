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

package dev.cobalt.app;

import android.os.Bundle;
import org.chromium.build.gtest_apk.NativeTestInstrumentationTestRunner;

/**
 * Instrumentation entry point for nplb.apk. Reuses Chromium's {@link
 * NativeTestInstrumentationTestRunner} and defaults the launched activity to {@link NplbActivity}.
 */
public class NplbInstrumentationTestRunner extends NativeTestInstrumentationTestRunner {
  private static final String EXTRA_NATIVE_TEST_ACTIVITY =
      "org.chromium.native_test.NativeTestInstrumentationTestRunner.NativeTestActivity";
  private static final String TEST_ACTIVITY = "dev.cobalt.app.NplbActivity";

  @Override
  public void onCreate(Bundle arguments) {
    arguments.putString(EXTRA_NATIVE_TEST_ACTIVITY, TEST_ACTIVITY);
    super.onCreate(arguments);
  }
}
