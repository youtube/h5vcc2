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

package dev.cobalt.media;

import static dev.cobalt.media.Log.TAG;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.PlaybackParams;
import android.os.Build;
import androidx.annotation.GuardedBy;
import androidx.annotation.RequiresApi;
import dev.cobalt.util.Log;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Locale;
import org.jni_zero.CalledByNative;
import org.jni_zero.CalledByNativeUnchecked;
import org.jni_zero.JNINamespace;

/**
 * A wrapper of the android AudioTrack class. Android AudioTrack would not start playing until the
 * buffer is fully filled once.
 */
@JNINamespace("starboard")
public class AudioTrackBridge {
  // Also used by AudioOutputManager.
  static final int AV_SYNC_HEADER_V1_SIZE = 16;

  private AudioTrack mAudioTrack;

  // mRawAudioTimestamp and mAudioTimestamp are defined as member variables to avoid object
  // allocation in getAudioTimestamp(), which is on a hot path for audio processing.
  // mRawAudioTimestamp is used to retrieve the timestamp directly from android.media.AudioTrack.
  // mAudioTimestamp is a wrapper that ensures the framePosition is monotonically increasing
  // before it is passed to the native side.
  private android.media.AudioTimestamp mRawAudioTimestamp = new android.media.AudioTimestamp();
  private AudioTimestamp mAudioTimestamp = new AudioTimestamp(0, 0);

  private final Object mPositionLock = new Object();
  @GuardedBy("mPositionLock")
  private long mMaxFramePositionSoFar = 0;

  private final boolean mTunnelModeEnabled;
  // The following variables are used only when |tunnelModeEnabled| is true.
  private ByteBuffer mAvSyncHeader;
  private int mAvSyncPacketBytesRemaining;

  // Pre-allocated byte[] or float[] shared with C++ via getPreAllocatedAudioDataAs*Array() to avoid
  // allocation on every WriteSample(). C++ writes directly to this array,
  // which is then passed back to Java write*() methods.
  private Object mPreAllocatedAudioData;

  private static int getBytesPerSample(int audioFormat) {
    switch (audioFormat) {
      case AudioFormat.ENCODING_PCM_16BIT:
        return 2;
      case AudioFormat.ENCODING_PCM_FLOAT:
        return 4;
      case AudioFormat.ENCODING_INVALID:
      default:
        throw new RuntimeException("Unsupported audio format " + audioFormat);
    }
  }

  private static Object createAudioDataArray(int sampleType, int maxSamplesPerWrite) {
    switch (sampleType) {
      case AudioFormat.ENCODING_PCM_FLOAT:
        return new float[maxSamplesPerWrite];
      case AudioFormat.ENCODING_PCM_16BIT:
        return new byte[maxSamplesPerWrite * getBytesPerSample(sampleType)];
      case AudioFormat.ENCODING_AC3:
      case AudioFormat.ENCODING_E_AC3:
        return new byte[maxSamplesPerWrite];
      default:
        // Throwing RuntimeException crashes the app, which is intended since the invariant is broken.
        throw new IllegalArgumentException("Unsupported sample type: " + sampleType);
    }
  }

