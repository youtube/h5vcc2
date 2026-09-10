package dev.cobalt.shell;

import static dev.cobalt.shell.Shell.TAG;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import org.chromium.base.PathUtils;

/**
 * This class crashes the application if scheduled and not disarmed before its timer expires.
 *
 * <p>This can be used to mitigate applications being stuck and not able to make progress.
 *
 * <p>Intentionally crashing allows the system to capture a stack trace and potentially restart the
 * application, rather than leaving the user stuck on an unresponsive black screen.
 *
 * <p>StartupGuard also serves as the persistence bridge for the Chromium UMA funnel. Before the C++
 * JNI library (`libcobalt.so`) is fully unpacked and initialized, early startup milestones (1-4)
 * are recorded here. The bitmask is written to disk via a bare-metal `MappedByteBuffer` (bypassing
 * the heap) so it easily survives watchdog crashes. The C++ `ShellBrowserMainParts` harvester
 * collects this file on the subsequent boot and logs the rescued events to
 * `Cobalt.Startup.MilestoneReached`.
 */
public class StartupGuard {
  public static final String STARTUP_STATE_FILE_NAME = "java_startup_state.bin";
  public static final String STARTUP_STATE_PREVIOUS_FILE_NAME = "java_startup_state_previous.bin";

  private final Handler mHandler;
  private final Runnable mCrashRunnable;
  private final AtomicLong mStartupStatus = new AtomicLong(0L);
  private final Map<String, String> mDiagnosisInfo = new HashMap<>();
  private final AtomicBoolean mIsArmed = new AtomicBoolean(false);
  private final Object mStateLock = new Object();

  private static class LazyHolder {
    private static final StartupGuard INSTANCE = new StartupGuard();
  }

  // Backing memory-mapped file for Phase 1 cross-layer UMA persistence
  private MappedByteBuffer mStartupStateBuffer = null;

  // Private constructor prevents direct instantiation from other classes
  private StartupGuard() {
    // We attach the mHandler to the Main Looper to ensure the crash occurs on the UI thread
    mHandler = new Handler(Looper.getMainLooper());

    mCrashRunnable =
        new Runnable() {
          @Override
          public void run() {
            mIsArmed.set(false);
            throw new RuntimeException(
                "Application startup may not have succeeded, crash triggered by StartupGuard. "
                    + getStartupStatusAndDiagnosisInfo());
          }
        };
  }

  public void initializePersistence(Context context) {
    initializePersistenceInternal(context, null);
  }

  @VisibleForTesting
  public void initializePersistenceInternal(Context context, @Nullable File baseDir) {
    try {
      // Default to Chromium's data directory so it matches C++ DIR_ANDROID_APP_DATA.
      File dir = baseDir != null ? baseDir : new File(PathUtils.getDataDirectory());
      if (!dir.exists() && !dir.mkdirs()) {
        Log.e(TAG, "Failed to create directory for startup state.");
        return;
      }

      File file = new File(dir, STARTUP_STATE_FILE_NAME);
      // If a file from the previous session exists, rename it so C++ can harvest the previous
      // session's state without racing with this fresh session's writes.
      if (file.exists()) {
        File prevFile = new File(dir, STARTUP_STATE_PREVIOUS_FILE_NAME);
        if (prevFile.exists() && !prevFile.delete()) {
          Log.w(TAG, "Failed to delete old previous state file.");
        }
        if (!file.renameTo(prevFile)) {
          Log.w(TAG, "Failed to rename current state to previous state.");
        }
      }

      try (RandomAccessFile raf = new RandomAccessFile(file, "rw");
          FileChannel channel = raf.getChannel()) {
        // MappedByteBuffer defaults to big-endian, we strictly need little-endian for C++.
        mStartupStateBuffer = channel.map(FileChannel.MapMode.READ_WRITE, 0, 8);
        mStartupStateBuffer.order(ByteOrder.LITTLE_ENDIAN);
        mStartupStateBuffer.putLong(0, 0);
      }
    } catch (IOException e) {
      Log.e(TAG, "Failed to map startup state file: " + e.getMessage());
    }
  }

  private String getStartupStatusAndDiagnosisInfo() {
    StringBuilder message = new StringBuilder();
    message.append("Status: 0x");
    message.append(Long.toHexString(mStartupStatus.get()));
    synchronized (mDiagnosisInfo) {
      if (!mDiagnosisInfo.isEmpty()) {
        message.append(", Diagnosis Info: ");
        message.append(mDiagnosisInfo.toString());
      }
    }
    return message.toString();
  }

  /**
   * Returns the single instance of StartupGuard. Uses the Initialization-on-demand holder idiom for
   * thread-safe lazy loading.
   */
  public static StartupGuard getInstance() {
    return LazyHolder.INSTANCE;
  }

  /**
   * Sets a milestone bit in the startup status.
   *
   * @param milestone The milestone to set, 0-indexed (0-63).
   */
  public void setStartupMilestone(int milestone) {
    if (milestone < 0 || milestone >= 64) {
      Log.e(TAG, "Invalid milestone: " + milestone);
      return;
    }
    Log.v(TAG, "StartupGuard setStartupMilestone:" + milestone);
    long mask = 1L << milestone;

    // Synchronize to ensure atomic write-through to the disk buffer without interleaving
    synchronized (mStateLock) {
      long current = mStartupStatus.updateAndGet(curr -> curr | mask);
      if (mStartupStateBuffer != null) {
        mStartupStateBuffer.putLong(0, current);
      }
    }
  }

  /**
   * Sets startup diagnosis info.
   *
   * @param key The key for the diagnosis info.
   * @param value The value for the diagnosis info.
   */
  public void setDiagnosisInfo(String key, String value) {
    synchronized (mDiagnosisInfo) {
      Log.v(TAG, "StartupGuard setDiagnosisInfo: " + key + "=" + value);
      mDiagnosisInfo.put(key, value);
    }
  }

  /**
   * Schedules the forced crash to happen after the specified delay.
   *
   * @param delaySeconds The delay in seconds before the crash is triggered.
   */
  public void scheduleCrash(long delaySeconds) {
    if (mIsArmed.compareAndSet(/* expect= */ false, /* update= */ true)) {
      mHandler.postDelayed(mCrashRunnable, delaySeconds * 1000);
      Log.i(TAG, "StartupGuard scheduled crash in " + delaySeconds + " seconds.");
    } else {
      Log.w(
          TAG,
          "StartupGuard fail to schedule crash, because there is already a pending crash"
              + " scheduled.");
    }
  }

  /** Cancels the pending crash job. */
  public void disarm() {
    if (mIsArmed.compareAndSet(/* expect= */ true, /* update= */ false)) {
      mHandler.removeCallbacks(mCrashRunnable);
      Log.i(TAG, "StartupGuard cancelled crash. " + getStartupStatusAndDiagnosisInfo());
    }
  }

  /** Checks if the forced crash is currently scheduled. */
  @VisibleForTesting
  public boolean isArmed() {
    return mIsArmed.get();
  }

  /** Returns the runnable that triggers the forced crash. */
  @VisibleForTesting
  public Runnable getCrashRunnable() {
    return mCrashRunnable;
  }

  @VisibleForTesting
  public void resetForTesting() {
    synchronized (mStateLock) {
      mStartupStatus.set(0);
      mIsArmed.set(false);
      mStartupStateBuffer = null;
    }
  }
}
