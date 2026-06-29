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

package dev.cobalt.media;

import static dev.cobalt.media.Log.TAG;

import android.content.Context;
import android.view.Surface;
import androidx.media3.common.ColorInfo;
import androidx.media3.common.Format;
import androidx.media3.common.MimeTypes;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.mediacodec.MediaCodecInfo;
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector;
import androidx.media3.exoplayer.mediacodec.MediaCodecUtil;
import dev.cobalt.util.IsEmulator;
import dev.cobalt.util.Log;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;

/**
 * Entry point for creating ExoPlayer components from the native layer.
 *
 * This manager provides factory methods for creating the {@link ExoPlayerBridge} and various
 * {@link MediaSource} instances tailored for Starboard's playback requirements.
 */
@JNINamespace("starboard")
public class ExoPlayerManager {
  private final Context mContext;
  private final DefaultRenderersFactory mRenderersFactory;
  private static final int PASSTHROUGH_CODEC_BITRATE = 384000;

  /** Helper to check if a mime type represents a passthrough audio codec. */
  private static boolean isPassthroughCodec(String mime) {
    return MimeTypes.AUDIO_AC3.equals(mime)
        || MimeTypes.AUDIO_E_AC3.equals(mime)
        || MimeTypes.AUDIO_E_AC3_JOC.equals(mime);
  }

  private static final class FilteringMediaCodecSelector implements MediaCodecSelector {
    /**
     * Returns a list of decoders for the given mime type.
     * This implementation filters out software decoders for video (unless on emulator)
     * and ignores on-device decoders for Dolby codecs to force passthrough.
     */
    @Override
    public List<MediaCodecInfo> getDecoderInfos(
        String mimeType, boolean requiresSecureDecoder, boolean requiresTunnelingDecoder) {
      // Ignore on-device AC3/EAC3 decoders as we only support passthrough.
      if (isPassthroughCodec(mimeType)) {
        return Collections.emptyList();
      }
      List<MediaCodecInfo> defaultDecoderInfos = null;
      try {
        defaultDecoderInfos =
            MediaCodecUtil.getDecoderInfos(
                mimeType, requiresSecureDecoder, requiresTunnelingDecoder);
      } catch (MediaCodecUtil.DecoderQueryException e) {
        Log.i(TAG, String.format("MediaCodecUtil.getDecoderInfos() error %s", e));
        return defaultDecoderInfos;
      }
      // Skip video decoder filtering for emulators.
      if (IsEmulator.isEmulator()) {
        Log.i(TAG, "Allowing all available decoders for emulator.");
        return defaultDecoderInfos;
      }

      List<MediaCodecInfo> hardwareVideoDecoderInfos = new ArrayList<>();
      if (mimeType.startsWith("video/")) {
        for (MediaCodecInfo decoderInfo : defaultDecoderInfos) {
          if (decoderInfo.hardwareAccelerated) {
            hardwareVideoDecoderInfos.add(decoderInfo);
          }
        }

        if (hardwareVideoDecoderInfos.isEmpty()) {
          Log.w(
              TAG,
              "Could not find a hardware accelerated video decoder, falling back to software"
                  + " decoders.");
          return defaultDecoderInfos;
        }
        return hardwareVideoDecoderInfos;
      }
      return defaultDecoderInfos;
    }
  }

  /**
   * Initializes a new ExoPlayerManager.
   *
   * @param context The application context.
   */
  public ExoPlayerManager(Context context) {
    mContext = context;
    mRenderersFactory =
        new DefaultRenderersFactory(mContext)
            .setMediaCodecSelector(new FilteringMediaCodecSelector());
  }