  // TODO: Pass error details to caller.
  public AudioTrackBridge(
      int sampleType,
      int sampleRate,
      int channelCount,
      int maxSamplesPerWrite,
      int preferredBufferSizeInBytes,
      int tunnelModeAudioSessionId,
      boolean isWebAudio) {

    mTunnelModeEnabled = tunnelModeAudioSessionId != TunnelModeAudioSessionId.NONE;
    int channelConfig;
    switch (channelCount) {
      case 1:
        channelConfig = AudioFormat.CHANNEL_OUT_MONO;
        break;
      case 2:
        channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
        break;
      case 6:
        channelConfig = AudioFormat.CHANNEL_OUT_5POINT1;
        break;
      default:
        throw new RuntimeException("Unsupported channel count: " + channelCount);
    }

    AudioAttributes attributes;
    if (mTunnelModeEnabled) {
      // Android 9.0 (Build.VERSION.SDK_INT >= 28) support v2 sync header that aligns sync header
      // with audio frame size. V1 sync header has alignment issues for multi-channel audio.
      if (Build.VERSION.SDK_INT < 28) {
        int frameSize = getBytesPerSample(sampleType) * channelCount;
        // This shouldn't happen as it should have been checked in
        // AudioOutputManager.generateTunnelModeAudioSessionId().
        if (AV_SYNC_HEADER_V1_SIZE % frameSize != 0) {
          mAudioTrack = null;
          String errorMessage =
              String.format(
                  Locale.US,
                  "Enable tunnel mode when frame size is unaligned, "
                      + "sampleType: %d, channel: %d, sync header size: %d.",
                  sampleType,
                  channelCount,
                  AV_SYNC_HEADER_V1_SIZE);
          Log.e(TAG, errorMessage);
          throw new RuntimeException(errorMessage);
        }
      }
      attributes =
          new AudioAttributes.Builder()
              .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
              .setFlags(AudioAttributes.FLAG_HW_AV_SYNC)
              .setUsage(AudioAttributes.USAGE_MEDIA)
              .build();
    } else {
      final int usage =
          isWebAudio ? AudioAttributes.USAGE_NOTIFICATION : AudioAttributes.USAGE_MEDIA;
      // TODO: Support ENCODING_E_AC3_JOC for api level 28 or later.
      final boolean isSurround =
          sampleType == AudioFormat.ENCODING_AC3 || sampleType == AudioFormat.ENCODING_E_AC3;
      final boolean useContentTypeMovie = isSurround || !isWebAudio;
      attributes =
          new AudioAttributes.Builder()
              .setContentType(
                  useContentTypeMovie
                      ? AudioAttributes.CONTENT_TYPE_MOVIE
                      : AudioAttributes.CONTENT_TYPE_MUSIC)
              .setUsage(usage)
              .build();
    }
    AudioFormat format =
        new AudioFormat.Builder()
            .setEncoding(sampleType)
            .setSampleRate(sampleRate)
            .setChannelMask(channelConfig)
            .build();

    int audioTrackBufferSize = preferredBufferSizeInBytes;
    // TODO: Investigate if this implementation could be refined.
    // It is not necessary to loop until 0 since there is new implementation based on
    // AudioTrack.getMinBufferSize(). Especially for tunnel mode, it would fail if audio HAL does
    // not support tunnel mode and then it is not helpful to retry.
    while (audioTrackBufferSize > 0) {
      try {
        mAudioTrack =
            new AudioTrack(
                attributes,
                format,
                audioTrackBufferSize,
                AudioTrack.MODE_STREAM,
                mTunnelModeEnabled
                    ? tunnelModeAudioSessionId
                    : AudioManager.AUDIO_SESSION_ID_GENERATE);
      } catch (Exception e) {
        mAudioTrack = null;
      }
      // AudioTrack ctor can fail in multiple, platform specific ways, so do a thorough check
      // before proceed.
      if (mAudioTrack != null) {
        if (mAudioTrack.getState() == AudioTrack.STATE_INITIALIZED) {
          break;
        }
        mAudioTrack = null;
      }
      audioTrackBufferSize /= 2;
    }

    String sampleTypeString = "ENCODING_INVALID";
    if (isAudioTrackValid()) {
      // If the AudioTrack encoding indicates compressed data,
      // e.g. AudioFormat.ENCODING_AC3, then the frame count returned
      // is the size of the AudioTrack buffer in bytes.
      // In such cases, audioTrackBufferSize does not have to be
      // multiplied with bytes per sample and channel count below.
      audioTrackBufferSize = mAudioTrack.getBufferSizeInFrames();
      switch (sampleType) {
        case AudioFormat.ENCODING_PCM_16BIT:
          sampleTypeString = "ENCODING_PCM_16BIT";
          audioTrackBufferSize *= getBytesPerSample(sampleType) * channelCount;
          break;
        case AudioFormat.ENCODING_PCM_FLOAT:
          sampleTypeString = "ENCODING_PCM_FLOAT";
          audioTrackBufferSize *= getBytesPerSample(sampleType) * channelCount;
          break;
        case AudioFormat.ENCODING_AC3:
          sampleTypeString = "ENCODING_AC3";
          break;
        case AudioFormat.ENCODING_E_AC3:
          sampleTypeString = "ENCODING_E_AC3";
          break;
        default:
          Log.i(TAG, String.format(Locale.US, "Unknown AudioFormat %d.", sampleType));
          break;
      }

      mPreAllocatedAudioData = createAudioDataArray(sampleType, maxSamplesPerWrite);
    }
    Log.i(
        TAG,
        "AudioTrack created with AudioFormat %s and buffer size %d (preferred: %d)."
            + " The minimum buffer size is %d.",
        sampleTypeString,
        audioTrackBufferSize,
        preferredBufferSizeInBytes,
        AudioTrack.getMinBufferSize(sampleRate, channelConfig, sampleType));
  }

  public boolean isAudioTrackValid() {
    return mAudioTrack != null;
  }

  public void release() {
    if (mAudioTrack != null) {
      mAudioTrack.release();
    }
    mAudioTrack = null;
    mAvSyncHeader = null;
    mAvSyncPacketBytesRemaining = 0;
  }

  @CalledByNative
  public boolean setPlaybackRate(float playbackRate) {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to setPlaybackRate with NULL audio track.");
      return false;
    }
    if (!mTunnelModeEnabled) {
      Log.i(TAG, "Skip SetPlaybackRate for non tunnel mode tracks.");
      return true;
    }

