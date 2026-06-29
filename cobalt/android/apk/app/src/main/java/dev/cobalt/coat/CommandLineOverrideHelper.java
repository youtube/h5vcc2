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

package dev.cobalt.coat;

import java.util.ArrayList;
import java.util.List;
import java.util.StringJoiner;
import org.chromium.base.CommandLine;


// ==========
// IMPORTANT:
//
// These command line switches defaults do not affect non-AndroidTV platforms.
// If you are making changes to these values, please check that other
// platforms (such as Linux/Evergreen) are getting corresponding updates.

/** Helper class to provide commandLine Overrides. */
public final class CommandLineOverrideHelper {
    private CommandLineOverrideHelper() {} // Prevent instantiation.

    /** Param class to simplify #getFlagOverrides method signature */
    public static class CommandLineOverrideHelperParams {
        public CommandLineOverrideHelperParams(
            boolean isOfficialBuild,
            String[] commandLineArgs) {
            mIsOfficialBuild = isOfficialBuild;
            mCommandLineArgs = commandLineArgs;
        }

        private boolean mIsOfficialBuild;
        private String[] mCommandLineArgs;
    }

    // This can be returned as a list, since it does not need to be a single
    // string object. The others can be combined into a single String because
    // they need to be enclosed in the feature's enable/disable header.
    public static List<String> getDefaultCommandLineOverridesList() {
        List<String> paramOverrides = new ArrayList<>();

        // Run Cobalt as a single process.
        paramOverrides.add("--single-process");
        // Enable Blink to work in overlay video mode.
        paramOverrides.add("--force-video-overlays");
        // Autoplay video with url.
        paramOverrides.add("--autoplay-policy=no-user-gesture-required");
        // Disable rescaling Webpage.
        paramOverrides.add("--force-device-scale-factor=1");
        // Enable low end device mode.
        paramOverrides.add("--enable-low-end-device-mode");
        // Disables RGBA_4444 textures which
        // causes rendering artifacts when
        // low-end-device-mode is enabled.
        paramOverrides.add("--disable-rgba-4444-textures");
        // Disable Chrome's accelerated video encoding and decoding (Cobalt uses
        // Starboard's stack).
        paramOverrides.add("--disable-accelerated-video-decode");
        paramOverrides.add("--disable-accelerated-video-encode");
        // Rasterize Tiles directly to GPU memory.
        paramOverrides.add("--enable-zero-copy");
        // Set default raster threads to 2 for smoother performance.
        paramOverrides.add("--num-raster-threads=2");
        // Enforce ANGLE to use GLES backend by default.
        paramOverrides.add("--use-angle=gles");

        return paramOverrides;
    }

    public static StringJoiner getDefaultJsFlagOverridesList() {
        StringJoiner paramOverrides = new StringJoiner(",");

        // Trades a little V8 performance for significant memory savings.
        paramOverrides.add("--optimize-for-size");
        // Set initial old space size to 64MB and max old space size to 512MB.
        paramOverrides.add("--initial-old-space-size=64");
        paramOverrides.add("--max-old-space-size=512");

        // Disable decommitting pooled pages to prevent virtual memory fragmentation.
        paramOverrides.add("--no-decommit-pooled-pages");

        // Disable v8 concurrent marking by default.
        paramOverrides.add("--no-concurrent-marking");

        return paramOverrides;
    }

    public static StringJoiner getDefaultEnableFeatureOverridesList() {
        StringJoiner paramOverrides = new StringJoiner(",");

        // Pass javascript console log to adb log.
        paramOverrides.add("LogJsConsoleMessages");
        // Limit decoded image cache to 32 mbytes.
        paramOverrides.add("LimitImageDecodeCacheSize:mb/24");
        // It is important to use a feature override instead of the
        // rendering switch, to make sure certain devices are excluded.
        paramOverrides.add("DefaultPassthroughCommandDecoder");
        // Compositor switch to reduce prepaint tile cache size
        paramOverrides.add("SmallerInterestArea");
        paramOverrides.add("ReclaimPrepaintTilesWhenIdle");
        paramOverrides.add("ReclaimOldPrepaintTiles");

        return paramOverrides;
    }

