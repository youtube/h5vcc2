package dev.cobalt.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import androidx.test.core.app.ApplicationProvider;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class StartupGuardPersistenceTest {
  private Context mContext;

  @Before
  public void setUp() {
    mContext = ApplicationProvider.getApplicationContext();
    // Clean up previous files if any
    new File(mContext.getFilesDir(), StartupGuard.STARTUP_STATE_FILE_NAME).delete();
    new File(mContext.getFilesDir(), StartupGuard.STARTUP_STATE_PREVIOUS_FILE_NAME).delete();
    StartupGuard.getInstance().resetForTesting();
  }

  @Test
  public void testInitializeRenamesPreviousFile() throws Exception {
    File priorFile = new File(mContext.getFilesDir(), StartupGuard.STARTUP_STATE_FILE_NAME);
    priorFile.createNewFile();

    StartupGuard guard = StartupGuard.getInstance();
    guard.initializePersistenceInternal(mContext, mContext.getFilesDir());

    // Assert the prior file was moved to _previous
    File prevFile = new File(mContext.getFilesDir(), StartupGuard.STARTUP_STATE_PREVIOUS_FILE_NAME);
    assertTrue(prevFile.exists());
    assertTrue(new File(mContext.getFilesDir(), StartupGuard.STARTUP_STATE_FILE_NAME).exists());
  }

  @Test
  public void testSetStartupMilestoneWritesToBuffer() throws Exception {
    StartupGuard guard = StartupGuard.getInstance();
    guard.initializePersistenceInternal(mContext, mContext.getFilesDir());

    // Flip bits 1 and 3 (0x0A)
    guard.setStartupMilestone(1);
    guard.setStartupMilestone(3);

    File currentStateFile = new File(mContext.getFilesDir(), StartupGuard.STARTUP_STATE_FILE_NAME);
    assertTrue(currentStateFile.exists());

    try (RandomAccessFile raf = new RandomAccessFile(currentStateFile, "r")) {
      byte[] data = new byte[8];
      raf.readFully(data);
      ByteBuffer buffer = ByteBuffer.wrap(data);
      buffer.order(ByteOrder.LITTLE_ENDIAN);

      long storedValue = buffer.getLong();
      // bit 1 (2) + bit 3 (8) = 10 (0x0A)
      long expected = (1L << 1) | (1L << 3);

      assertEquals("Milestone bits not properly set in little-endian order", expected, storedValue);
    }
  }
}
