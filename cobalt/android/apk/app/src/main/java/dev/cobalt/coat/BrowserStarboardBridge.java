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
import android.app.Service;
import android.content.Context;
import dev.cobalt.shell.StartupGuard;
import dev.cobalt.util.Holder;
import org.chromium.content_public.browser.WebContents;

/** Content-dependent StarboardBridge subclass used by AndroidTV. */
public class BrowserStarboardBridge extends StarboardBridge {

  private CobaltMediaSession mCobaltMediaSession;
  private VolumeStateReceiver mVolumeStateReceiver;
  private PlatformError mPlatformError;

  public BrowserStarboardBridge(
      Context appContext,
      Holder<Activity> activityHolder,
      Holder<Service> serviceHolder,
      ArtworkDownloader artworkDownloader,
      String[] args,
      String startDeepLink) {
    super(appContext, activityHolder, serviceHolder, args, startDeepLink);
    mCobaltMediaSession = new CobaltMediaSession(appContext, activityHolder, artworkDownloader);
    mVolumeStateReceiver = new VolumeStateReceiver(appContext);
  }

  @Override
  protected void onActivityStop(Activity activity) {
    super.onActivityStop(activity);
    mCobaltMediaSession.onActivityStop();
  }

  @Override
  void raisePlatformError(int errorType, long data, String url) {
    StartupGuard.getInstance().setStartupMilestone(37);
    mPlatformError = new PlatformError(mActivityHolder, errorType, data, url);
    mPlatformError.raise();
  }

  @Override
  public boolean isPlatformErrorShowing() {
    if (mPlatformError != null) {
      return mPlatformError.isShowing();
    }
    return false;
  }

  public void setWebContents(WebContents webContents) {
    mCobaltMediaSession.setWebContents(webContents);
    mVolumeStateReceiver.setWebContents(webContents);
  }

  @Override
  protected void hideSplashScreen() {
    StartupGuard.getInstance().disarm();
  }

  @Override
  protected void setStartupMilestone(int milestone) {
    StartupGuard.getInstance().setStartupMilestone(milestone);
  }

  @Override
  protected void setStartupDiagnosisInfo(String key, String value) {
    StartupGuard.getInstance().setDiagnosisInfo(key, value);
  }
}