    public static StringJoiner getDefaultDisableFeatureOverridesList() {
        StringJoiner paramOverrides = new StringJoiner(",");

        // Disable BackupRefPtr and have shim allocator tracked by MemoryReclaimer.
        paramOverrides.add("PartitionAllocBackupRefPtr");

        // Disable AAudio to make the microphone use OpenSL ES.
        // OpenSL ES supports seamless switching to virtual microphones like AtvRemote.
        // For details, see http://b/478022126#comment6.
        paramOverrides.add("UseAAudioInput");

        return paramOverrides;
    }

    public static StringJoiner getDefaultBlinkEnableFeatureOverridesList() {
        StringJoiner paramOverrides = new StringJoiner(",");

        // Enable precise memory info so we can make accurate client-side
        // measurements.
        paramOverrides.add("PreciseMemoryInfo");

        return paramOverrides;
    }

    public static void getFlagOverrides(
        CommandLineOverrideHelperParams params) {
        List<String> cliOverrides =
            getDefaultCommandLineOverridesList();
        StringJoiner jsFlagOverrides =
            getDefaultJsFlagOverridesList();
        StringJoiner enableFeatureOverrides =
            getDefaultEnableFeatureOverridesList();
        StringJoiner disableFeatureOverrides =
            getDefaultDisableFeatureOverridesList();
        StringJoiner blinkEnableFeatureOverrides =
            getDefaultBlinkEnableFeatureOverridesList();
        StringJoiner enableH5vccSettings = new StringJoiner(";");

        if (params != null) {
            if (!params.mIsOfficialBuild) {
                cliOverrides.add(
                  "--remote-allow-origins="
                  + "https://chrome-devtools-frontend.appspot.com");
            }

            if (params.mCommandLineArgs != null) {
                for (String param: params.mCommandLineArgs) {
                    if (param == null || param.isEmpty()) {
                        continue; // Skip null or empty params
                    }
                    String[] parts = param.split("=", 2);
                    if (parts.length == 2) {
                        String key = parts[0];
                        String value = parts[1];
                        String[] values = value.split(";");
                        for (String v : values) {
                            if (key.equals("--js-flags")) {
                                jsFlagOverrides.add(v);
                            } else if (key.equals("--enable-features")) {
                                enableFeatureOverrides.add(v);
                            } else if (key.equals("--disable-features")) {
                                disableFeatureOverrides.add(v);
                            } else if (key.equals("--enable-blink-features")) {
                                blinkEnableFeatureOverrides.add(v);
                            } else if (key.equals("--enable-h5vcc-settings")) {
                                enableH5vccSettings.add(v);
                            } else {
                                cliOverrides.add(param);
                                break; // Avoid adding the same param multiple times
                            }
                        }
                    } else {
                        cliOverrides.add(param);
                    }
                }
            }
        }
        CommandLine.getInstance().appendSwitchesAndArguments(
            cliOverrides.toArray(new String[0]));
        CommandLine.getInstance().appendSwitchesAndArguments(
            new String[]{"--js-flags=" + jsFlagOverrides.toString() });
        CommandLine.getInstance().appendSwitchesAndArguments(
            new String[]{"--enable-features="
            + enableFeatureOverrides.toString() });
        CommandLine.getInstance().appendSwitchesAndArguments(
            new String[]{"--disable-features="
            + disableFeatureOverrides.toString() });
        CommandLine.getInstance().appendSwitchesAndArguments(
            new String[]{"--enable-blink-features="
            + blinkEnableFeatureOverrides.toString() });
        if (enableH5vccSettings.length() > 0) {
            CommandLine.getInstance().appendSwitchesAndArguments(
                new String[]{"--enable-h5vcc-settings="
                + enableH5vccSettings.toString() });
        }
    }
}
