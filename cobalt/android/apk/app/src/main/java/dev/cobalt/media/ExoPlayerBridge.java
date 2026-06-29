// Copyright 2026 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package dev.cobalt.media;

import static dev.cobalt.media.Log.TAG;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Surface;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.media3.common.Format;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.PlaybackParameters;
import androidx.media3.common.Player;
import androidx.media3.common.Tracks;
import androidx.media3.exoplayer.DefaultLoadControl;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.analytics.AnalyticsListener;
import androidx.media3.exoplayer.source.MediaSource;
import androidx.media3.exoplayer.source.MergingMediaSource;
import androidx.media3.exoplayer.trackselection.DefaultTrackSelector;
import dev.cobalt.util.Log;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

/** Facilitates communication between the native ExoPlayerBridge and the Java ExoPlayer */
@JNINamespace("starboard")
public class ExoPlayerBridge {
  // Used in ExoPlayerSampleStream.
  public static final int TYPE_AUDIO = 0;
  public static final int TYPE_VIDEO = 1;

  private ExoPlayer mPlayer;
  private ExoPlayerMediaSource mAudioMediaSource;
  private ExoPlayerMediaSource mVideoMediaSource;
  private final long mNativeExoPlayerBridge;
  private final Handler mExoPlayerHandler;
  // The following variables are accessed on both the Handler and native threads
  private volatile long mLastPlaybackPosUsec = 0;
  private volatile long mPlaybackPosLastUpdatedMsec = 0;
  private volatile float mPlaybackRate = 1.0f;
  private volatile boolean mIsProgressing = false;
  private volatile boolean mIsReleased = false;
  private volatile long mSeekTimeUsec = 0;

  private final ExoPlayerListener mPlayerListener;
  private final DroppedFramesListener mDroppedFramesListener;

  private static final long PLAYER_RELEASE_TIMEOUT_MS = 2000; // 2 seconds.
  private static final int MIN_BUFFER_DURATION_MS = 1000; // 1 second.
  private static final int MAX_BUFFER_DURATION_MS = 5000; // 5 seconds

  private class ExoPlayerListener implements Player.Listener {
    @Override
    public void onPlaybackStateChanged(@Player.State int playbackState) {
      if (!mIsReleased) {
        switch (playbackState) {
          case Player.STATE_BUFFERING:
          case Player.STATE_IDLE:
            return;
          case Player.STATE_READY:
            ExoPlayerBridgeJni.get().onReady(mNativeExoPlayerBridge);
            break;
          case Player.STATE_ENDED:
            ExoPlayerBridgeJni.get().onEnded(mNativeExoPlayerBridge);
            break;
        }
      }
    }

    @Override
    public void onTracksChanged(Tracks tracks) {
      if (!mIsReleased) {
        ExoPlayerBridgeJni.get().onInitialized(mNativeExoPlayerBridge);
      }
    }

    @Override
    public synchronized void onIsPlayingChanged(boolean isPlaying) {
      updatePositionAnchor();
      mIsProgressing = isPlaying;
      if (!mIsReleased) {
        ExoPlayerBridgeJni.get().onIsPlayingChanged(mNativeExoPlayerBridge, isPlaying);
      }
    }

    @Override
    public void onPositionDiscontinuity(
        Player.PositionInfo oldPosition, Player.PositionInfo newPosition, int reason) {
      updatePositionAnchor();
    }

    @Override
    public void onPlaybackParametersChanged(PlaybackParameters playbackParameters) {
      updatePositionAnchor();
      mPlaybackRate = playbackParameters.speed;
    }

    @Override
    public void onPlayerError(@NonNull PlaybackException error) {
      String errorMessage =
          String.format(
              "ExoPlayer playback error %s, code: %s, cause: %s",
              error.getMessage(),
              error.getErrorCodeName(),
              error.getCause() != null ? error.getCause().getMessage() : "none");
      reportError(errorMessage);
    }
  }

  private final class DroppedFramesListener implements AnalyticsListener {
    @Override
    public void onDroppedVideoFrames(
        @NonNull EventTime eventTime, int droppedFrames, long elapsedMs) {
      if (!mIsReleased) {
        ExoPlayerBridgeJni.get().onDroppedVideoFrames(mNativeExoPlayerBridge, droppedFrames);
      }
    }
  }

