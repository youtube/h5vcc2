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
//
// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dev.cobalt.media;

import static dev.cobalt.media.Log.TAG;

import android.media.MediaCodec;
import android.media.MediaCodec.CryptoInfo;
import android.media.MediaCodec.CryptoInfo.Pattern;
import android.media.MediaCodecInfo;
import android.media.MediaCodecInfo.CodecCapabilities;
import android.media.MediaCodecInfo.VideoCapabilities;
import android.media.MediaCrypto;
import android.media.MediaFormat;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;
import androidx.annotation.GuardedBy;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import dev.cobalt.media.MediaCodecFrameRateEstimator.FrameRateEstimator;
import dev.cobalt.util.Log;
import dev.cobalt.util.SynchronizedHolder;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.Locale;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

/** A wrapper of the MediaCodec class. */
@JNINamespace("starboard")
class MediaCodecBridge {
  // TODO: Use MediaFormat constants when part of the public API.
  private static final String KEY_CROP_LEFT = "crop-left";
  private static final String KEY_CROP_RIGHT = "crop-right";
  private static final String KEY_CROP_BOTTOM = "crop-bottom";
  private static final String KEY_CROP_TOP = "crop-top";

  private final Object mNativeBridgeLock = new Object();

  @GuardedBy("mNativeBridgeLock")
  private long mNativeMediaCodecBridge;

  private final SynchronizedHolder<MediaCodec, IllegalStateException> mMediaCodec =
      new SynchronizedHolder<>(() -> new IllegalStateException("MediaCodec was destroyed"));

  // mMainHandler is used to ensure that MediaCodec callbacks and state transitions
  // (like resetting mIsFlushing) happen on the main thread, providing a consistent
  // execution environment and avoiding potential race conditions with the native layer.
  private final Handler mMainHandler = new Handler(Looper.getMainLooper());
  private volatile boolean mIsFlushing = false;
  private final boolean mEnableIgnoreCallbacksDuringFlushing;

  private MediaCodec.Callback mCallback;
  private double mPlaybackRate = 1.0;
  private int mFps = 30;
  private double mOperatingRate = mPlaybackRate * mFps;
  private boolean mSkipVideoFramesOver60Fps = false;
  private final boolean mIsTunnelingPlayback;
  private final boolean mEnableFrameRendererListener;

  private MediaCodec.OnFrameRenderedListener mFrameRendererListener;
  private MediaCodec.OnFirstTunnelFrameReadyListener mFirstTunnelFrameReadyListener;

  private boolean shouldSkipVideoFrame(long presentationTimeUs, boolean isDecodeOnly) {
    final double kMaxAcceptedOperatingRate = 60.0;
    if (isDecodeOnly) {
      return true;
    }
    // |mOperatingRate| won't be 0, but to be cautious we still add a check here.
    if (!mSkipVideoFramesOver60Fps || mOperatingRate <= kMaxAcceptedOperatingRate || mOperatingRate == 0) {
      return false;
    }
    // Deterministically downsample to 60fps by picking one frame per 1/60s interval.
    // Some visual jitter may occur briefly when the playback rate changes.
    double frameIntervalUs = 1_000_000.0 / mOperatingRate;
    if (Math.floor(presentationTimeUs * kMaxAcceptedOperatingRate / 1_000_000.0)
        == Math.floor((presentationTimeUs - frameIntervalUs) * kMaxAcceptedOperatingRate / 1_000_000.0)) {
      return true;
    }
    return false;
  }

  public static final class MimeTypes {
    public static final String VIDEO_H264 = "video/avc";
    public static final String VIDEO_H265 = "video/hevc";
    public static final String VIDEO_VP8 = "video/x-vnd.on2.vp8";
    public static final String VIDEO_VP9 = "video/x-vnd.on2.vp9";
    public static final String VIDEO_AV1 = "video/av01";
  }

  private FrameRateEstimator mFrameRateEstimator = null;
  private final AtomicInteger mActiveOutputBuffers = new AtomicInteger(0);
  private volatile MediaFormatWrapper mActiveFormat = null;

  int getCurrentMediaFormatDimension() {
    if (mActiveFormat == null) {
      return 0;
    }
    return mActiveFormat.width() * mActiveFormat.height();
  }

  int sizeOfActiveOutputBuffers() {
    return mActiveOutputBuffers.get();
  }

   /** Wraps a {@link MediaFormat} object to expose its properties to native code */
   // Copied from Chromium's MediaCodecBridge.java
   // https://source.chromium.org/chromium/chromium/src/+/main:media/base/android/java/src/org/chromium/media/MediaCodecBridge.java;l=294-350;drc=6ac17d9d1b844a695209e865137466925fa1214f
   // Here are changes made.
   // - Exposes formatHasCropValues() to the native layer, which needs to call it.
   // - Removes the methods that Cobalt do not use (e.g. colorStandrd).
   // - Add @Nonnul annotation to mFormat. since it is not null.
   // - Add safety checks for width() and height() to prevent a crash. Cobalt's native code
   //   accesses the format immediately in the onOutputFormatChanged callback, which can be
   //   before the dimension keys are available.
   private static class MediaFormatWrapper {
    @NonNull private final MediaFormat mFormat;

    private MediaFormatWrapper(MediaFormat format) {
      mFormat = format;
    }

    @CalledByNative("MediaFormatWrapper")
    private boolean formatHasCropValues() {
      return mFormat.containsKey(KEY_CROP_RIGHT)
            && mFormat.containsKey(KEY_CROP_LEFT)
            && mFormat.containsKey(KEY_CROP_BOTTOM)
            && mFormat.containsKey(KEY_CROP_TOP);
    }

