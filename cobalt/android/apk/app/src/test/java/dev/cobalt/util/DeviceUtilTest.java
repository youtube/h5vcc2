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

import static com.google.common.truth.Truth.assertThat;

import android.util.DisplayMetrics;
import org.chromium.base.ContextUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/** Unit tests for {@link DeviceUtil}. */
@RunWith(RobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class DeviceUtilTest {

  @Before
  public void setUp() {
    ContextUtils.initApplicationContextForTests(RuntimeEnvironment.getApplication());
    DeviceUtil.resetForTesting();
  }

  @After
  public void tearDown() {
    DeviceUtil.resetForTesting();
  }

  @Test
  public void testIs1GbDevice_OverrideTrue() {
    DeviceUtil.setIs1GbDeviceForTesting(true);
    assertThat(DeviceUtil.is1GbDevice()).isTrue();
  }

  @Test
  public void testIs1GbDevice_OverrideFalse() {
    DeviceUtil.setIs1GbDeviceForTesting(false);
    assertThat(DeviceUtil.is1GbDevice()).isFalse();
  }

  @Test
  public void testIsDisplayAtLeast1080p_OverrideTrue() {
    DeviceUtil.setIsDisplayAtLeast1080pForTesting(true);
    assertThat(DeviceUtil.isDisplayAtLeast1080p()).isTrue();
  }

  @Test
  public void testIsDisplayAtLeast1080p_OverrideFalse() {
    DeviceUtil.setIsDisplayAtLeast1080pForTesting(false);
    assertThat(DeviceUtil.isDisplayAtLeast1080p()).isFalse();
  }

  @Test
  public void testResetForTesting() {
    boolean defaultIs1Gb = DeviceUtil.is1GbDevice();
    boolean defaultIs1080p = DeviceUtil.isDisplayAtLeast1080p();

    DeviceUtil.setIs1GbDeviceForTesting(!defaultIs1Gb);
    DeviceUtil.setIsDisplayAtLeast1080pForTesting(!defaultIs1080p);

    assertThat(DeviceUtil.is1GbDevice()).isEqualTo(!defaultIs1Gb);
    assertThat(DeviceUtil.isDisplayAtLeast1080p()).isEqualTo(!defaultIs1080p);

    DeviceUtil.resetForTesting();

    assertThat(DeviceUtil.is1GbDevice()).isEqualTo(defaultIs1Gb);
    assertThat(DeviceUtil.isDisplayAtLeast1080p()).isEqualTo(defaultIs1080p);
  }

  @Test
  public void testIsDisplayAtLeast1080p_DisplayMetrics_Landscape1080p() {
    DisplayMetrics metrics = RuntimeEnvironment.getApplication().getResources().getDisplayMetrics();
    metrics.widthPixels = 1920;
    metrics.heightPixels = 1080;

    assertThat(DeviceUtil.isDisplayAtLeast1080p()).isTrue();
  }

  @Test
  public void testIsDisplayAtLeast1080p_DisplayMetrics_Portrait1080p() {
    DisplayMetrics metrics = RuntimeEnvironment.getApplication().getResources().getDisplayMetrics();
    metrics.widthPixels = 1080;
    metrics.heightPixels = 1920;

    assertThat(DeviceUtil.isDisplayAtLeast1080p()).isTrue();
  }

  @Test
  public void testIsDisplayAtLeast1080p_DisplayMetrics_Portrait720p() {
    DisplayMetrics metrics = RuntimeEnvironment.getApplication().getResources().getDisplayMetrics();
    metrics.widthPixels = 720;
    metrics.heightPixels = 1280;

    // Portrait 720p has height 1280 > 1080, but max dimension 1280 < 1600.
    assertThat(DeviceUtil.isDisplayAtLeast1080p()).isFalse();
  }

  @Test
  public void testIsDisplayAtLeast1080p_DisplayMetrics_Landscape720p() {
    DisplayMetrics metrics = RuntimeEnvironment.getApplication().getResources().getDisplayMetrics();
    metrics.widthPixels = 1280;
    metrics.heightPixels = 720;

    assertThat(DeviceUtil.isDisplayAtLeast1080p()).isFalse();
  }
}