  /**
   * Initializes a new ExoPlayerBridge.
   *
   * @param nativeExoPlayerBridge The pointer to the native Starboard ExoPlayerBridge.
   * @param context The application context.
   * @param renderersFactory The factory for creating media renderers.
   * @param audioFormat The audio Format, or null if audio-only playback.
   * @param videoFormat The video Format, or null if video-only playback.
   * @param surface The rendering surface for video.
   * @param enableTunnelMode Whether to enable low-latency tunneling mode.
   */
  public ExoPlayerBridge(
      long nativeExoPlayerBridge,
      Context context,
      DefaultRenderersFactory renderersFactory,
      @Nullable Format audioFormat,
      @Nullable Format videoFormat,
      @Nullable Surface surface,
      boolean enableTunnelMode) {
    mExoPlayerHandler = new Handler(Looper.getMainLooper());
    mNativeExoPlayerBridge = nativeExoPlayerBridge;

    if (enableTunnelMode) {
      Log.i(TAG, "Tunnel mode is enabled for this playback.");
    }

    DefaultTrackSelector trackSelector = new DefaultTrackSelector(context);
    trackSelector.setParameters(
        new DefaultTrackSelector.ParametersBuilder(context)
            .setTunnelingEnabled(enableTunnelMode)
            .build());

    if (audioFormat != null) {
      mAudioMediaSource = new ExoPlayerMediaSource(audioFormat, this);
    }
    if (videoFormat != null) {
      mVideoMediaSource = new ExoPlayerMediaSource(videoFormat, this);
    }

    MediaSource playbackMediaSource;
    if (mAudioMediaSource != null && mVideoMediaSource != null) {
      playbackMediaSource = new MergingMediaSource(true, mAudioMediaSource, mVideoMediaSource);
    } else {
      playbackMediaSource = mAudioMediaSource != null ? mAudioMediaSource : mVideoMediaSource;
    }

    ExoPlayer.Builder builder =
        new ExoPlayer.Builder(context)
            .setRenderersFactory(renderersFactory)
            .setLoadControl(
                new DefaultLoadControl.Builder()
                    .setBufferDurationsMs(
                        MIN_BUFFER_DURATION_MS,
                        MAX_BUFFER_DURATION_MS,
                        MIN_BUFFER_DURATION_MS,
                        MIN_BUFFER_DURATION_MS)
                    .build())
            .setLooper(mExoPlayerHandler.getLooper())
            .setTrackSelector(trackSelector)
            .setReleaseTimeoutMs(PLAYER_RELEASE_TIMEOUT_MS);

    mPlayer = builder.build();

    mPlayerListener = new ExoPlayerListener();
    mDroppedFramesListener = new DroppedFramesListener();
    mExoPlayerHandler.post(
        () -> {
          mPlayer.addListener(mPlayerListener);
          mPlayer.addAnalyticsListener(mDroppedFramesListener);
          mPlayer.setMediaSource(playbackMediaSource);
          mPlayer.setVideoSurface(surface);
          mPlayer.prepare();
          mPlayer.play();
        });
  }

  /**
   * Updates the position anchor by reading the current position from ExoPlayer.
   * This method must be called on the ExoPlayer thread to ensure thread safety.
   * It saves the position and current time for interpolation in getCurrentPositionUsec.
   */
  private synchronized void updatePositionAnchor() {
    if (!mIsReleased && mPlayer != null) {
      mLastPlaybackPosUsec = mPlayer.getCurrentPosition() * 1000;
      mPlaybackPosLastUpdatedMsec = SystemClock.elapsedRealtime();
    }
  }

  /**
   * Releases the ExoPlayer and its resources.
   */
  @CalledByNative
  public void release() {
    mIsReleased = true;
    mIsProgressing = false;
    if (mPlayer == null) {
      Log.w(TAG, "Attempted to destroy ExoPlayer after it has already been released.");
      return;
    }

    mExoPlayerHandler.post(
        () -> {
          mPlayer.stop();
          mPlayer.removeListener(mPlayerListener);
          mPlayer.removeAnalyticsListener(mDroppedFramesListener);
          mPlayer.release();
          mPlayer = null;
          mAudioMediaSource = null;
          mVideoMediaSource = null;
        });
  }

  /**
   * Seeks to the specified position in microseconds.
   * @param seekToTimeUsec The position to seek to, in microseconds.
   */
  @CalledByNative
  private void seek(long seekToTimeUsec) {
    if (mPlayer == null || mIsReleased) {
      reportError("Cannot seek with NULL or released ExoPlayer.");
      return;
    }
    mExoPlayerHandler.post(
        () -> {
          mPlayer.seekTo(seekToTimeUsec / 1000);
          updatePositionAnchor();
        });
    mSeekTimeUsec = seekToTimeUsec;
  }