  /**
   * Creates and initializes a new ExoPlayerBridge.
   *
   * @param nativeExoPlayerBridge The pointer to the native ExoPlayerBridge.
   * @param audioFormat The audio Format.
   * @param videoFormat The video Format.
   * @param surface The rendering surface.
   * @param enableTunnelMode Whether to enable tunnel mode.
   * @return A new ExoPlayerBridge instance.
   */
  @CalledByNative
  public synchronized ExoPlayerBridge createExoPlayerBridge(
      long nativeExoPlayerBridge,
      Format audioFormat,
      Format videoFormat,
      Surface surface,
      boolean enableTunnelMode) {
    if (videoFormat != null && (surface == null || !surface.isValid())) {
      Log.e(TAG, "Cannot initialize ExoPlayer with an invalid surface.");
      return null;
    }

    return new ExoPlayerBridge(
        nativeExoPlayerBridge,
        mContext,
        mRenderersFactory,
        audioFormat,
        videoFormat,
        surface,
        enableTunnelMode);
  }

  /**
   * Creates an ExoPlayer Format for audio.
   * @param mime The mime type of the audio stream.
   * @param audioConfigurationData Configuration data (e.g. CSD).
   * @param sampleRate The sample rate.
   * @param channelCount The number of channels.
   * @return A new ExoPlayer Format instance.
   */
  @CalledByNative
  public static Format createAudioFormat(
      String mime, byte[] audioConfigurationData, int sampleRate, int channelCount) {
    Format.Builder builder =
        new Format.Builder()
            .setSampleRate(sampleRate)
            .setChannelCount(channelCount)
            .setSampleMimeType(mime);

    if (audioConfigurationData != null) {
      if (MimeTypes.AUDIO_OPUS.equals(mime)) {
        byte[][] csds =
            MediaFormatBuilder.starboardParseOpusConfigurationData(
                sampleRate, audioConfigurationData);
        if (csds == null) {
          Log.e(TAG, "Error parsing Opus config info");
          return null;
        }
        builder.setInitializationData(Arrays.asList(csds));
      } else {
        builder.setInitializationData(Collections.singletonList(audioConfigurationData));
      }
    }

    if (isPassthroughCodec(mime)) {
      builder.setAverageBitrate(PASSTHROUGH_CODEC_BITRATE);
    }

    return builder.build();
  }

  /**
   * Creates an ExoPlayer Format for video.
   * @param mime The mime type of the video stream.
   * @param width The width of the video.
   * @param height The height of the video.
   * @param fps The frame rate.
   * @param bitrate The bitrate.
   * @param colorInfo HDR color info, if applicable.
   * @return A new ExoPlayer Format instance.
   */
  @CalledByNative
  public static Format createVideoFormat(
      String mime, int width, int height, int fps, int bitrate, ColorInfo colorInfo) {
    Format.Builder builder = new Format.Builder();
    builder.setSampleMimeType(mime).setWidth(width).setHeight(height);

    if (fps != 0) {
      builder.setFrameRate(fps);
    }

    if (bitrate != 0) {
      builder.setAverageBitrate(bitrate);
    }

    if (colorInfo != null) {
      builder.setColorInfo(colorInfo);
    }

    return builder.build();
  }

  /** Creates a {@link ColorInfo} object including HDR static metadata. */
  @CalledByNative
  public static ColorInfo createExoPlayerColorInfo(
      int colorRange,
      int colorSpace,
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
      int maxFall) {
    ByteBuffer hdrStaticInfo =
        dev.cobalt.media.MediaCodecUtil.getHdrStaticInfo(
            primaryRChromaticityX,
            primaryRChromaticityY,
            primaryGChromaticityX,
            primaryGChromaticityY,
            primaryBChromaticityX,
            primaryBChromaticityY,
            whitePointChromaticityX,
            whitePointChromaticityY,
            maxMasteringLuminance,
            minMasteringLuminance,
            maxCll,
            maxFall,
            false);
    byte[] hdrStaticInfoArray = new byte[hdrStaticInfo.remaining()];
    hdrStaticInfo.get(hdrStaticInfoArray);
    return new ColorInfo.Builder()
        .setColorRange(colorRange)
        .setColorSpace(colorSpace)
        .setColorTransfer(colorTransfer)
        .setHdrStaticInfo(hdrStaticInfoArray)
        .build();
  }
}
