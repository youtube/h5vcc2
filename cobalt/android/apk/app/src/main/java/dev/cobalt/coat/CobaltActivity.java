// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

import static dev.cobalt.util.Log.TAG;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.SystemClock;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup.LayoutParams;
import android.view.ViewParent;
import android.view.WindowManager;
import android.view.Display;
import android.hardware.display.DisplayManager;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;
import android.widget.FrameLayout;
import android.widget.Toast;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import androidx.annotation.VisibleForTesting;
import dev.cobalt.browser.CobaltContentBrowserClient;
import dev.cobalt.coat.javabridge.CobaltJavaScriptAndroidObject;
import dev.cobalt.coat.javabridge.CobaltJavaScriptInterface;
import dev.cobalt.coat.javabridge.HTMLMediaElementExtension;
import dev.cobalt.media.AudioOutputManager;
import dev.cobalt.media.MediaCodecCapabilitiesLogger;
import dev.cobalt.media.VideoSurfaceView;
import dev.cobalt.shell.Shell;
import dev.cobalt.shell.ShellManager;
import dev.cobalt.shell.ShellManagerJni;
import dev.cobalt.shell.StartupGuard;
import dev.cobalt.util.DisplayUtil;
import dev.cobalt.util.JavaSwitches;
import dev.cobalt.util.Log;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;
import org.chromium.base.CommandLine;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.library_loader.LibraryProcessType;
import org.chromium.base.memory.MemoryPressureMonitor;
import org.chromium.base.memory.MemoryPressureUma;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.version_info.VersionInfo;
import org.chromium.content.browser.input.ImeAdapterImpl;
import org.chromium.content_public.browser.BrowserStartupController;
import org.chromium.content_public.browser.DeviceUtils;
import org.chromium.content_public.browser.JavascriptInjector;
import org.chromium.content_public.browser.WebContents;
import org.chromium.net.NetworkChangeNotifier;
import org.chromium.ui.base.ActivityWindowAndroid;
import org.chromium.ui.base.IntentRequestTracker;

/** Native activity that has the required JNI methods called by the Starboard implementation. */
public abstract class CobaltActivity extends Activity {
  private static final String URL_ARG = "--url=";
  private static final String META_DATA_APP_URL = "cobalt.APP_URL";
  private static final String META_DATA_ENABLE_SPLASH_SCREEN = "cobalt.ENABLE_SPLASH_SCREEN";
  private static final String META_DATA_ENABLE_FEATURES = "cobalt.ENABLE_FEATURES";
  private static final String YOUTUBE_URL = "https://www.youtube.com/tv";
  private static final String COBALT_USING_ANDROID_OVERLAY = "cobalt-using-android-overlay";

  // This key differs in naming format for legacy reasons
  public static final String COMMAND_LINE_ARGS_KEY = "commandLineArgs";

  private static final Pattern URL_PARAM_PATTERN = Pattern.compile("^[a-zA-Z0-9_=]*$");

  // How many seconds before the app exits if it fails to land YouTube home page.
  private static final int DEFAULT_HANG_APP_CRASH_TIMEOUT_SECONDS = 120;

  // Maintain the list of JavaScript-exposed objects as a member variable
  // to prevent them from being garbage collected prematurely.
  private List<CobaltJavaScriptAndroidObject> mJavaScriptAndroidObjectList = new ArrayList<>();
  private Map<String, String> mJavaSwitches = new HashMap<>();

  @SuppressWarnings("unused")
  private CobaltA11yHelper mA11yHelper;

  private VideoSurfaceView mVideoSurfaceView;

  private boolean mForceCreateNewVideoSurfaceView;

  private long mTimeInNanoseconds;

  private ShellManager mShellManager;
  private ActivityWindowAndroid mWindowAndroid;
  private Intent mLastSentIntent;
  private String mStartupUrl;
  private IntentRequestTracker mIntentRequestTracker;
  // Tracks the status of the FLAG_KEEP_SCREEN_ON window flag.
  private Boolean mIsKeepScreenOnEnabled = false;
  private Runnable mFreezeRunnable;
  private final Handler mHandler = new Handler(Looper.getMainLooper());
  private boolean mIsCobaltUsingAndroidOverlay;

  private boolean mEnableSplashScreen;
  private String mStartDeepLink;

  private Object mBackInvokedCallback;

