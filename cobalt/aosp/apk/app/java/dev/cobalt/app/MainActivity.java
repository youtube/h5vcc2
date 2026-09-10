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

import static dev.cobalt.util.Log.TAG;

import android.app.Activity;
import android.app.Service;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.Window;

import dev.cobalt.coat.BaseCobaltActivity;
import dev.cobalt.coat.BaseStarboardBridge;
import dev.cobalt.coat.CobaltService;
import dev.cobalt.libraries.services.clientloginfo.ClientLogInfoModule;
import dev.cobalt.media.VideoSurfaceView;
import dev.cobalt.util.DisplayUtil;
import dev.cobalt.util.Holder;
import dev.cobalt.util.Log;

import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

/**
 * Main Activity for the "Cobalt on AOSP" app.
 *
 * <p>The lifecycle is handled by the base class. MainActivity loads the evergreen loader library,
 * creates the BaseStarboardBridge, and spawns the native Starboard thread.
 */
@JNINamespace("starboard")
public class MainActivity extends BaseCobaltActivity {

    static {
        // Each aosp apk ships exactly one app loader under its natural name
        // via android_apk shared_libraries: libloader_app.so for the production
        // app or libelf_loader_sandbox.so for everything else. Load whichever is
        // present.
        UnsatisfiedLinkError loadError = null;
        for (String lib : new String[] {"loader_app", "elf_loader_sandbox"}) {
            try {
                System.loadLibrary(lib);
                loadError = null;
                break;
            } catch (UnsatisfiedLinkError e) {
                loadError = e;
            }
        }
        if (loadError != null) {
            throw loadError;
        }
    }

    @NativeMethods
    interface Natives {
        // Spawns the loader thread, whose main() runs the app loader.
        void startLoader();

        void nativeOnSurfaceCreated(Surface surface);

        // Blocks until the engine has let go of the surface
        void nativeOnSurfaceDestroyed();

        boolean nativeSendKeyEvent(int keyCode, int action, int unicodeChar, int metaState);

        void nativeSendBlurEvent();

        void nativeSendFocusEvent();

        void nativeSendConcealEvent();

        void nativeSendStopEvent();
    }

    private boolean mStarboardStarted;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // BaseStarboardBridge.getDisplaySize()/getDisplayDpi() read the DisplayMetrics DisplayUtil
        // caches, which stays null until cacheDefaultDisplay() runs. CobaltActivity does this from
        // onStart(); the AOSP app doesn't build CobaltActivity, so do it here.
        DisplayUtil.cacheDefaultDisplay(this);

        String startDeepLink = getIntentUrlAsString(getIntent());
        boolean coldStart = getStarboardBridge() == null;
        if (coldStart) {
            // Cold start - Instantiate the singleton BaseStarboardBridge.
            BaseStarboardBridge starboardBridge = createStarboardBridge(getArgs(), startDeepLink);
            ((BaseStarboardBridge.HostApplication) getApplication())
                    .setStarboardBridge(starboardBridge);
        } else if (savedInstanceState == null) {
            // Warm start - Pass the deep link to the running Starboard app.
            getStarboardBridge().handleDeepLink(startDeepLink);
        }

        // The NDK uses an ANativeWindow to represent a producer of an image queue - it can send the
        // produced images to other consumers or it can be displayed on screen. The Activity's
        // window
        // already owns such a surface, so instead of stacking a dedicated SurfaceView on top of the
        // view hierarchy we take that surface over and render the Cobalt UI into it.
        Window window = getWindow();

        // Punch-out video needs SurfaceFlinger to blend this window over the VideoSurfaceView below
        window.setFormat(PixelFormat.TRANSLUCENT);

        // The surface isn't available immediately, so we wait for the callback, store the surface
        // and only then start the loader.
        window.takeSurface(
                new SurfaceHolder.Callback2() {
                    @Override
                    public void surfaceCreated(SurfaceHolder holder) {
                        // ViewRootImpl resets the window format to the theme default when it
                        // installs its
                        // holder, so the translucent PixelFormat needs to be set again
                        holder.setFormat(PixelFormat.TRANSLUCENT);
                        MainActivityJni.get().nativeOnSurfaceCreated(holder.getSurface());
                        if (coldStart && !mStarboardStarted) {
                            mStarboardStarted = true;
                            // Spawn the loader thread.
                            MainActivityJni.get().startLoader();
                        } else {
                            // Coming back from background. Reveal and focus only after the new
                            // surface is in place
                            MainActivityJni.get().nativeSendFocusEvent();
                        }
                    }

                    @Override
                    public void surfaceChanged(
                            SurfaceHolder holder, int format, int width, int height) {
                        Log.i(
                                TAG,
                                "Window surface changed: %dx%d format=%d",
                                width,
                                height,
                                format);
                        // TODO(b/532068409): forward this as kSbEventTypeWindowSizeChanged. Ozone
                        // already
                        // consumes it (PlatformEventSourceStarboard::HandleWindowSizeChangedEvent)
                    }

                    @Override
                    public void surfaceDestroyed(SurfaceHolder holder) {
                        // From the SurfaceHolder surfaceDestroyed() callback:
                        // "This is called immediately before a surface is being destroyed.
                        // After returning from this call, you should no longer try to access
                        // this surface.  If you have a rendering thread that directly accesses
                        // the surface, you must ensure that thread is no longer touching the
                        // Surface before returning from this function."
                        //
                        // The native side blocks until the engine has released the surface,
                        // so nothing touches it after this returns.
                        MainActivityJni.get().nativeOnSurfaceDestroyed();
                    }

                    @Override
                    public void surfaceRedrawNeeded(SurfaceHolder holder) {
                        // Starboard renders continuously, so there is nothing to redraw on demand.
                    }
                });

        setContentView(new VideoSurfaceView(this));
    }

    @Override
    protected void onPause() {
        // Blurred, not concealed, the surface is still alive.
        MainActivityJni.get().nativeSendBlurEvent();
        super.onPause();
    }

    @Override
    protected void onStop() {
        // Covers stopping without dropping the surface, which is the only case
        // where surfaceDestroyed() doesn't run first.
        MainActivityJni.get().nativeSendConcealEvent();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        // The Activity may be destroyed and re-created, for instance on
        // configuration change events. Only tear down the starboard process if
        // the activity is really being closed.
        if (isFinishing()) {
            MainActivityJni.get().nativeSendStopEvent();
        }
        super.onDestroy();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        boolean handled =
                MainActivityJni.get()
                        .nativeSendKeyEvent(
                                event.getKeyCode(),
                                event.getAction(),
                                event.getUnicodeChar(),
                                event.getMetaState());
        return handled || super.dispatchKeyEvent(event);
    }

    @Override
    protected BaseStarboardBridge createStarboardBridge(String[] args, String startDeepLink) {
        Holder<Activity> activityHolder = new Holder<>();
        Holder<Service> serviceHolder = new Holder<>();
        BaseStarboardBridge bridge =
                new BaseStarboardBridge(
                        getApplicationContext(),
                        activityHolder,
                        serviceHolder,
                        args,
                        startDeepLink);

        CobaltService.Factory clientLogInfoFactory =
                new ClientLogInfoModule().provideFactory(getApplicationContext());
        bridge.registerCobaltService(clientLogInfoFactory);

        return bridge;
    }
}