  @CalledByNative
  private void pause() {
    if (mPlayer == null || mIsReleased) {
      reportError("Cannot pause with NULL or released ExoPlayer");
      return;
    }
    mExoPlayerHandler.post(
        () -> {
          mPlayer.pause();
          updatePositionAnchor();
        });
  }

  @CalledByNative
  private void play() {
    if (mPlayer == null || mIsReleased) {
      reportError("Cannot play with NULL or released ExoPlayer");
      return;
    }
    mExoPlayerHandler.post(
        () -> {
          if (!mPlayer.isPlaying() && mPlaybackRate > 0.0) {
            mPlayer.play();
            updatePositionAnchor();
          }
        });
  }

  @CalledByNative
  private void setPlaybackRate(float playbackRate) {
    if (mPlayer == null || mIsReleased) {
      reportError("Cannot set playback rate with NULL or released ExoPlayer");
      return;
    }

    mPlaybackRate = playbackRate;

    if (playbackRate > 0.0f) {
      mExoPlayerHandler.post(
          () -> {
            mPlayer.setPlaybackParameters(new PlaybackParameters(playbackRate, 1.0f));
          });
    }
  }

  @CalledByNative
  private void setVolume(float volume) {
    if (mPlayer == null || mIsReleased) {
      reportError("Cannot set volume with NULL or released ExoPlayer");
      return;
    }

    mExoPlayerHandler.post(
        () -> {
          mPlayer.setVolume(volume);
        });
  }

  @CalledByNative
  private void stop() {
    if (mPlayer == null || mIsReleased) {
      Log.e(TAG, "Cannot stop with NULL or released ExoPlayer. Assuming stopped successfully.");
      return;
    }
    mExoPlayerHandler.post(
        () -> {
          mPlayer.stop();
          updatePositionAnchor();
        });
  }

  /**
   * Returns the current playback position in microseconds.
   *
   * This method provides a high-resolution estimate of the playback position. Since ExoPlayer
   * only provides millisecond precision via {@link ExoPlayer#getCurrentPosition()}, this method
   * interpolates the position based on the time elapsed since the last anchor update from the
   * player thread, adjusted by the current playback rate.
   */
  @CalledByNative
  private synchronized long getCurrentPositionUsec() {
    if (mIsReleased) {
      return mLastPlaybackPosUsec;
    }
    long currentPositionUsec = mLastPlaybackPosUsec;
    if (mIsProgressing) {
      currentPositionUsec +=
          (long)
              ((SystemClock.elapsedRealtime() - mPlaybackPosLastUpdatedMsec)
                  * mPlaybackRate
                  * 1000);
    }

    return currentPositionUsec;
  }

  private void reportError(String errorMessage) {
    Log.e(TAG, errorMessage);

    if (mIsReleased) {
      return;
    }

    ExoPlayerBridgeJni.get().onError(mNativeExoPlayerBridge, errorMessage);
  }

  public int readSample(int type, java.nio.ByteBuffer outBuffer, long[] outMetadata) {
    if (mIsReleased) {
      return androidx.media3.common.C.RESULT_NOTHING_READ;
    }
    return ExoPlayerBridgeJni.get().readSample(mNativeExoPlayerBridge, type, outBuffer, outMetadata);
  }

  public boolean isReady(int type) {
    if (mIsReleased) {
      return false;
    }
    return ExoPlayerBridgeJni.get().isReady(mNativeExoPlayerBridge, type);
  }

  public int skipData(int type, long positionUs) {
    if (mIsReleased) {
      return 0;
    }
    return ExoPlayerBridgeJni.get().skipData(mNativeExoPlayerBridge, type, positionUs);
  }

  public long getBufferedPositionUs(int type) {
    if (mIsReleased) {
      return androidx.media3.common.C.TIME_END_OF_SOURCE;
    }
    return ExoPlayerBridgeJni.get().getBufferedPositionUs(mNativeExoPlayerBridge, type);
  }

  @NativeMethods
  interface Natives {
    void onInitialized(long nativeExoPlayerBridge);

    void onReady(long nativeExoPlayerBridge);

    void onError(long nativeExoPlayerBridge, String errorMessage);

    void onEnded(long nativeExoPlayerBridge);

    void onDroppedVideoFrames(long nativeExoPlayerBridge, int count);

    void onIsPlayingChanged(long nativeExoPlayerBridge, boolean isPlaying);

    int readSample(long nativeExoPlayerBridge, int type, java.nio.ByteBuffer outBuffer, long[] outMetadata);

    boolean isReady(long nativeExoPlayerBridge, int type);

    int skipData(long nativeExoPlayerBridge, int type, long positionUs);

    long getBufferedPositionUs(long nativeExoPlayerBridge, int type);
  }
}
