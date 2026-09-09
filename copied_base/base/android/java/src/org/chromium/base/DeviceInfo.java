// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

/**
 * Compatibility class for DeviceInfo in copied_base.
 */
public final class DeviceInfo {
  private DeviceInfo() {}

  public static String getArch() {
    return BuildInfo.getArch();
  }
}