  // Tracks if a physical/hardware back button press is currently in progress.
  // On Android 13+ (API >= 33) with predictive back gesture enabled, the OS dispatches physical
  // back button presses as both key events (onKeyDown/onKeyUp) and OnBackInvokedCallback.
  // To prevent double-triggering back navigation in the web application (which maps back keys
  // to Escape), this flag is used by OnBackInvokedCallback to detect physical key presses and
  // bypass simulated key dispatching.
  private boolean mPhysicalBackKeyPressed = false;
  private final DisplayUtil.Listener mDisplayListener = new DisplayUtil.Listener() {
    @Override
    public void onDisplayChanged(int displayId) {
      if (displayId == Display.DEFAULT_DISPLAY) {
        checkDisplayState();
      }
    }
  };
  private boolean mWasDisplayOn = true;

  private Bundle getActivityMetaData() {
    ComponentName componentName = getIntent().getComponent();
    if (componentName == null) {
      Log.w(TAG, "Activity intent has no component; cannot get metadata.");
      return null;
    }
    ActivityInfo ai;
    try {
      ai = getPackageManager()
                .getActivityInfo(componentName, PackageManager.GET_META_DATA);
    } catch (NameNotFoundException e) {
      Log.e(TAG, "Error getting activity info", e);
      return null;
    }
    if (ai == null) {
      return null;
    }
    return ai.metaData;
  }

  @VisibleForTesting
  static String[] appendArgsFromMetaData(Bundle metaData, String[] commandLineArgs) {
    if (metaData == null) {
      return commandLineArgs;
    }

    List<String> args = new ArrayList<>();
    if (commandLineArgs != null) {
      args.addAll(Arrays.asList(commandLineArgs));
    }

    boolean enableSplashScreen = metaData.getBoolean(META_DATA_ENABLE_SPLASH_SCREEN, true);
    if (!enableSplashScreen) {
      args.add("--disable-splash-screen");
    }

    String enableFeatures = metaData.getString(META_DATA_ENABLE_FEATURES);
    if (TextUtils.isEmpty(enableFeatures)) {
      return args.toArray(new String[0]);
    }

    // CommandLineOverrideHelper will merge this with other --enable-features flags
    // It also accepts semi-colon-separated list of features.
    // https://github.com/youtube/cobalt/blob/6407cbdf6573f0b5fcae4a8fa6f46a3198b3d42b/cobalt/android/apk/app/src/main/java/dev/cobalt/coat/CommandLineOverrideHelper.java#L139-L167
    args.add("--enable-features=" + enableFeatures);
    return args.toArray(new String[0]);
  }