    @CalledByNative("MediaFormatWrapper")
    private int width() {
      if (formatHasCropValues()) {
        return mFormat.getInteger(KEY_CROP_RIGHT) - mFormat.getInteger(KEY_CROP_LEFT) + 1;
      }
      if (mFormat.containsKey(MediaFormat.KEY_WIDTH)) {
        return mFormat.getInteger(MediaFormat.KEY_WIDTH);
      }
      Log.w(TAG, "KEY_WIDTH not found in MediaFormat.");
      return 0;
    }

    @CalledByNative("MediaFormatWrapper")
    private int height() {
      if (formatHasCropValues()) {
        return mFormat.getInteger(KEY_CROP_BOTTOM) - mFormat.getInteger(KEY_CROP_TOP) + 1;
      }
      if (mFormat.containsKey(MediaFormat.KEY_HEIGHT)) {
        return mFormat.getInteger(MediaFormat.KEY_HEIGHT);
      }
      Log.w(TAG, "KEY_HEIGHT not found in MediaFormat.");
      return 0;
    }

    @CalledByNative("MediaFormatWrapper")
    private int sampleRate() {
      return mFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE);
    }

    @CalledByNative("MediaFormatWrapper")
    private int channelCount() {
      return mFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
    }
  }

  public static class ColorInfo {

    public int colorRange;
    public int colorStandard;
    public int colorTransfer;
    public ByteBuffer hdrStaticInfo;

    @CalledByNative("ColorInfo")
    ColorInfo(
        int colorRange,
        int colorStandard,
        int colorTransfer,
        float primaryRChromaticityX,
        float primaryRChromaticityY,
        float primaryGChromaticityX,
        float primaryGChromaticityY,
        float primaryBChromaticityX,
        float primaryBChromaticityY,
        float whitePointChromaticityX,
        float whitePointChromaticityY,
        float maxMasteringLuminance,
        float minMasteringLuminance,
        int maxCll,
        int maxFall,
        boolean forceBigEndianHdrMetadata) {
      this.colorRange = colorRange;
      this.colorStandard = colorStandard;
      this.colorTransfer = colorTransfer;
      this.hdrStaticInfo = MediaCodecUtil.getHdrStaticInfo(
          primaryRChromaticityX,
          primaryRChromaticityY,
          primaryGChromaticityX,
          primaryGChromaticityY, primaryBChromaticityX,
          primaryBChromaticityY, whitePointChromaticityX,
          whitePointChromaticityY, maxMasteringLuminance,
          minMasteringLuminance, maxCll, maxFall, forceBigEndianHdrMetadata);
    }
  }

  private static class CreateMediaCodecBridgeResult {
    private MediaCodecBridge mMediaCodecBridge;
    // Contains the error message when mMediaCodecBridge is null.
    private String mErrorMessage;

    @CalledByNative("CreateMediaCodecBridgeResult")
    private CreateMediaCodecBridgeResult() {
      mMediaCodecBridge = null;
      mErrorMessage = "";
    }

    @CalledByNative("CreateMediaCodecBridgeResult")
    private MediaCodecBridge mediaCodecBridge() {
      return mMediaCodecBridge;
    }

    @CalledByNative("CreateMediaCodecBridgeResult")
    private String errorMessage() {
      return mErrorMessage;
    }
  }

  public MediaCodecBridge(
      long nativeMediaCodecBridge,
      MediaCodec mediaCodec,
      int tunnelModeAudioSessionId,
      boolean enableFrameRendererListener,
      boolean enableIgnoreCallbacksDuringFlushing) {
    if (mediaCodec == null) {
      throw new IllegalArgumentException();
    }
    mNativeMediaCodecBridge = nativeMediaCodecBridge;
    mMediaCodec.set(mediaCodec);
    mIsTunnelingPlayback = tunnelModeAudioSessionId != TunnelModeAudioSessionId.NONE;
    mEnableFrameRendererListener = enableFrameRendererListener;
    mEnableIgnoreCallbacksDuringFlushing = enableIgnoreCallbacksDuringFlushing;
    mCallback =
        new MediaCodec.Callback() {
          @Override
          public void onError(MediaCodec codec, MediaCodec.CodecException e) {
            synchronized (mNativeBridgeLock) {
              if (mNativeMediaCodecBridge == 0) {
                return;
              }
              // Always report codec errors, even when we are flushing, as they indicate
              // critical hardware/decoder level failures that should not be ignored.
              MediaCodecBridgeJni.get()
                  .onMediaCodecError(
                      mNativeMediaCodecBridge,
                      e.isRecoverable(),
                      e.isTransient(),
                      e.getDiagnosticInfo());
            }
          }

          @Override
          public void onInputBufferAvailable(MediaCodec codec, int index) {
            synchronized (mNativeBridgeLock) {
              if (mNativeMediaCodecBridge == 0 || mIsFlushing) {
                return;
              }
              MediaCodecBridgeJni.get()
                  .onMediaCodecInputBufferAvailable(mNativeMediaCodecBridge, index);
            }
          }

          @Override
          public void onOutputBufferAvailable(
              MediaCodec codec, int index, MediaCodec.BufferInfo info) {
            synchronized (mNativeBridgeLock) {
              if (mNativeMediaCodecBridge == 0 || mIsFlushing) {
                return;
              }
              MediaCodecBridgeJni.get()
                  .onMediaCodecOutputBufferAvailable(
                      mNativeMediaCodecBridge,
                      index,
                      info.flags,
                      info.offset,
                      info.presentationTimeUs,
                      info.size);
              mActiveOutputBuffers.incrementAndGet();
              if (mFrameRateEstimator != null) {
                mFrameRateEstimator.onNewOutput(info.presentationTimeUs);
                updateFrameRate();
              }
            }
          }

          @Override
          public void onOutputFormatChanged(MediaCodec codec, MediaFormat format) {
            synchronized (mNativeBridgeLock) {
              if (mNativeMediaCodecBridge == 0 || mIsFlushing) {
                return;
              }
              mActiveFormat = new MediaFormatWrapper(format);
              MediaCodecBridgeJni.get().onMediaCodecOutputFormatChanged(mNativeMediaCodecBridge);
              if (mFrameRateEstimator != null) {
                mFrameRateEstimator.reset();
              }
            }
          }
        };

    if (mEnableIgnoreCallbacksDuringFlushing) {
      mMediaCodec.get().setCallback(mCallback, mMainHandler);
    } else {
      mMediaCodec.get().setCallback(mCallback);
    }

    if (mEnableFrameRendererListener) {
      mFrameRendererListener =
          new MediaCodec.OnFrameRenderedListener() {
            @Override
            public void onFrameRendered(MediaCodec codec, long presentationTimeUs, long nanoTime) {
              synchronized (mNativeBridgeLock) {
                if (mNativeMediaCodecBridge == 0 || mIsFlushing) {
                  return;
                }
                MediaCodecBridgeJni.get()
                    .onMediaCodecFrameRendered(
                        mNativeMediaCodecBridge, presentationTimeUs, nanoTime);
              }
            }
          };
      if (mEnableIgnoreCallbacksDuringFlushing) {
        mMediaCodec.get().setOnFrameRenderedListener(mFrameRendererListener, mMainHandler);
      } else {
        mMediaCodec.get().setOnFrameRenderedListener(mFrameRendererListener, null);
      }
    }

    if (mIsTunnelingPlayback) {
      setupTunnelingPlayback();
    }
    mFrameRateEstimator = MediaCodecFrameRateEstimator.create(mIsTunnelingPlayback);
  }

  private boolean isDecodeOnlyFlagEnabled() {
    // Right now, we only enable BUFFER_FLAG_DECODE_ONLY for tunneling playback.
    if (!mIsTunnelingPlayback) {
      return false;
    }
    // BUFFER_FLAG_DECODE_ONLY is added in Android 14.
    return Build.VERSION.SDK_INT >= 34;
  }

  @CalledByNative
  public static void createVideoMediaCodecBridge(
      long nativeMediaCodecBridge,
      String mime,
      String decoderName,
      // `widthHint` and `heightHint` are used to create the Android video format, which don't have
      // to be directly related to the resolution of the video.
      int widthHint,
      int heightHint,
      int fps,
      int maxWidth,
      int maxHeight,
      Surface surface,
      MediaCrypto crypto,
      ColorInfo colorInfo,
      int tunnelModeAudioSessionId,
      int maxVideoInputSize,
      boolean enableFrameRendererListener,
      boolean skipVideoFramesOver60Fps,
      boolean ignoreCodecCallbacksDuringFlushing,
      boolean enableLowLatency,
      CreateMediaCodecBridgeResult outCreateMediaCodecBridgeResult) {
    MediaCodec mediaCodec = null;
    outCreateMediaCodecBridgeResult.mMediaCodecBridge = null;

    if (decoderName.equals("")) {
      String message = "Invalid decoder name.";
      Log.e(TAG, message);
      outCreateMediaCodecBridgeResult.mErrorMessage = message;
      return;
    }

    try {
      Log.i(TAG, "Creating \"%s\" decoder.", decoderName);
      mediaCodec = MediaCodec.createByCodecName(decoderName);
    } catch (Exception e) {
      String message =
          String.format(
              Locale.US,
              "Failed to create MediaCodec: %s, mustSupportSecure: %s," + " DecoderName: %s",
              mime,
              crypto != null,
              decoderName);
      message += ", exception: " + e.toString();
      Log.e(TAG, message);
      outCreateMediaCodecBridgeResult.mErrorMessage = message;
      return;
    }
    if (mediaCodec == null) {
      outCreateMediaCodecBridgeResult.mErrorMessage = "mediaCodec is null";
      return;
    }

    MediaCodecInfo codecInfo = mediaCodec.getCodecInfo();
    if (codecInfo == null) {
      outCreateMediaCodecBridgeResult.mErrorMessage = "codecInfo is null";
      return;
    }
    CodecCapabilities codecCapabilities = codecInfo.getCapabilitiesForType(mime);
    if (codecCapabilities == null) {
      outCreateMediaCodecBridgeResult.mErrorMessage = "codecCapabilities is null";
      return;
    }
    VideoCapabilities videoCapabilities = codecCapabilities.getVideoCapabilities();
    if (videoCapabilities == null) {
      outCreateMediaCodecBridgeResult.mErrorMessage = "videoCapabilities is null";
      return;
    }

    MediaCodecBridge bridge =
        new MediaCodecBridge(
            nativeMediaCodecBridge,
            mediaCodec,
            tunnelModeAudioSessionId,
            enableFrameRendererListener,
            ignoreCodecCallbacksDuringFlushing);
    bridge.mSkipVideoFramesOver60Fps = skipVideoFramesOver60Fps;
    MediaCodecOutputTracker.get().register(bridge);
    MediaFormat mediaFormat =
        createVideoDecoderFormat(mime, widthHint, heightHint, videoCapabilities);

    if (enableLowLatency) {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        Log.i(TAG, "Enabling low-latency playback.");
        mediaFormat.setInteger(MediaFormat.KEY_LOW_LATENCY, 1);
      } else {
        Log.i(
            TAG,
            "Low-latency playback requested but not supported on API level "
                + Build.VERSION.SDK_INT);
      }
    }

    boolean shouldConfigureHdr =
        colorInfo != null && MediaCodecUtil.isHdrCapableVideoDecoder(mime, codecCapabilities);
    if (shouldConfigureHdr) {
      Log.d(TAG, "Setting HDR info.");
      mediaFormat.setInteger(MediaFormat.KEY_COLOR_TRANSFER, colorInfo.colorTransfer);
      mediaFormat.setInteger(MediaFormat.KEY_COLOR_STANDARD, colorInfo.colorStandard);
      // If color range is unspecified, don't set it.
      if (colorInfo.colorRange != 0) {
        mediaFormat.setInteger(MediaFormat.KEY_COLOR_RANGE, colorInfo.colorRange);
      }
      mediaFormat.setByteBuffer(MediaFormat.KEY_HDR_STATIC_INFO, colorInfo.hdrStaticInfo);
    }

    if (tunnelModeAudioSessionId != TunnelModeAudioSessionId.NONE) {
      mediaFormat.setFeatureEnabled(CodecCapabilities.FEATURE_TunneledPlayback, true);
      mediaFormat.setInteger(MediaFormat.KEY_AUDIO_SESSION_ID, tunnelModeAudioSessionId);
      Log.d(TAG, "Enabled tunnel mode playback on audio session " + tunnelModeAudioSessionId);

      // TODO (b/495868363): KEY_PRIORITY might be also needed for non tunnel playback.
      // Set KEY_PRIORITY to realtime priority.
      mediaFormat.setInteger(MediaFormat.KEY_PRIORITY, 0 /* realtime priority */);
    }

    if (maxWidth > 0 && maxHeight > 0) {
      Log.i(TAG, "Evaluate maxWidth and maxHeight (%d, %d) passed in", maxWidth, maxHeight);
    } else {
      maxWidth = videoCapabilities.getSupportedWidths().getUpper();
      maxHeight = videoCapabilities.getSupportedHeights().getUpper();
      Log.i(
          TAG,
          "maxWidth and maxHeight not passed in, using result of getSupportedWidths()/Heights()"
              + " (%d, %d)",
          maxWidth,
          maxHeight);
    }

    if (fps > 0) {
      if (videoCapabilities.areSizeAndRateSupported(maxWidth, maxHeight, fps)) {
        Log.i(
            TAG,
            "Set maxWidth and maxHeight to (%d, %d)@%d per `areSizeAndRateSupported()`",
            maxWidth,
            maxHeight,
            fps);
      } else {
        Log.w(
            TAG,
            "maxWidth and maxHeight (%d, %d)@%d not supported per `areSizeAndRateSupported()`,"
                + " continue searching",
            maxWidth,
            maxHeight,
            fps);
        if (maxHeight >= 4320 && videoCapabilities.areSizeAndRateSupported(7680, 4320, fps)) {
          maxWidth = 7680;
          maxHeight = 4320;
        } else if (maxHeight >= 2160
            && videoCapabilities.areSizeAndRateSupported(3840, 2160, fps)) {
          maxWidth = 3840;
          maxHeight = 2160;
        } else if (maxHeight >= 1080
            && videoCapabilities.areSizeAndRateSupported(1920, 1080, fps)) {
          maxWidth = 1920;
          maxHeight = 1080;
        } else {
          Log.e(TAG, "Failed to find a compatible resolution");
          maxWidth = 1920;
          maxHeight = 1080;
        }
        Log.i(
            TAG,
            "Set maxWidth and maxHeight to (%d, %d)@%d per `areSizeAndRateSupported()`",
            maxWidth,
            maxHeight,
            fps);
      }
    } else {
      if (maxHeight >= 480 && videoCapabilities.isSizeSupported(maxWidth, maxHeight)) {
        // Technically we can do this check for all resolutions, but only check for resolution with
        // height more than 480p to minimize production impact.  To use a lower resolution is more
        // to reduce memory footprint, and optimize for lower resolution isn't as helpful anyway.
        Log.i(
            TAG,
            "Set maxWidth and maxHeight to (%d, %d) per `isSizeSupported()`",
            maxWidth,
            maxHeight);
      } else {
        if (maxHeight >= 2160 && videoCapabilities.isSizeSupported(3840, 2160)) {
          maxWidth = 3840;
          maxHeight = 2160;
        } else if (maxHeight >= 1080 && videoCapabilities.isSizeSupported(1920, 1080)) {
          maxWidth = 1920;
          maxHeight = 1080;
        } else {
          Log.e(TAG, "Failed to find a compatible resolution");
          maxWidth = 1920;
          maxHeight = 1080;
        }
        Log.i(
            TAG,
            "Set maxWidth and maxHeight to (%d, %d) per `isSizeSupported()`",
            maxWidth,
            maxHeight);
      }
    }

    if (maxVideoInputSize > 0) {
      mediaFormat.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, maxVideoInputSize);
      try {
        Log.i(
            TAG,
            "Overwrite KEY_MAX_INPUT_SIZE to "
                + maxVideoInputSize
                + " (actual: "
                + mediaFormat.getInteger(android.media.MediaFormat.KEY_MAX_INPUT_SIZE)
                + ").");
      } catch (Exception e) {
        Log.e(TAG, "MediaFormat.getInteger(KEY_MAX_INPUT_SIZE) failed with exception: ", e);
      }
    }
    if (!bridge.configureVideo(
        mediaFormat, surface, crypto, 0, maxWidth, maxHeight, outCreateMediaCodecBridgeResult)) {
      Log.e(TAG, "Failed to configure video codec.");
      bridge.release();
      // outCreateMediaCodecBridgeResult.mErrorMessage is set inside configureVideo() on error.
      return;
    }
    if (!bridge.start(outCreateMediaCodecBridgeResult)) {
      Log.e(TAG, "Failed to start video codec.");
      bridge.release();
      // outCreateMediaCodecBridgeResult.mErrorMessage is set inside start() on error.
      return;
    }

    outCreateMediaCodecBridgeResult.mMediaCodecBridge = bridge;
  }

  public boolean start() {
    return start(null);
  }

  @CalledByNative
  private boolean restart() {
    // Restart MediaCodec after flush().
    return start(null);
  }

  @SuppressWarnings("unused")
  public boolean start(@Nullable CreateMediaCodecBridgeResult outCreateMediaCodecBridgeResult) {
    try {
      mMediaCodec.get().start();
    } catch (IllegalStateException | IllegalArgumentException e) {
      Log.e(TAG, "Failed to start the media codec", e);
      if (outCreateMediaCodecBridgeResult != null) {
        outCreateMediaCodecBridgeResult.mErrorMessage =
            "Failed to start media codec " + e.toString();
      }
      return false;
    }
    return true;
  }

  @CalledByNative
  private void setPlaybackRate(double playbackRate) {
    if (mPlaybackRate == playbackRate) {
      return;
    }
    mPlaybackRate = playbackRate;
    if (mFrameRateEstimator != null) {
      updateOperatingRate();
    }
  }

  private void updateFrameRate() {
    if (mFrameRateEstimator == null) {
      return;
    }
    int fps = mFrameRateEstimator.getEstimatedFrameRate();
    if (fps != FrameRateEstimator.INVALID_FRAME_RATE && mFps != fps) {
      mFps = fps;
      updateOperatingRate();
    }
  }

  private void updateOperatingRate() {
    // We needn't set operation rate if playback rate is 0 or less.
    if (Double.compare(mPlaybackRate, 0.0) <= 0) {
      return;
    }
    if (mFps == FrameRateEstimator.INVALID_FRAME_RATE) {
      return;
    }
    if (mFps <= 0) {
      Log.e(TAG, "Failed to set operating rate with invalid fps " + mFps);
      return;
    }
    double operatingRate = mPlaybackRate * mFps;
    if (mOperatingRate == operatingRate) {
      return;
    }

    mOperatingRate = operatingRate;
    Bundle b = new Bundle();
    b.putFloat(MediaFormat.KEY_OPERATING_RATE, (float) operatingRate);
    try {
      mMediaCodec.get().setParameters(b);
    } catch (IllegalStateException e) {
      Log.e(TAG, "Failed to set MediaCodec operating rate: ", e);
    }
    Log.i(TAG, "Set operating rate to " + operatingRate);
  }

  @CalledByNative
  private int flush() {
    // When a flush is initiated on the player thread, there could still be pending
    // callbacks (e.g. onOutputBufferAvailable) already posted to the main thread's
    // looper queue from before the flush.
    // To prevent processing these stale callbacks after the flush starts, we set
    // mIsFlushing to true here to discard them. Then we post a runnable to the main
    // looper queue which will reset mIsFlushing to false once all prior pending
    // callbacks have been sequentialized and discarded.
    if (mEnableIgnoreCallbacksDuringFlushing) {
      synchronized (mNativeBridgeLock) {
        mIsFlushing = true;
      }
    }
    try {
      mMediaCodec.get().flush();
    } catch (Exception e) {
      Log.e(TAG, "Failed to flush MediaCodec", e);
      return MediaCodecStatus.ERROR;
    } finally {
      mActiveOutputBuffers.set(0);
      if (mFrameRateEstimator != null) {
        mFrameRateEstimator.reset();
      }
      if (mEnableIgnoreCallbacksDuringFlushing) {
        mMainHandler.post(() -> {
          synchronized (mNativeBridgeLock) {
            mIsFlushing = false;
          }
        });
      }
    }
    return MediaCodecStatus.OK;
  }

  @CalledByNative
  public void release() {
    MediaCodecOutputTracker.get().unregister(this);
    synchronized (mNativeBridgeLock) {
      mNativeMediaCodecBridge = 0;
    }

    // We skip calling stop() on Android 11, as this version has a race condition
    // if an error occurs during stop(). See b/369372033 for details.
    if (android.os.Build.VERSION.SDK_INT == android.os.Build.VERSION_CODES.R) {
      Log.w(TAG, "Skipping stop() during destruction to avoid Android 11 framework bug");
    } else {
      try {
        mMediaCodec.get().stop();
      } catch (Exception e) {
        Log.w(TAG, "Failed to stop MediaCodec. Proceeding with release", e);
      }
    }

    try {
      String codecName = mMediaCodec.get().getName();
      Log.w(TAG, "Calling MediaCodec.release() on " + codecName);
      mMediaCodec.get().release();
    } catch (Exception e) {
      // The MediaCodec is stuck in a wrong state, possibly due to losing
      // the surface.
      Log.w(TAG, "Failed to release MediaCodec", e);
    }
    mMediaCodec.set(null);
  }

  @CalledByNative
  private MediaFormatWrapper getOutputFormat() {
    MediaFormat format = null;
    try {
      format = mMediaCodec.get().getOutputFormat();
    // Catches `RuntimeException` to handle any undocumented exceptions.
    // See http://b/445694177#comment4 for details.
    } catch (RuntimeException e) {
      Log.e(TAG, "Failed to get output format", e);
      return null;
    }
    // https://developer.android.com/reference/android/media/MediaCodec#getOutputFormat()
    // getOutputFormat should return non-null MediaForamt.
    // If the format is null, we crash the app for dev builds to enforce the API contract.
    // On release builds, we log the error and return null.
    if (format == null) {
      assert(false);
      Log.e(TAG, "MediaCodec.getOutputFormat() returned null");
      return null;
    }
    return new MediaFormatWrapper(format);
  }

  /** Returns null if MediaCodec throws IllegalStateException. */
  @CalledByNative
  private ByteBuffer getInputBuffer(int index) {
    try {
      return mMediaCodec.get().getInputBuffer(index);
    } catch (IllegalStateException e) {
      Log.e(TAG, "Failed to get input buffer", e);
      return null;
    }
  }

  /** Returns null if MediaCodec throws IllegalStateException. */
  @CalledByNative
  private ByteBuffer getOutputBuffer(int index) {
    try {
      return mMediaCodec.get().getOutputBuffer(index);
    } catch (IllegalStateException e) {
      Log.e(TAG, "Failed to get output buffer", e);
      return null;
    }
  }

  @CalledByNative
  private int queueInputBuffer(
      int index, int offset, int size, long presentationTimeUs, int flags, boolean isDecodeOnly) {
    try {
      if (isDecodeOnlyFlagEnabled()
          && (flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) == 0
          && shouldSkipVideoFrame(presentationTimeUs, isDecodeOnly)) {
        flags |= MediaCodec.BUFFER_FLAG_DECODE_ONLY;
      }
      mMediaCodec.get().queueInputBuffer(index, offset, size, presentationTimeUs, flags);
    } catch (Exception e) {
      Log.e(TAG, "Failed to queue input buffer", e);
      return MediaCodecStatus.ERROR;
    }

    // Avoid update frame rate for inputs when tunnel mode is not enabled for better performance.
    if (mIsTunnelingPlayback && mFrameRateEstimator != null) {
      mFrameRateEstimator.onNewInput(presentationTimeUs);
      updateFrameRate();
    }
    return MediaCodecStatus.OK;
  }

  @CalledByNative
  private int queueSecureInputBuffer(
      int index,
      int offset,
      byte[] iv,
      byte[] keyId,
      int[] numBytesOfClearData,
      int[] numBytesOfEncryptedData,
      int numSubSamples,
      int cipherMode,
      int blocksToEncrypt,
      int blocksToSkip,
      long presentationTimeUs,
      boolean isDecodeOnly) {
    try {
      CryptoInfo cryptoInfo = new CryptoInfo();
      cryptoInfo.set(
          numSubSamples, numBytesOfClearData, numBytesOfEncryptedData, keyId, iv, cipherMode);

      if (cipherMode == MediaCodec.CRYPTO_MODE_AES_CBC) {
        cryptoInfo.setPattern(new Pattern(blocksToEncrypt, blocksToSkip));
      } else if (blocksToEncrypt != 0 || blocksToSkip != 0) {
        Log.e(TAG, "Pattern encryption only supported for 'cbcs' scheme (CBC mode).");
        return MediaCodecStatus.ERROR;
      }

      int flags = 0;
      if (isDecodeOnlyFlagEnabled() && shouldSkipVideoFrame(presentationTimeUs, isDecodeOnly)) {
        flags |= MediaCodec.BUFFER_FLAG_DECODE_ONLY;
      }

      mMediaCodec
          .get()
          .queueSecureInputBuffer(index, offset, cryptoInfo, presentationTimeUs, flags);
    } catch (MediaCodec.CryptoException e) {
      int errorCode = e.getErrorCode();
      if (errorCode == MediaCodec.CryptoException.ERROR_NO_KEY) {
        Log.d(TAG, "Failed to queue secure input buffer: CryptoException.ERROR_NO_KEY");
        return MediaCodecStatus.NO_KEY;
      } else if (errorCode == MediaCodec.CryptoException.ERROR_INSUFFICIENT_OUTPUT_PROTECTION) {
        Log.d(
            TAG,
            "Failed to queue secure input buffer: "
                + "CryptoException.ERROR_INSUFFICIENT_OUTPUT_PROTECTION");
        return MediaCodecStatus.INSUFFICIENT_OUTPUT_PROTECTION;
      }
      Log.e(
          TAG,
          "Failed to queue secure input buffer, CryptoException with error code "
              + e.getErrorCode());
      return MediaCodecStatus.ERROR;
    } catch (IllegalArgumentException e) {
      Log.e(TAG, "Failed to queue secure input buffer, IllegalArgumentException " + e);
      return MediaCodecStatus.ERROR;
    } catch (IllegalStateException e) {
      Log.e(TAG, "Failed to queue secure input buffer, IllegalStateException " + e);
      return MediaCodecStatus.ERROR;
    }

    // Avoid update frame rate for inputs when tunnel mode is not enabled for better performance.
    if (mIsTunnelingPlayback && mFrameRateEstimator != null) {
      mFrameRateEstimator.onNewInput(presentationTimeUs);
      updateFrameRate();
    }
    return MediaCodecStatus.OK;
  }

  @CalledByNative
  private void releaseOutputBuffer(int index, boolean render) {
    try {
      mMediaCodec.get().releaseOutputBuffer(index, render);
      mActiveOutputBuffers.decrementAndGet();
    } catch (IllegalStateException e) {
      // TODO: May need to report the error to the caller. crbug.com/356498.
      Log.e(TAG, "Failed to release output buffer", e);
    }
  }

  @CalledByNative
  private void releaseOutputBufferAtTimestamp(int index, long renderTimestampNs) {
    try {
      mMediaCodec.get().releaseOutputBuffer(index, renderTimestampNs);
      mActiveOutputBuffers.decrementAndGet();
    } catch (IllegalStateException e) {
      // TODO: May need to report the error to the caller. crbug.com/356498.
      Log.e(TAG, "Failed to release output buffer", e);
    }
  }

  private boolean configureVideo(
      MediaFormat format,
      Surface surface,
      MediaCrypto crypto,
      int flags,
      int maxSupportedWidth,
      int maxSupportedHeight,
      CreateMediaCodecBridgeResult outCreateMediaCodecBridgeResult) {
    try {
      // Since we haven't passed the properties of the stream we're playing down to this level, from
      // our perspective, we could potentially adapt up to 8k at any point. We thus request 8k
      // buffers up front, unless the decoder claims to not be able to do 8k, in which case we're
      // ok, since we would've rejected a 8k stream when canPlayType was called, and then use those
      // decoder values instead. We only support 8k for API level 29 and above.
      if (Build.VERSION.SDK_INT > 28) {
        format.setInteger(MediaFormat.KEY_MAX_WIDTH, Math.min(7680, maxSupportedWidth));
        format.setInteger(MediaFormat.KEY_MAX_HEIGHT, Math.min(4320, maxSupportedHeight));
      } else {
        // Android 5.0/5.1 seems not support 8K. Fallback to 4K until we get a
        // better way to get maximum supported resolution.
        format.setInteger(MediaFormat.KEY_MAX_WIDTH, Math.min(3840, maxSupportedWidth));
        format.setInteger(MediaFormat.KEY_MAX_HEIGHT, Math.min(2160, maxSupportedHeight));
      }

      maybeSetMaxVideoInputSize(format);
      mMediaCodec.get().configure(format, surface, crypto, flags);
      return true;
    } catch (IllegalArgumentException e) {
      Log.e(TAG, "Cannot configure the video codec with IllegalArgumentException: ", e);
      outCreateMediaCodecBridgeResult.mErrorMessage =
          "Cannot configure the video codec with IllegalArgumentException: " + e.toString();
    } catch (IllegalStateException e) {
      Log.e(TAG, "Cannot configure the video codec with IllegalStateException: ", e);
      outCreateMediaCodecBridgeResult.mErrorMessage =
          "Cannot configure the video codec with IllegalStateException: " + e.toString();
    } catch (MediaCodec.CryptoException e) {
      Log.e(TAG, "Cannot configure the video codec with CryptoException: ", e);
      outCreateMediaCodecBridgeResult.mErrorMessage =
          "Cannot configure the video codec with CryptoException: " + e.toString();
    } catch (Exception e) {
      Log.e(TAG, "Cannot configure the video codec with Exception: ", e);
      outCreateMediaCodecBridgeResult.mErrorMessage =
          "Cannot configure the video codec with Exception: " + e.toString();
    }
    return false;
  }

  private static MediaFormat createVideoDecoderFormat(
      String mime, int widthHint, int heightHint, VideoCapabilities videoCapabilities) {
    return MediaFormat.createVideoFormat(
        mime,
        alignDimension(widthHint, videoCapabilities.getWidthAlignment()),
        alignDimension(heightHint, videoCapabilities.getHeightAlignment()));
  }

  private static int alignDimension(int size, int alignment) {
    int ceilDivide = (size + alignment - 1) / alignment;
    return ceilDivide * alignment;
  }

  // Use some heuristics to set KEY_MAX_INPUT_SIZE (the size of the input buffers).
  // Taken from ExoPlayer:
  // https://github.com/google/ExoPlayer/blob/8595c65678a181296cdf673eacb93d8135479340/library/src/main/java/com/google/android/exoplayer/MediaCodecVideoTrackRenderer.java
  private void maybeSetMaxVideoInputSize(MediaFormat format) {
    if (format.containsKey(android.media.MediaFormat.KEY_MAX_INPUT_SIZE)) {
      try {
        Log.i(
            TAG,
            "Use default value for KEY_MAX_INPUT_SIZE: "
                + format.getInteger(android.media.MediaFormat.KEY_MAX_INPUT_SIZE)
                + '.');
      } catch (Exception e) {
        Log.e(TAG, "MediaFormat.getInteger(KEY_MAX_INPUT_SIZE) failed with exception: ", e);
      }
      // Already set. The source of the format may know better, so do nothing.
      return;
    }
    int maxHeight = format.getInteger(MediaFormat.KEY_HEIGHT);
    if (format.containsKey(MediaFormat.KEY_MAX_HEIGHT)) {
      maxHeight = Math.max(maxHeight, format.getInteger(MediaFormat.KEY_MAX_HEIGHT));
    }
    int maxWidth = format.getInteger(MediaFormat.KEY_WIDTH);
    if (format.containsKey(MediaFormat.KEY_MAX_WIDTH)) {
      maxWidth = Math.max(maxWidth, format.getInteger(MediaFormat.KEY_MAX_WIDTH));
    }
    int maxPixels;
    int minCompressionRatio;
    switch (format.getString(MediaFormat.KEY_MIME)) {
      case MimeTypes.VIDEO_H264:
        if ("BRAVIA 4K 2015".equals(Build.MODEL)) {
          // The Sony BRAVIA 4k TV has input buffers that are too small for the calculated
          // 4k video maximum input size, so use the default value.
          return;
        }
        // Round up width/height to an integer number of macroblocks.
        maxPixels = ((maxWidth + 15) / 16) * ((maxHeight + 15) / 16) * 16 * 16;
        minCompressionRatio = 2;
        break;
      case MimeTypes.VIDEO_VP8:
        // VPX does not specify a ratio so use the values from the platform's SoftVPX.cpp.
        maxPixels = maxWidth * maxHeight;
        minCompressionRatio = 2;
        break;
      case MimeTypes.VIDEO_H265:
      case MimeTypes.VIDEO_VP9:
      case MimeTypes.VIDEO_AV1:
        maxPixels = maxWidth * maxHeight;
        minCompressionRatio = 4;
        break;
      default:
        // Leave the default max input size.
        return;
    }
    // Estimate the maximum input size assuming three channel 4:2:0 subsampled input frames.
    int maxVideoInputSize = (maxPixels * 3) / (2 * minCompressionRatio);
    format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, maxVideoInputSize);
    try {
      Log.i(
          TAG,
          "Set KEY_MAX_INPUT_SIZE to "
              + maxVideoInputSize
              + " (actual: "
              + format.getInteger(android.media.MediaFormat.KEY_MAX_INPUT_SIZE)
              + ").");
    } catch (Exception e) {
      Log.e(TAG, "MediaFormat.getInteger(KEY_MAX_INPUT_SIZE) failed with exception: ", e);
    }
  }

  private void setupTunnelingPlayback() {
    // PARAMETER_KEY_TUNNEL_PEEK is added in Android 12.
    if (Build.VERSION.SDK_INT >= 31) {
      // |PARAMETER_KEY_TUNNEL_PEEK| should be default to enabled according to the API
      // documentation, but some devices don't adhere to the documentation and we need to set the
      // parameter explicitly.
      Bundle bundle = new Bundle();
      bundle.putInt(MediaCodec.PARAMETER_KEY_TUNNEL_PEEK, 1);
      try {
        mMediaCodec.get().setParameters(bundle);
      } catch (IllegalStateException e) {
        Log.e(TAG, "Failed to set MediaCodec PARAMETER_KEY_TUNNEL_PEEK: ", e);
      }
    } else {
      Log.w(
          TAG,
          "MediaCodec PARAMETER_KEY_TUNNEL_PEEK is not supported in SDK version < 31: SDK version="
              + Build.VERSION.SDK_INT);
    }

    // OnFirstTunnelFrameReadyListener is added in Android 12.
    if (Build.VERSION.SDK_INT >= 31) {
      mFirstTunnelFrameReadyListener =
          new MediaCodec.OnFirstTunnelFrameReadyListener() {
            @Override
            public void onFirstTunnelFrameReady(MediaCodec codec) {
              synchronized (mNativeBridgeLock) {
                if (mNativeMediaCodecBridge == 0 || mIsFlushing) {
                  return;
                }
                MediaCodecBridgeJni.get()
                    .onMediaCodecFirstTunnelFrameReady(mNativeMediaCodecBridge);
              }
            }
          };
      if (mEnableIgnoreCallbacksDuringFlushing) {
        mMediaCodec.get().setOnFirstTunnelFrameReadyListener(mMainHandler, mFirstTunnelFrameReadyListener);
      } else {
        mMediaCodec.get().setOnFirstTunnelFrameReadyListener(null, mFirstTunnelFrameReadyListener);
      }
    } else {
      Log.w(
          TAG,
          "MediaCodec OnFirstTunnelFrameReadyListener is not supported in SDK version < 31: SDK"
              + " version="
              + Build.VERSION.SDK_INT);
    }
  }

  public boolean configureAudio(MediaFormat format, MediaCrypto crypto, int flags) {
    try {
      mMediaCodec.get().configure(format, null, crypto, flags);
      return true;
    } catch (IllegalArgumentException | IllegalStateException e) {
      Log.e(TAG, "Cannot configure the audio codec", e);
    } catch (MediaCodec.CryptoException e) {
      Log.e(TAG, "Cannot configure the audio codec: DRM error", e);
    } catch (Exception e) {
      Log.e(TAG, "Cannot configure the audio codec", e);
    }
    return false;
  }

  @NativeMethods
  interface Natives {
    void onMediaCodecError(
        long nativeMediaCodecBridge,
        boolean isRecoverable,
        boolean isTransient,
        String diagnosticInfo);

    void onMediaCodecInputBufferAvailable(long nativeMediaCodecBridge, int bufferIndex);

    void onMediaCodecOutputBufferAvailable(
        long nativeMediaCodecBridge,
        int bufferIndex,
        int flags,
        int offset,
        long presentationTimeUs,
        int size);

    void onMediaCodecOutputFormatChanged(long nativeMediaCodecBridge);

    void onMediaCodecFrameRendered(
        long nativeMediaCodecBridge, long presentationTimeUs, long renderAtSystemTimeNs);

    void onMediaCodecFirstTunnelFrameReady(long nativeMediaCodecBridge);
  }
}