    try {
      PlaybackParams params = mAudioTrack.getPlaybackParams();
      params.setSpeed(playbackRate);
      mAudioTrack.setPlaybackParams(params);
    } catch (IllegalArgumentException | IllegalStateException e) {
      Log.e(TAG, String.format("Unable to setPlaybackRate, error: %s", e.toString()));
      return false;
    }
    return true;
  }

  @CalledByNative
  public int setVolume(float gain) {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to setVolume with NULL audio track.");
      return AudioTrack.ERROR_INVALID_OPERATION;
    }
    return mAudioTrack.setVolume(gain);
  }

  @CalledByNative
  public int getPlayState() {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to getPlayState with NULL audio track.");
      return AudioTrack.PLAYSTATE_STOPPED;
    }
    return mAudioTrack.getPlayState();
  }

  @CalledByNative
  public float[] getPreAllocatedAudioDataAsFloatArray() {
    if (mPreAllocatedAudioData instanceof float[]) {
      return (float[]) mPreAllocatedAudioData;
    }
    return null;
  }

  @CalledByNative
  public byte[] getPreAllocatedAudioDataAsByteArray() {
    if (mPreAllocatedAudioData instanceof byte[]) {
      return (byte[]) mPreAllocatedAudioData;
    }
    return null;
  }

  // TODO (b/262608024): Have this method return a boolean and return false on failure.
  @CalledByNative
  private void play() {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to play with NULL audio track.");
      return;
    }
    try {
      mAudioTrack.play();
    } catch (IllegalStateException e) {
      Log.e(TAG, String.format(Locale.US, "Unable to play audio track, error: %s", e.toString()));
    }
  }

  // TODO (b/262608024): Have this method return a boolean and return false on failure.
  @CalledByNativeUnchecked
  private void pause() {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to pause with NULL audio track.");
      return;
    }
    try {
      mAudioTrack.pause();
    } catch (IllegalStateException e) {
      Log.e(TAG, String.format(Locale.US, "Unable to pause audio track, error: %s", e.toString()));
    }
  }

  // TODO (b/262608024): Have this method return a boolean and return false on failure.
  @CalledByNativeUnchecked
  private void stop() {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to stop with NULL audio track.");
      return;
    }
    try {
      mAudioTrack.stop();
    } catch (IllegalStateException e) {
      Log.e(TAG, String.format(Locale.US, "Unable to stop audio track, error: %s", e.toString()));
    }
  }

  @CalledByNativeUnchecked
  private void flush() {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to flush with NULL audio track.");
      return;
    }
    mAudioTrack.flush();
    // Reset the states to allow reuse of |audioTrack| after flush() is called. This can reduce
    // switch latency for passthrough playbacks.
    mAvSyncHeader = null;
    mAvSyncPacketBytesRemaining = 0;
    synchronized (mPositionLock) {
      mMaxFramePositionSoFar = 0;
    }
  }

  @CalledByNative
  private int writeWithPresentationTime(byte[] audioData, int sizeInBytes, long presentationTimeInMicroseconds) {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to write with NULL audio track.");
      return 0;
    }

    if (mTunnelModeEnabled) {
      return writeWithAvSync(audioData, sizeInBytes, presentationTimeInMicroseconds);
    }

    return mAudioTrack.write(audioData, 0, sizeInBytes, AudioTrack.WRITE_NON_BLOCKING);
  }

  private int writeWithAvSync(
      byte[] audioData, int sizeInBytes, long presentationTimeInMicroseconds) {
    if (mAudioTrack == null) {
      throw new RuntimeException("writeWithAvSync() is called when audioTrack is null.");
    }

    if (!mTunnelModeEnabled) {
      throw new RuntimeException("writeWithAvSync() is called when tunnelModeEnabled is false.");
    }

    long presentationTimeInNanoseconds = presentationTimeInMicroseconds * 1000;

    // Android support tunnel mode from 5.0 (API level 21), but the app has to manually write the
    // sync header before API 23, where the write() function with presentation timestamp is
    // introduced.
    // Set the following constant to |false| to test manual sync header writing in API level 23 or
    // later.  Note that the code to write sync header manually only supports v1 sync header.
    final boolean useAutoSyncHeaderWrite = true;
    if (useAutoSyncHeaderWrite) {
      ByteBuffer byteBuffer = ByteBuffer.wrap(audioData);
      return mAudioTrack.write(
          byteBuffer, sizeInBytes, AudioTrack.WRITE_NON_BLOCKING, presentationTimeInNanoseconds);
    }

    if (mAvSyncHeader == null) {
      mAvSyncHeader = ByteBuffer.allocate(AV_SYNC_HEADER_V1_SIZE);
      mAvSyncHeader.order(ByteOrder.BIG_ENDIAN);
      mAvSyncHeader.putInt(0x55550001);
    }

    if (mAvSyncPacketBytesRemaining == 0) {
      mAvSyncHeader.putInt(4, sizeInBytes);
      mAvSyncHeader.putLong(8, presentationTimeInNanoseconds);
      mAvSyncHeader.position(0);
      mAvSyncPacketBytesRemaining = sizeInBytes;
    }

    if (mAvSyncHeader.remaining() > 0) {
      int ret =
          mAudioTrack.write(mAvSyncHeader, mAvSyncHeader.remaining(), AudioTrack.WRITE_NON_BLOCKING);
      if (ret < 0) {
        mAvSyncPacketBytesRemaining = 0;
        return ret;
      }
      if (mAvSyncHeader.remaining() > 0) {
        return 0;
      }
    }

    int sizeToWrite = Math.min(mAvSyncPacketBytesRemaining, sizeInBytes);
    ByteBuffer byteBuffer = ByteBuffer.wrap(audioData);
    int ret = mAudioTrack.write(byteBuffer, sizeToWrite, AudioTrack.WRITE_NON_BLOCKING);
    if (ret < 0) {
      mAvSyncPacketBytesRemaining = 0;
      return ret;
    }
    mAvSyncPacketBytesRemaining -= ret;
    return ret;
  }

  @CalledByNative
  private int write(float[] audioData, int sizeInFloats) {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to write with NULL audio track.");
      return 0;
    }
    if (mTunnelModeEnabled) {
      throw new RuntimeException("Float sample is not supported under tunnel mode.");
    }
    return mAudioTrack.write(audioData, 0, sizeInFloats, AudioTrack.WRITE_NON_BLOCKING);
  }

  @CalledByNative
  private AudioTimestamp getAudioTimestamp() {
    // TODO: Consider calling with TIMEBASE_MONOTONIC and returning that
    // information to the starboard audio sink.
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to getAudioTimestamp with NULL audio track.");
      return mAudioTimestamp;
    }
    // The `synchronized` is required as `maxFramePositionSoFar` can also be modified in flush().
    // TODO: Consider refactor the code to remove the dependency on `synchronized`.
    synchronized (mPositionLock) {
      boolean success = false;
      try {
        success = mAudioTrack.getTimestamp(mRawAudioTimestamp);
      } catch (Exception | OutOfMemoryError e) {
        Log.e(TAG, "Failed to getAudioTimestamp", e);
      }

      if (success) {
        // This conversion is safe, as only the lower bits will be set, since we
        // called |getTimestamp| without a timebase.
        // https://developer.android.com/reference/android/media/AudioTimestamp.html#framePosition
        mAudioTimestamp.mFramePosition = mRawAudioTimestamp.framePosition & 0x7FFFFFFF;
        mAudioTimestamp.mNanoTime = mRawAudioTimestamp.nanoTime;
      } else {
        // Time stamps haven't been updated yet, assume playback hasn't started.
        mAudioTimestamp.mFramePosition = 0;
        mAudioTimestamp.mNanoTime = System.nanoTime();
      }

      if (mAudioTimestamp.getFramePosition() > mMaxFramePositionSoFar) {
        mMaxFramePositionSoFar = mAudioTimestamp.getFramePosition();
      } else {
        // The returned |audioTimestamp.mFramePosition| is not monotonically
        // increasing, and a monotonically increastion frame position is
        // required to calculate the playback time correctly, because otherwise
        // we would be going back in time.
        mAudioTimestamp.mFramePosition = mMaxFramePositionSoFar;
      }
    }

    return mAudioTimestamp;
  }

  @CalledByNative
  private int getUnderrunCount() {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to call getUnderrunCount() with NULL audio track.");
      return 0;
    }
    return mAudioTrack.getUnderrunCount();
  }

  @CalledByNative
  private int getStartThresholdInFrames() {
    if (Build.VERSION.SDK_INT >= 31) {
      return getStartThresholdInFramesV31();
    }
    return 0;
  }

  /** A wrapper of the android AudioTimestamp class to be used by JNI. */
  private static class AudioTimestamp {
    private long mFramePosition;
    private long mNanoTime;

    public AudioTimestamp(long framePosition, long nanoTime) {
      mFramePosition = framePosition;
      mNanoTime = nanoTime;
    }



    @CalledByNative("AudioTimestamp")
    public long getFramePosition() {
      return mFramePosition;
    }

    @CalledByNative("AudioTimestamp")
    public long getNanoTime() {
      return mNanoTime;
    }
  }

  @RequiresApi(31)
  private int getStartThresholdInFramesV31() {
    if (mAudioTrack == null) {
      Log.e(TAG, "Unable to call getStartThresholdInFrames() with NULL audio track.");
      return 0;
    }
    return mAudioTrack.getStartThresholdInFrames();
  }
}