  // Initially copied from ContentShellActiviy.java
  protected void createContent(final Bundle savedInstanceState) {
    StartupGuard.getInstance().setStartupMilestone(1);

    // Initializing the command line must occur before loading the library.
    if (!CommandLine.isInitialized()) {
      CommandLine.init(null);

      String[] commandLineArgs = null;
      if (!VersionInfo.isReleaseBuild()) {
        commandLineArgs = getCommandLineParamsFromIntent(getIntent(), COMMAND_LINE_ARGS_KEY);
      }
      commandLineArgs = appendArgsFromMetaData(getActivityMetaData(), commandLineArgs);

      List<String> extraCommandLineArgs = JavaSwitches.getExtraCommandLineArgs(getJavaSwitches());

      if (!extraCommandLineArgs.isEmpty()) {
        if (commandLineArgs != null) {
          extraCommandLineArgs.addAll(0, Arrays.asList(commandLineArgs));
        }
        commandLineArgs = extraCommandLineArgs.toArray(new String[0]);
      }

      CommandLineOverrideHelper.getFlagOverrides(
          new CommandLineOverrideHelper.CommandLineOverrideHelperParams(
              VersionInfo.isOfficialBuild(), commandLineArgs));
    }
    mIsCobaltUsingAndroidOverlay = CommandLine.getInstance().hasSwitch(COBALT_USING_ANDROID_OVERLAY);

    DeviceUtils.updateDeviceSpecificUserAgentSwitch(this);


    StartupGuard.getInstance().setStartupMilestone(2);
    // This initializes JNI and ends up calling JNI_OnLoad in native code
    LibraryLoader.getInstance().ensureInitialized();
    StartupGuard.getInstance().setStartupMilestone(3);

    // StarboardBridge initialization must happen right after library loading,
    // before Browser/Content module is started. It currently tracks its own JNI state
    // variables, and needs to set up an early copy.

    // TODO(b/374147993): how to handle deeplink in Chrobalt?
    mStartDeepLink = getIntentUrlAsString(getIntent());
    if (mStartDeepLink == null) {
      Log.w(TAG, "startDeepLink cannot be null, set it to empty string.");
      mStartDeepLink = "";
    }

    StartupGuard.getInstance().setStartupMilestone(4);
    if (getStarboardBridge() == null) {
      // Cold start - Instantiate the singleton StarboardBridge.
      RecordHistogram.recordBooleanHistogram("Cobalt.Android.ColdStart", true);
      if (CommandLine.getInstance().hasSwitch("enable-optimized-font-loading")
          || getJavaSwitches().containsKey(JavaSwitches.ENABLE_OPTIMIZED_FONT_LOADING)) {
        FontUtil.copyFontsXml(getApplicationContext());
      }
      StarboardBridge starboardBridge = createStarboardBridge(getArgs(), mStartDeepLink);
      ((StarboardBridge.HostApplication) getApplication()).setStarboardBridge(starboardBridge);
    } else {
      // Warm start - Pass the deep link to the running Starboard app.
      if (savedInstanceState == null) {
        RecordHistogram.recordBooleanHistogram("Cobalt.Android.ColdStart", false);
      }
      getStarboardBridge().handleDeepLink(mStartDeepLink);
    }
    StartupGuard.getInstance().setStartupMilestone(7);

    mShellManager = new ShellManager(this);
    final boolean listenToActivityState = true;
    mIntentRequestTracker = IntentRequestTracker.createFromActivity(this);
    mWindowAndroid =
        new ActivityWindowAndroid(
            this,
            listenToActivityState,
            mIntentRequestTracker,
            /* insetObserver= */ null,
            /* trackOcclusion= */ false);
    mIntentRequestTracker.restoreInstanceState(savedInstanceState);
    mShellManager.setWindow(mWindowAndroid);
    // Set up the animation placeholder to be the SurfaceView. This disables the
    // SurfaceView's 'hole' clipping during animations that are notified to the window.
    mWindowAndroid.setAnimationPlaceholderView(
        mShellManager.getContentViewRenderView().getSurfaceView());
    mA11yHelper = new CobaltA11yHelper(this,
        mShellManager.getContentViewRenderView().getSurfaceView());

    if (mStartupUrl == null || mStartupUrl.isEmpty()) {
      String[] args = getStarboardBridge().getArgs();
      mStartupUrl =
          Arrays.stream(args)
              .filter(line -> line.contains(URL_ARG))
              .findAny()
              .map(arg -> arg.substring(arg.indexOf(URL_ARG) + URL_ARG.length()))
              .orElse(null);
    }

    // META_DATA_APP_URL is configured to be the same as hardcoded YOUTUBE_URL.
    // If the app is used to start other web applications e.g google.com, because there will have no Kabuki web application code to call h5vcc.system.HideSplashScreen().
    // We should disarm Startup Guard now.
    if (TextUtils.isEmpty(mStartupUrl) || !mStartupUrl.startsWith(YOUTUBE_URL)) {
      Log.i(TAG, "Non-Youtube startup URL detected.");
      StartupGuard.getInstance().disarm();
    }

    StartupGuard.getInstance().setStartupMilestone(8);
    // TODO(b/377025559): Bring back WebTests launch capability
    BrowserStartupController.getInstance()
        .startBrowserProcessesAsync(
            LibraryProcessType.PROCESS_BROWSER,
            false, // Do not start a separate GPU process
            // TODO(b/377025565): Figure out what this means
            false, // Do not start in "minimal" or paused mode
            new BrowserStartupController.StartupCallback() {
              @Override
              public void onSuccess() {
                // NOTE: This log message is hard-coded in smoke tests to detect browser startup success.
                // See ManekiBaseDeviceUtil.CHROBALT_BROWSER_READY_REGEX in the internal test suite.
                Log.i(TAG, "Browser process init succeeded");

                finishInitialization(savedInstanceState);
                getStarboardBridge().measureAppStartTimestamp();
              }

              @Override
              public void onFailure() {
                Log.e(TAG, "Browser process init failed");
                initializationFailed();
              }
            });

  }

  // Initially copied from ContentShellActiviy.java
  private void finishInitialization(Bundle savedInstanceState) {

    // Initialize the platform's AudioSinkImpl. This is called here as this must come after the
    // browser client's feature list and field trials are initialized. The feature list and field
    // trials are initialized in CobaltContentBrowserClient::CreateFeatureListAndFieldTrials().
    getStarboardBridge().initializePlatformAudioSink();

    // Load an empty page to let shell create WebContents. Override Shell.java's
    // onWebContentsReady()
    // to only continue with initializeJavaBridge() and setting the webContents once it's confirmed
    // that the webContents are correctly created not null.
    mShellManager.launchShell(
        "",
        mStartDeepLink,
        new Shell.OnWebContentsReadyListener() {
          @Override
          public void onWebContentsReady() {
            // Inject JavaBridge objects to the WebContents.
            initializeJavaBridge();
            ((BrowserStarboardBridge) getStarboardBridge()).setWebContents(getActiveWebContents());

            // Load the `url` with the same shell we created above.
            mStartupUrl = ShellManagerJni.get().appendMigrationStatus(mStartupUrl);
            Log.i(TAG, "shellManager load url:" + mStartupUrl);
            mShellManager.getActiveShell().loadUrl(mStartupUrl);

            // Load splash screen.
            mShellManager.getActiveShell().loadSplashScreenWebContents();
          }
        });
  }

