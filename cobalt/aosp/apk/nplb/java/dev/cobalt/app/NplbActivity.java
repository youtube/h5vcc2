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

import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import org.chromium.build.gtest_apk.NativeTestIntent;
import org.chromium.test.reporter.TestStatusReporter;

/**
 * A {@link MainActivity} for running nplb, handling the arguments passed by the instrumentation
 * runner.
 */
public class NplbActivity extends MainActivity {
  private static final String TAG = "NplbInstrumentation";

  private static final String EVERGREEN_LIBRARY_PATH = "app/cobalt/lib/libnplb.lz4";
  private static final String EVERGREEN_CONTENT_PATH = "app/cobalt/content";

  private TestStatusReporter mReporter;

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    mReporter = new TestStatusReporter(this);
    mReporter.testRunStarted(Process.myPid());
    super.onCreate(savedInstanceState);
  }

  @Override
  protected void onDestroy() {
    mReporter.testRunFinished(Process.myPid());
    super.onDestroy();
  }

  @Override
  protected String[] getArgs() {
    ArrayList<String> args = new ArrayList<>();
    args.add("--evergreen_library=" + EVERGREEN_LIBRARY_PATH);
    args.add("--evergreen_content=" + EVERGREEN_CONTENT_PATH);
    args.addAll(gtestArgsFromIntent());
    Log.i(TAG, "NPLB loader argv: " + args);
    return args.toArray(new String[0]);
  }

  private ArrayList<String> gtestArgsFromIntent() {
    ArrayList<String> flags = new ArrayList<>();
    Intent intent = getIntent();

    // Flags may arrive as a command-line file, an inline string, or a gtest-filter extra.
    String cmdFile = intent.getStringExtra(NativeTestIntent.EXTRA_COMMAND_LINE_FILE);
    if (cmdFile != null && !cmdFile.isEmpty()) {
      flags.addAll(readCommandLineFile(cmdFile));
    }

    String cmdFlags = intent.getStringExtra(NativeTestIntent.EXTRA_COMMAND_LINE_FLAGS);
    if (cmdFlags != null && !cmdFlags.trim().isEmpty()) {
      for (String flag : cmdFlags.trim().split("\\s+")) {
        flags.add(flag);
      }
    }

    String gtestFilter = intent.getStringExtra(NativeTestIntent.EXTRA_GTEST_FILTER);
    if (gtestFilter != null && !gtestFilter.isEmpty()) {
      flags.add("--gtest_filter=" + gtestFilter);
    }

    // Redirect the loader's stdout/stderr to the runner's stdout file.
    String stdoutFile = intent.getStringExtra(NativeTestIntent.EXTRA_STDOUT_FILE);
    if (stdoutFile != null && !stdoutFile.isEmpty()) {
      flags.add("--android_stdout_file=" + stdoutFile);
    }
    return flags;
  }

  private static ArrayList<String> readCommandLineFile(String path) {
    ArrayList<String> tokens = new ArrayList<>();
    try (BufferedReader reader = new BufferedReader(new FileReader(new File(path)))) {
      StringBuilder builder = new StringBuilder();
      String line;
      while ((line = reader.readLine()) != null) {
        builder.append(line).append(' ');
      }
      String content = builder.toString().trim();
      if (!content.isEmpty()) {
        String[] parts = content.split("\\s+");
        // The first token is conventionally the program name; drop it if it isn't a flag.
        int start = (parts.length > 0 && !parts[0].startsWith("-")) ? 1 : 0;
        for (int i = start; i < parts.length; i++) {
          tokens.add(parts[i]);
        }
      }
    } catch (IOException e) {
      Log.w(TAG, "Failed to read command line file: " + path, e);
    }
    return tokens;
  }
}
