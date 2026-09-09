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
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import org.chromium.base.ContextUtils;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

/** Helper for verifying native-to-Java file path and content synchronization in browser tests. */
@JNINamespace("cobalt")
public class CobaltPrefNamesTestHelper {
  @CalledByNative
  public static String getCacheDirAbsolutePath() {
    Context context = ContextUtils.getApplicationContext();
    return context.getCacheDir().getAbsolutePath();
  }

  @CalledByNative
  public static String readCacheFile(String filename) {
    Context context = ContextUtils.getApplicationContext();
    File file = new File(context.getCacheDir(), filename);
    if (!file.exists()) {
      return null;
    }
    try (BufferedReader reader =
        new BufferedReader(
            new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
      StringBuilder sb = new StringBuilder();
      String line;
      while ((line = reader.readLine()) != null) {
        sb.append(line);
      }
      return sb.toString();
    } catch (Exception e) {
      return null;
    }
  }

  @CalledByNative
  public static String getMetricsConfigFilename() {
    return CobaltPrefNames.METRICS_CONFIG_FILENAME;
  }

  @CalledByNative
  public static String getVariationsBeaconFilename() {
    return CobaltPrefNames.VARIATIONS_BEACON_FILENAME;
  }

  @CalledByNative
  public static String getExperimentConfigFilename() {
    return CobaltPrefNames.EXPERIMENT_CONFIG_FILENAME;
  }
}