  // Initially copied from ContentShellActiviy.java
  private void initializationFailed() {
    Log.e(TAG, "ContentView initialization failed.");
    Toast.makeText(
            CobaltActivity.this, R.string.browser_process_initialization_failed, Toast.LENGTH_SHORT)
        .show();
    finish();
  }

  protected boolean dispatchKeyEventToIme(int keyCode, int action) {
    ImeAdapterImpl imeAdapter = getImeAdapterImpl();
    if (imeAdapter == null) {
      return false;
    }

    // |KeyEvent| needs to be created with |downTime| and |eventTime| set. If they are not set the
    // app closes.
    long eventTime = SystemClock.uptimeMillis();
    return imeAdapter.dispatchKeyEvent(new KeyEvent(eventTime, eventTime, action, keyCode, 0));
  }

  @Override
  public boolean onKeyDown(int keyCode, KeyEvent event) {
    if (keyCode == KeyEvent.KEYCODE_BACK) {
      mPhysicalBackKeyPressed = true;
    }
    // Gamepad buttons and certain partner-handled keys should bypass IME. The IME incorrectly
    // consumes the events as VKEY_UNKNOWN and prevent proper handling by the OS.
    if (KeyEvent.isGamepadButton(keyCode) || KeyboardUtils.isPartnerFallbackKey(keyCode)) {
      return super.onKeyDown(keyCode, event);
    }
    return dispatchKeyEventToIme(keyCode, KeyEvent.ACTION_DOWN) || super.onKeyDown(keyCode, event);
  }

  @Override
  public boolean onKeyUp(int keyCode, KeyEvent event) {
    if (KeyEvent.isGamepadButton(keyCode) || KeyboardUtils.isPartnerFallbackKey(keyCode)) {
      return super.onKeyUp(keyCode, event);
    }
    if (keyCode == KeyEvent.KEYCODE_BACK) {
      mPhysicalBackKeyPressed = false;
    }
    return dispatchKeyEventToIme(keyCode, KeyEvent.ACTION_UP) || super.onKeyUp(keyCode, event);
  }

  // Initially copied from ContentShellActiviy.java
  @Override
  public void onActivityResult(int requestCode, int resultCode, Intent data) {
    super.onActivityResult(requestCode, resultCode, data);
    mIntentRequestTracker.onActivityResult(requestCode, resultCode, data);
  }

  // Initially copied from ContentShellActiviy.java
  @Override
  public void startActivity(Intent i) {
    mLastSentIntent = i;
    super.startActivity(i);
  }

  public Intent getLastSentIntent() {
    return mLastSentIntent;
  }

  private static String getUrlFromIntent(Intent intent) {
    return intent != null ? intent.getDataString() : null;
  }

  private static String[] getCommandLineParamsFromIntent(Intent intent, String key) {
    return intent != null ? intent.getStringArrayExtra(key) : null;
  }

  /**
   * @return The {@link ShellManager} configured for the activity or null if it has not been created
   *     yet.
   */
  public ShellManager getShellManager() {
    return mShellManager;
  }

  /**
   * @return The currently visible {@link Shell} or null if one is not showing.
   */
  public Shell getActiveShell() {
    return mShellManager != null ? mShellManager.getActiveShell() : null;
  }

  /**
   * @return The {@link WebContents} owned by the currently visible {@link Shell} or null if one is
   *     not showing.
   */
  @Nullable
  public WebContents getActiveWebContents() {
    Shell shell = getActiveShell();
    return shell != null ? shell.getWebContents() : null;
  }

  @Nullable
  protected ImeAdapterImpl getImeAdapterImpl() {
    WebContents webContents = getActiveWebContents();
    return webContents != null ? ImeAdapterImpl.fromWebContents(webContents) : null;
  }

  @VisibleForTesting
  void setupStartupGuard() {
    Map<String, String> switches = getJavaSwitches();
    if (switches.containsKey(JavaSwitches.DISABLE_STARTUP_GUARD)) {
      Log.i(TAG, "StartupGuard is disabled by Java switch.");
      return;
    }

    long timeout = DEFAULT_HANG_APP_CRASH_TIMEOUT_SECONDS;
    String intervalStr = switches.get(JavaSwitches.STARTUP_GUARD_INTERVAL_IN_SECONDS);
    if (intervalStr != null) {
      try {
        long parsedTimeout = Long.parseLong(intervalStr);
        if (parsedTimeout > 0) {
          timeout = parsedTimeout;
        } else {
          Log.w(TAG, "Invalid StartupGuard interval (must be positive): " + intervalStr);
        }
      } catch (NumberFormatException e) {
        Log.w(TAG, "Invalid StartupGuard interval string: " + intervalStr);
      }
    }
    Log.i(TAG, "Arming StartupGuard with " + timeout + " second timeout.");
    StartupGuard.getInstance().scheduleCrash(timeout);
  }

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    // Record the application start timestamp.
    mTimeInNanoseconds = System.nanoTime();

    // To ensure that volume controls adjust the correct stream, make this call
    // early in the app's lifecycle. This connects the volume controls to
    // STREAM_MUSIC whenever the target activity or fragment is visible.
    setVolumeControlStream(AudioManager.STREAM_MUSIC);

    super.onCreate(savedInstanceState);

    setupStartupGuard();
    createContent(savedInstanceState);
    MemoryPressureMonitor.INSTANCE.registerComponentCallbacks();
    MemoryPressureUma.initializeForBrowser();
    NetworkChangeNotifier.init();
    NetworkChangeNotifier.setAutoDetectConnectivityState(true);

    if (!mIsCobaltUsingAndroidOverlay) {
      mVideoSurfaceView = new VideoSurfaceView(this);
      addContentView(
          mVideoSurfaceView, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
    } else {
      Log.i(TAG, "Do not create VideoSurfaceView.");
    }
    StartupGuard.getInstance().setStartupMilestone(9);

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      mBackInvokedCallback = OnBackInvokedHelper.register(this);
    }
  }

  /**
   * Initializes the Java Bridge to allow communication between Java and JavaScript. This method
   * injects Java objects into the WebView and loads corresponding JavaScript code.
   */
  private void initializeJavaBridge() {
    Log.i(TAG, "initializeJavaBridge");

    WebContents webContents = getActiveWebContents();
    if (webContents == null) {
      throw new RuntimeException(
          "webContents is null in initializeJavaBridge. This should never happen.");
    }

    // --- Initialize the Java Bridge ---

    // 1. Gather all Java objects that need to be exposed to JavaScript.
    // TODO(b/379701165): consider to refine the way to add JavaScript interfaces.
    mJavaScriptAndroidObjectList.add(new HTMLMediaElementExtension(this));

    // 2. Use JavascriptInjector to inject Java objects into the WebContents.
    //    This makes the annotated methods in these objects accessible from JavaScript.
    JavascriptInjector javascriptInjector = JavascriptInjector.fromWebContents(webContents);

    javascriptInjector.setAllowInspection(true);
    for (CobaltJavaScriptAndroidObject javascriptAndroidObject : mJavaScriptAndroidObjectList) {
      Log.d(
          TAG,
          "Add JavaScriptAndroidObject:" + javascriptAndroidObject.getJavaScriptInterfaceName());
      javascriptInjector.addPossiblyUnsafeInterface(
          javascriptAndroidObject,
          javascriptAndroidObject.getJavaScriptInterfaceName(),
          CobaltJavaScriptInterface.class,
          /* originAllowlist= */ new ArrayList<String>());
    }
  }

  /**
   * Instantiates the StarboardBridge. Apps not supporting sign-in should inject an instance of
   * NoopUserAuthorizer. Apps may subclass StarboardBridge if they need to override anything.
   */
  protected abstract StarboardBridge createStarboardBridge(String[] args, String startDeepLink);

  protected StarboardBridge getStarboardBridge() {
    return ((StarboardBridge.HostApplication) getApplication()).getStarboardBridge();
  }

  @Override
  protected void onStart() {
    DisplayUtil.cacheDefaultDisplay(this);
    DisplayUtil.addDisplayListener(this);
    mWasDisplayOn = isDisplayOn();
    registerDisplayListener();
    StartupGuard.getInstance().setStartupMilestone(10);
    if (isDevelopmentBuild()) {
      getStarboardBridge().getAudioOutputManager().dumpAllOutputDevices();
      MediaCodecCapabilitiesLogger.dumpAllDecoders();
    }
    if (mIsCobaltUsingAndroidOverlay) {
      Log.i(TAG, "Use AndroidOverlay for Video SurfaceView.");
    } else if (mForceCreateNewVideoSurfaceView) {
      Log.w(TAG, "Force to create a new video surface.");
      createNewSurfaceView();
    }

    AudioOutputManager.addAudioDeviceListener(this);

    getStarboardBridge().onActivityStart(this);
    super.onStart();

    if (mFreezeRunnable != null) {
      mHandler.removeCallbacks(mFreezeRunnable);
      mFreezeRunnable = null;
    }
    WebContents webContents = getActiveWebContents();
    if (webContents != null &&
        (getJavaSwitches().containsKey(JavaSwitches.DELAY_FREEZE_ON_BACKGROUND) ||
         getJavaSwitches().containsKey(JavaSwitches.ENABLE_FREEZE))) {
      // document.onresume event
      webContents.onResume();
    }
    // visibility:visible event
    updateShellActivityVisible(mWasDisplayOn);
    MemoryPressureMonitor.INSTANCE.enablePolling(false);

    StartupGuard.getInstance().setStartupMilestone(11);
  }

  @Override
  protected void onPause() {
    mPhysicalBackKeyPressed = false;
    CobaltContentBrowserClient.dispatchBlur();
    super.onPause();
  }

  @Override
  protected void onStop() {
    unregisterDisplayListener();
    getStarboardBridge().onActivityStop(this);
    super.onStop();

    // visibility:hidden event
    updateShellActivityVisible(false);
    WebContents webContents = getActiveWebContents();
    if (webContents != null) {
      if (getJavaSwitches().containsKey(JavaSwitches.DELAY_FREEZE_ON_BACKGROUND)) {
        if (mFreezeRunnable != null) {
          mHandler.removeCallbacks(mFreezeRunnable);
        }
        mFreezeRunnable = new Runnable() {
          @Override
          public void run() {
            WebContents currentWebContents = getActiveWebContents();
            if (currentWebContents != null) {
              currentWebContents.onFreeze();
            }
            mFreezeRunnable = null;
          }
        };
        mHandler.postDelayed(mFreezeRunnable, 1500);
      } else if (getJavaSwitches().containsKey(JavaSwitches.ENABLE_FREEZE)) {
        // If ENABLE_FREEZE is specified, fire freeze event immediately
        webContents.onFreeze();
      }
    }

    if (VideoSurfaceView.getCurrentSurface() != null) {
      mForceCreateNewVideoSurfaceView = true;
    }
    MemoryPressureMonitor.INSTANCE.disablePolling();

    // Set the SurfaceView to fullscreen.
    View rootView = getWindow().getDecorView();
    setVideoSurfaceBounds(0, 0, rootView.getWidth(), rootView.getHeight());
  }

  @Override
  protected void onResume() {
    super.onResume();
    StartupGuard.getInstance().setStartupMilestone(12);
    View rootView = getWindow().getDecorView().getRootView();
    if (rootView != null && rootView.isAttachedToWindow() && !rootView.hasFocus()) {
      rootView.requestFocus();
      Log.i(TAG, "Request focus on the root view on resume.");
    }
    CobaltContentBrowserClient.dispatchFocus();
    StartupGuard.getInstance().setStartupMilestone(13);
  }

  @Override
  protected void onDestroy() {
    unregisterDisplayListener();
    if (mFreezeRunnable != null) {
      mHandler.removeCallbacks(mFreezeRunnable);
      mFreezeRunnable = null;
    }
    if (mShellManager != null) {
      mShellManager.destroy();
    }
    mWindowAndroid.destroy();
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      OnBackInvokedHelper.unregister(this, mBackInvokedCallback);
      mBackInvokedCallback = null;
    }
    super.onDestroy();
    getStarboardBridge().onActivityDestroy(this);
  }

  @Override
  public boolean onSearchRequested() {
    return getStarboardBridge().onSearchRequested();
  }

  /** Returns true if the argument list contains an arg starting with argName. */
  private static boolean hasArg(List<String> args, String argName) {
    for (String arg : args) {
      if (arg.startsWith(argName)) {
        return true;
      }
    }
    return false;
  }

  /**
   * Overridden by Kimono to provide specific Java switch configurations.
   */
  protected Map<String, String> getJavaSwitches() {
    return this.mJavaSwitches;
  }

  /**
   * Get argv/argc style args, if any from intent extras. Returns empty array if there are none
   *
   * <p>To use, invoke application via, eg, adb shell am start --esa args arg1,arg2 \
   * dev.cobalt.coat/dev.cobalt.app.MainActivity
   */
  protected String[] getArgs() {
    String[] commandLineArgs = null;
    Intent intent = getIntent();
    if (!isReleaseBuild()) {
      commandLineArgs = getCommandLineParamsFromIntent(intent, COMMAND_LINE_ARGS_KEY);
    }
    return constructArgs(commandLineArgs, getActivityMetaData(), intent.getExtras());
  }

  @VisibleForTesting
  static String[] constructArgs(String[] commandLineArgs, Bundle metaData, Bundle extras) {
    ArrayList<String> args = new ArrayList<>();
    if (commandLineArgs != null) {
      args.addAll(Arrays.asList(commandLineArgs));
    }

    // If the URL arg isn't specified, get it from AndroidManifest.xml.
    if (!hasArg(args, URL_ARG) && metaData != null) {
      String url = metaData.getString(META_DATA_APP_URL);
      if (url != null) {
        args.add(URL_ARG + url);
      }
    }

    CharSequence[] urlParams = (extras == null) ? null : extras.getCharSequenceArray("url_params");
    if (urlParams != null) {
      appendUrlParamsToUrl(args, urlParams);
    }

    return args.toArray(new String[0]);
  }

  @VisibleForTesting
  static void appendUrlParamsToUrl(List<String> args, CharSequence[] urlParams) {
    int idx = -1;
    for (int i = 0; i < args.size(); i++) {
      if (args.get(i).startsWith(URL_ARG)) {
        idx = i;
        break;
      }
    }

    if (idx >= 0) {
      StringBuilder urlBuilder = new StringBuilder();
      urlBuilder.append(args.get(idx));
      // append & if ? is already in the url, otherwise append ?
      if (urlBuilder.indexOf("?") > 0) {
        urlBuilder.append("&");
      } else {
        urlBuilder.append("?");
      }

      for (int j = 0; j < urlParams.length; j++) {
        // sanitize the input before append to the url.
        String paramKeyValuePair = urlParams[j].toString();
        if (URL_PARAM_PATTERN.matcher(paramKeyValuePair).matches()) {
          urlBuilder.append(paramKeyValuePair);
          urlBuilder.append('&');
        }
      }

      urlBuilder.deleteCharAt(urlBuilder.length() - 1);
      args.set(idx, urlBuilder.toString());
    }
  }

  protected boolean isReleaseBuild() {
    return StarboardBridge.isReleaseBuild();
  }
  protected boolean isDevelopmentBuild() {
    return StarboardBridge.isDevelopmentBuild();
  }

  @Override
  protected void onNewIntent(Intent intent) {
    getStarboardBridge().handleDeepLink(getIntentUrlAsString(intent));
  }

  /**
   * Returns the URL from an Intent as a string. This may be overridden for additional processing.
   */
  protected String getIntentUrlAsString(Intent intent) {
    Uri intentUri = intent.getData();
    return (intentUri == null) ? "" : intentUri.toString();
  }

  @Override
  public void onRequestPermissionsResult(
      int requestCode, String[] permissions, int[] grantResults) {
    getStarboardBridge().onRequestPermissionsResult(requestCode, permissions, grantResults);
  }

  public void resetVideoSurface() {
    if (mIsCobaltUsingAndroidOverlay) {
      return;
    }
    runOnUiThread(
        new Runnable() {
          @Override
          public void run() {
            createNewSurfaceView();
          }
        });
  }

  public void setVideoSurfaceBounds(final int x, final int y, final int width, final int height) {
    if (mIsCobaltUsingAndroidOverlay) {
      return;
    }
    if (width == 0 || height == 0) {
      // The SurfaceView should be covered by our UI layer in this case.
      return;
    }
    runOnUiThread(
        new Runnable() {
          @Override
          public void run() {
            LayoutParams layoutParams = mVideoSurfaceView.getLayoutParams();
            // Since mVideoSurfaceView is added directly to the Activity's content view, which is a
            // FrameLayout, we expect its layout params to become FrameLayout.LayoutParams.
            if (layoutParams instanceof FrameLayout.LayoutParams) {
              ((FrameLayout.LayoutParams) layoutParams).setMargins(x, y, x + width, y + height);
            } else {
              Log.w(
                  TAG,
                  "Unexpected video surface layout params class "
                      + layoutParams.getClass().getName());
            }
            layoutParams.width = width;
            layoutParams.height = height;
            // Even though as a NativeActivity we're not using the Android UI framework, by setting
            // the  layout params it will force a layout to be requested. That will cause the
            // SurfaceView to position its underlying Surface to match the screen coordinates of
            // where the view would be in a UI layout and to set the surface transform matrix to
            // match the view's size.
            mVideoSurfaceView.setLayoutParams(layoutParams);
          }
        });
  }

  private void createNewSurfaceView() {
    if (mIsCobaltUsingAndroidOverlay) {
      return;
    }
    ViewParent parent = mVideoSurfaceView.getParent();
    if (parent instanceof FrameLayout) {
      FrameLayout frameLayout = (FrameLayout) parent;
      int index = frameLayout.indexOfChild(mVideoSurfaceView);
      frameLayout.removeView(mVideoSurfaceView);
      Log.i(TAG, "removed mVideoSurfaceView at index:" + index);

      mVideoSurfaceView = new VideoSurfaceView(this);
      frameLayout.addView(
          mVideoSurfaceView,
          index,
          new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
      Log.i(TAG, "inserted new mVideoSurfaceView at index:" + index);
    } else {
      Log.w(TAG, "Unexpected surface view parent class " + parent.getClass().getName());
    }
  }

  public long getAppStartTimestamp() {
    return mTimeInNanoseconds;
  }

  public void evaluateJavaScript(String jsCode) {
    // evaluateJavaScript must run on UI thread.
    runOnUiThread(
        new Runnable() {
          @Override
          public void run() {
            WebContents webContents = getActiveWebContents();
            if (webContents != null) {
              webContents.evaluateJavaScript(jsCode, null);
            }
          }
        });
  }

  public void toggleKeepScreenOn(boolean keepOn) {
    if (mIsKeepScreenOnEnabled != keepOn) {
      runOnUiThread(
          new Runnable() {
            @Override
            public void run() {
              if (keepOn) {
                getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                Log.i(TAG, "Screen keep-on enabled for video playback");
              } else {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                Log.i(TAG, "Screen keep-on disabled");
              }
            }
          });
      mIsKeepScreenOnEnabled = keepOn;
    }
  }

  private void updateShellActivityVisible(boolean isVisible) {
    if (mShellManager != null) {
      mShellManager.onActivityVisible(isVisible);
    }
  }

  private boolean isDisplayOn() {
    Display defaultDisplay = DisplayUtil.getDefaultDisplay();
    if (defaultDisplay == null) {
      DisplayUtil.cacheDefaultDisplay(this);
      defaultDisplay = DisplayUtil.getDefaultDisplay();
    }
    if (defaultDisplay == null) {
      // Fail-safe: assume display is ON if we cannot query it
      return true;
    }
    int state = defaultDisplay.getState();
    // If the state is unknown, we fail-safe to true (display ON) to prevent
    // rendering freezes or black screens on devices that do not report display state correctly.
    return state == Display.STATE_ON || state == Display.STATE_UNKNOWN;
  }

  private void checkDisplayState() {
    boolean isDisplayOn = isDisplayOn();
    if (isDisplayOn != mWasDisplayOn) {
      mWasDisplayOn = isDisplayOn;
      Log.i(TAG, "Display state changed: isDisplayOn = " + isDisplayOn);
      updateShellActivityVisible(isDisplayOn);
    }
  }

  private void registerDisplayListener() {
    DisplayUtil.registerListener(mDisplayListener);
  }

  private void unregisterDisplayListener() {
    DisplayUtil.unregisterListener(mDisplayListener);
  }

  @RequiresApi(api = 33)
  private static class OnBackInvokedHelper {
    private static final Handler sHandler = new Handler(Looper.getMainLooper());

    static Object register(final CobaltActivity activity) {
      OnBackInvokedCallback callback = new OnBackInvokedCallback() {
        @Override
        public void onBackInvoked() {
          if (activity.mPhysicalBackKeyPressed) {
            return; // Bypassed: physical key events are already driving this navigation
          }

          // 1. Dispatch keydown to initiate navigation immediately.
          activity.dispatchKeyEventToIme(KeyEvent.KEYCODE_BACK, KeyEvent.ACTION_DOWN);

          // 2. Simulate physical key release with a 100ms delay.
          // This mimics natural user latency and prevents the web page's focus manager
          // from receiving 'keyup' prematurely during asynchronous page transitions
          // (which would otherwise disrupt cursor/spatial navigation focus restoration).
          sHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
              if (activity.isDestroyed() || activity.isFinishing()) {
                return; // Avoid memory leaks or dispatching keyup to a destroyed activity
              }
              activity.dispatchKeyEventToIme(KeyEvent.KEYCODE_BACK, KeyEvent.ACTION_UP);
            }
          }, 100);
        }
      };
      activity.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
          OnBackInvokedDispatcher.PRIORITY_DEFAULT,
          callback
      );
      return callback;
    }

    static void unregister(CobaltActivity activity, Object callback) {
      if (callback instanceof OnBackInvokedCallback) {
        activity.getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(
            (OnBackInvokedCallback) callback
        );
      }
    }
  }
}
