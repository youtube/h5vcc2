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

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Build;
import androidx.annotation.NonNull;
import androidx.annotation.RequiresApi;
import dev.cobalt.util.Log;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CopyOnWriteArrayList;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

/** Creates and destroys AudioTrackBridge and handles the volume change. */
@JNINamespace("starboard")
public class AudioOutputManager {
  @NonNull private final List<AudioTrackBridge> mAudioTrackBridgeList;
  private Context mContext;

  boolean mHasRegisteredAudioDeviceCallback = false;

  public AudioOutputManager(Context context) {
    mContext = context;
    // CopyOnWriteArrayList allows thread-safe UI iteration without locking native threads.
    mAudioTrackBridgeList = new CopyOnWriteArrayList<AudioTrackBridge>();
  }

  @CalledByNative
  AudioTrackBridge createAudioTrackBridge(
      int sampleType,
      int sampleRate,
      int channelCount,
      int maxSamplesPerWrite,
      int preferredBufferSizeInBytes,
      int tunnelModeAudioSessionId,
      boolean isWebAudio) {
    AudioTrackBridge audioTrackBridge =
        new AudioTrackBridge(
            sampleType,
            sampleRate,
            channelCount,
            maxSamplesPerWrite,
            preferredBufferSizeInBytes,
            tunnelModeAudioSessionId,
            isWebAudio);
    if (!audioTrackBridge.isAudioTrackValid()) {
      Log.e(TAG, "AudioTrackBridge has invalid audio track");
      return null;
    }
    mAudioTrackBridgeList.add(audioTrackBridge);

    if (mHasRegisteredAudioDeviceCallback || isWebAudio) {
      return audioTrackBridge;
    }

    AudioManager audioManager = (AudioManager) mContext.getSystemService(Context.AUDIO_SERVICE);
    audioManager.registerAudioDeviceCallback(
        new AudioDeviceCallback() {
          // Since registering a callback triggers an immediate call to onAudioDevicesAdded() with
          // current devices, don't notify tracks for this initial call.
          private boolean mInitialDevicesAdded = false;

          private void handleConnectedDeviceChange(AudioDeviceInfo[] devices, boolean isAdded) {
            AudioDeviceInfo relevantDevice = null;
            for (AudioDeviceInfo info : devices) {
              // TODO: Determine if AudioDeviceInfo.TYPE_HDMI_EARC should be checked in API 31.
              if (info.isSink()
                  && (info.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP
                      || info.getType() == AudioDeviceInfo.TYPE_HDMI_ARC
                      || info.getType() == AudioDeviceInfo.TYPE_HDMI)) {
                relevantDevice = info;
                break;
              }
            }
            if (relevantDevice == null) {
              return;
            }

            String eventName = isAdded ? "added" : "removed";
            // If all active audio tracks are PCM and seamless switching is enabled, Android OS
            // automatically handles dynamic routing (e.g. to Bluetooth A2DP or built-in speakers)
            // without requiring player recreation. While player recreation is avoided, an audio
            // sink flush (RESET_AND_CONTINUE) is still performed to reset timestamps and latency.
            // Only flag player recreation (RESTART_PLAYER) if seamless switching is disabled or
            // there is an active passthrough track that cannot be routed to incompatible sinks.
            if (AudioOutputManagerJni.get().isSeamlessAudioSwitchingEnabled()
                && !hasActivePassthroughTrack(mAudioTrackBridgeList)) {
              Log.i(
                  TAG,
                  "Audio device %s: %s (%s). Notifying active tracks (RESET_AND_CONTINUE) "
                      + "for seamless PCM playback.",
                  eventName,
                  relevantDevice.getProductName(),
                  getDeviceTypeName(relevantDevice.getType()));
              for (AudioTrackBridge bridge : mAudioTrackBridgeList) {
                bridge.onAudioDeviceChanged(AudioDeviceChange.RESET_AND_CONTINUE);
              }
              return;
            }

            Log.i(
                TAG,
                "Audio device %s: %s (%s). Notifying active tracks (RESTART_PLAYER) "
                    + "to renegotiate audio capabilities.",
                eventName,
                relevantDevice.getProductName(),
                getDeviceTypeName(relevantDevice.getType()));
            for (AudioTrackBridge bridge : mAudioTrackBridgeList) {
              bridge.onAudioDeviceChanged(AudioDeviceChange.RESTART_PLAYER);
            }
          }

          @Override
          public void onAudioDevicesAdded(AudioDeviceInfo[] addedDevices) {
            Log.v(
                TAG,
                "onAudioDevicesAdded() called, |mInitialDevicesAdded| is: %b.",
                mInitialDevicesAdded);
            if (mInitialDevicesAdded) {
              handleConnectedDeviceChange(addedDevices, /* isAdded= */ true);
              return;
            }
            mInitialDevicesAdded = true;
          }

          @Override
          public void onAudioDevicesRemoved(AudioDeviceInfo[] removedDevices) {
            Log.v(TAG, "onAudioDevicesRemoved() called.");
            handleConnectedDeviceChange(removedDevices, /* isAdded= */ false);
          }
        },
        null);
    mHasRegisteredAudioDeviceCallback = true;
    return audioTrackBridge;
  }

  @CalledByNative
  void destroyAudioTrackBridge(AudioTrackBridge audioTrackBridge) {
    audioTrackBridge.release();
    mAudioTrackBridgeList.remove(audioTrackBridge);
  }

  /** Stores info from AudioDeviceInfo to be passed to the native app. */
  public static class OutputDeviceInfo {
    public int type;
    public int channels;

    @CalledByNative("OutputDeviceInfo")
    private OutputDeviceInfo() {}

    @CalledByNative("OutputDeviceInfo")
    public int getType() {
      return type;
    }

    @CalledByNative("OutputDeviceInfo")
    public int getChannels() {
      return channels;
    }
  }

  /** Returns output device info. */
  @CalledByNative
  boolean getOutputDeviceInfo(int index, OutputDeviceInfo outDeviceInfo) {
    if (index < 0) {
      return false;
    }

    AudioManager audioManager = (AudioManager) mContext.getSystemService(Context.AUDIO_SERVICE);
    AudioDeviceInfo[] deviceInfos = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);

    if (index >= 0 && index < deviceInfos.length) {
      outDeviceInfo.type = deviceInfos[index].getType();
      outDeviceInfo.channels = 2;

      // The aac audio decoder on this platform will switch its output from 5.1
      // to stereo right before providing the first output buffer when
      // attempting to decode 5.1 input.  Since this heavily violates invariants
      // of the shared starboard player framework, disable 5.1 on this platform.
      // It is expected that we will be able to resolve this issue with Xiaomi
      // by Android P, so only do this workaround for SDK versions < 27.
      if (android.os.Build.MODEL.equals("MIBOX3") && android.os.Build.VERSION.SDK_INT < 27) {
        return true;
      }

      int[] channelCounts = deviceInfos[index].getChannelCounts();
      if (channelCounts.length == 0) {
        // An empty array indicates that the device supports arbitrary channel masks.
        outDeviceInfo.channels = 8;
      } else {
        for (int count : channelCounts) {
          outDeviceInfo.channels = Math.max(outDeviceInfo.channels, count);
        }
      }
      return true;
    }

    return false;
  }

  private static boolean isPassthroughEncoding(int encoding) {
    return encoding == AudioFormat.ENCODING_AC3
        || encoding == AudioFormat.ENCODING_E_AC3
        || encoding == AudioFormat.ENCODING_E_AC3_JOC;
  }

  private static boolean hasActivePassthroughTrack(
      @NonNull List<AudioTrackBridge> audioTrackBridges) {
    for (AudioTrackBridge bridge : audioTrackBridges) {
      if (isPassthroughEncoding(bridge.getSampleType())) {
        return true;
      }
    }
    return false;
  }

  /** Convert AudioDeviceInfo.TYPE_* to name in String */
  private static String getDeviceTypeName(int deviceType) {
    switch (deviceType) {
      case AudioDeviceInfo.TYPE_AUX_LINE:
        return "TYPE_AUX_LINE";
      case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
        return "TYPE_BLUETOOTH_A2DP";
      case AudioDeviceInfo.TYPE_BLUETOOTH_SCO:
        return "TYPE_BLUETOOTH_SCO";
      case AudioDeviceInfo.TYPE_BUILTIN_EARPIECE:
        return "TYPE_BUILTIN_EARPIECE";
      case AudioDeviceInfo.TYPE_BUILTIN_MIC:
        return "TYPE_BUILTIN_MIC";
      case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER:
        return "TYPE_BUILTIN_SPEAKER";
      case AudioDeviceInfo.TYPE_BUS:
        return "TYPE_BUS";
      case AudioDeviceInfo.TYPE_DOCK:
        return "TYPE_DOCK";
      case AudioDeviceInfo.TYPE_FM:
        return "TYPE_FM";
      case AudioDeviceInfo.TYPE_FM_TUNER:
        return "TYPE_FM_TUNER";
      case AudioDeviceInfo.TYPE_HDMI:
        return "TYPE_HDMI";
      case AudioDeviceInfo.TYPE_HDMI_ARC:
        return "TYPE_HDMI_ARC";
      case AudioDeviceInfo.TYPE_IP:
        return "TYPE_IP";
      case AudioDeviceInfo.TYPE_LINE_ANALOG:
        return "TYPE_LINE_ANALOG";
      case AudioDeviceInfo.TYPE_LINE_DIGITAL:
        return "TYPE_LINE_DIGITAL";
      case AudioDeviceInfo.TYPE_TELEPHONY:
        return "TYPE_TELEPHONY";
      case AudioDeviceInfo.TYPE_TV_TUNER:
        return "TYPE_TV_TUNER";
      case AudioDeviceInfo.TYPE_UNKNOWN:
        return "TYPE_UNKNOWN";
      case AudioDeviceInfo.TYPE_USB_ACCESSORY:
        return "TYPE_USB_ACCESSORY";
      case AudioDeviceInfo.TYPE_USB_DEVICE:
        return "TYPE_USB_DEVICE";
      case AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
        return "TYPE_WIRED_HEADPHONES";
      case AudioDeviceInfo.TYPE_WIRED_HEADSET:
        return "TYPE_WIRED_HEADSET";
      default:
        // This may include constants introduced after API 23.
        return String.format(Locale.US, "TYPE_UNKNOWN (%d)", deviceType);
    }
  }

  /** Convert audio encodings in int[] to common separated values in String */
  private static String getEncodingNames(final int[] encodings) {
    StringBuffer encodingsInString = new StringBuffer("[");
    for (int i = 0; i < encodings.length; ++i) {
      switch (encodings[i]) {
        case AudioFormat.ENCODING_DEFAULT:
          encodingsInString.append("DEFAULT");
          break;
        case AudioFormat.ENCODING_PCM_8BIT:
          encodingsInString.append("PCM_8BIT");
          break;
        case AudioFormat.ENCODING_PCM_16BIT:
          encodingsInString.append("PCM_16BIT");
          break;
        case AudioFormat.ENCODING_PCM_FLOAT:
          encodingsInString.append("PCM_FLOAT");
          break;
        case AudioFormat.ENCODING_DTS:
          encodingsInString.append("DTS");
          break;
        case AudioFormat.ENCODING_DTS_HD:
          encodingsInString.append("DTS_HD");
          break;
        case AudioFormat.ENCODING_AC3:
          encodingsInString.append("AC3");
          break;
        case AudioFormat.ENCODING_E_AC3:
          encodingsInString.append("E_AC3");
          break;
        case AudioFormat.ENCODING_IEC61937:
          encodingsInString.append("IEC61937");
          break;
        case AudioFormat.ENCODING_INVALID:
          encodingsInString.append("INVALID");
          break;
        default:
          // This may include constants introduced after API 23.
          encodingsInString.append(String.format(Locale.US, "UNKNOWN (%d)", encodings[i]));
          break;
      }
      if (i != encodings.length - 1) {
        encodingsInString.append(", ");
      }
    }
    encodingsInString.append(']');
    return encodingsInString.toString();
  }

  /** Dump all audio output devices. */
  public void dumpAllOutputDevices() {
    Log.i(TAG, "Dumping all audio output devices:");

    AudioManager audioManager = (AudioManager) mContext.getSystemService(Context.AUDIO_SERVICE);
    AudioDeviceInfo[] deviceInfos = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);

    for (AudioDeviceInfo info : deviceInfos) {
      Log.i(
          TAG,
          "  Audio Device: %s, channels: %s, sample rates: %s, encodings: %s",
          getDeviceTypeName(info.getType()),
          Arrays.toString(info.getChannelCounts()),
          Arrays.toString(info.getSampleRates()),
          getEncodingNames(info.getEncodings()));
    }
  }

  /** Returns the minimum buffer size of AudioTrack. */
  @CalledByNative
  int getMinBufferSize(int sampleType, int sampleRate, int channelCount) {
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
    return AudioTrack.getMinBufferSize(sampleRate, channelConfig, sampleType);
  }

  /** Generate audio session id used by tunneled playback. */
  @CalledByNative
  int generateTunnelModeAudioSessionId(int numberOfChannels) {
    // Android 9.0 (Build.VERSION.SDK_INT >= 28) support v2 sync header that
    // aligns sync header with audio frame size. V1 sync header has alignment
    // issues for multi-channel audio.
    if (Build.VERSION.SDK_INT < 28) {
      // Currently we only support int16 under tunnel mode.
      final int sampleSizeInBytes = 2;
      final int frameSizeInBytes = sampleSizeInBytes * numberOfChannels;
      if (AudioTrackBridge.AV_SYNC_HEADER_V1_SIZE % frameSizeInBytes != 0) {
        Log.w(
            TAG,
            "Disable tunnel mode due to sampleSizeInBytes (%d) * numberOfChannels (%d) isn't"
                + " aligned to AV_SYNC_HEADER_V1_SIZE (%d).",
            sampleSizeInBytes,
            numberOfChannels,
            AudioTrackBridge.AV_SYNC_HEADER_V1_SIZE);
        return TunnelModeAudioSessionId.NONE;
      }
    }
    AudioManager audioManager = (AudioManager) mContext.getSystemService(Context.AUDIO_SERVICE);
    return audioManager.generateAudioSessionId();
  }

  /** Returns whether passthrough on `encoding` is supported. */
  @CalledByNative
  boolean hasPassthroughSupportFor(int encoding) {
    AudioManager audioManager = (AudioManager) mContext.getSystemService(Context.AUDIO_SERVICE);
    AudioDeviceInfo[] deviceInfos = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);

    // Some devices have issues on reporting playback capability and managing routing when Bluetooth
    // output is connected.  So e/ac3 support is disabled when Bluetooth output device is connected.
    for (AudioDeviceInfo info : deviceInfos) {
      if (info.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP) {
        Log.i(
            TAG,
            "Passthrough on encoding %d is disabled because Bluetooth output device is"
                + " connected.",
            encoding);
        return false;
      }
    }

    // Sample rate is not provided when the function is called, assume it is 48000.
    final int defaultSurroundSampleRate = 48000;

    if (hasPassthroughSupportForV23(deviceInfos, encoding)) {
      Log.i(
          TAG,
          "Passthrough on encoding %d is supported, as hasPassthroughSupportForV23() returns"
              + " true.",
          encoding);
    } else {
      if (Build.VERSION.SDK_INT < 29) {
        Log.i(
            TAG,
            "Passthrough on encoding %d is rejected, as"
                + " hasDirectSurroundPlaybackSupportForV29() is not called for api %d.",
            encoding,
            Build.VERSION.SDK_INT);
        return false;
      }
      if (hasDirectSurroundPlaybackSupportForV29(encoding, defaultSurroundSampleRate)) {
        Log.i(
            TAG,
            "Passthrough on encoding %d is supported, as"
                + " hasDirectSurroundPlaybackSupportForV29() returns true.",
            encoding);
      } else {
        Log.i(
            TAG,
            "Passthrough on encoding %d is not supported, as"
                + " hasDirectSurroundPlaybackSupportForV29() returns false.",
            encoding);
        return false;
      }
    }

    Log.i(TAG, "Verify passthrough support by creating an AudioTrack.");

    try {
      AudioTrack audioTrack =
          new AudioTrack(
              getDefaultAudioAttributes(),
              getPassthroughAudioFormatFor(encoding, defaultSurroundSampleRate),
              AudioTrack.getMinBufferSize(48000, AudioFormat.CHANNEL_OUT_5POINT1, encoding),
              AudioTrack.MODE_STREAM,
              AudioManager.AUDIO_SESSION_ID_GENERATE);
      audioTrack.release();
    } catch (Exception e) {
      // AudioTrack creation can fail if the audio is routed to an unexpected device. For example,
      // when the user has Bluetooth headphones connected, or when the encoding is EAC3 and both
      // HDMI and SPDIF are connected, where the output should fallback to AC3.
      Log.w(
          TAG,
          "Passthrough on encoding %d is disabled because creating AudioTrack raises"
              + " exception: ",
          encoding,
          e);
      return false;
    }

    Log.i(TAG, "AudioTrack creation succeeded, passthrough support verified.");

    return true;
  }

  /** Returns whether passthrough on `encoding` is supported for API 23 and above. */
  private boolean hasPassthroughSupportForV23(final AudioDeviceInfo[] deviceInfos, int encoding) {
    for (AudioDeviceInfo info : deviceInfos) {
      final int type = info.getType();
      if (type != AudioDeviceInfo.TYPE_HDMI && type != AudioDeviceInfo.TYPE_HDMI_ARC) {
        continue;
      }
      // TODO: ExoPlayer uses ACTION_HDMI_AUDIO_PLUG to detect the encodings supported via
      //       passthrough, we should consider using it, and maybe other actions like
      //       ACTION_HEADSET_PLUG for general audio device switch/encoding detection.
      final int[] encodings = info.getEncodings();
      if (encodings.length == 0) {
        // Per https://developer.android.com/reference/android/media/AudioDeviceInfo#getEncodings()
        // an empty array indicates that the device supports arbitrary encodings.
        Log.i(
            TAG,
            "Passthrough on encoding %d is supported on %s, because getEncodings() returns"
                + " an empty array.",
            encoding,
            getDeviceTypeName(type));
        return true;
      }
      for (int i = 0; i < encodings.length; ++i) {
        if (encodings[i] == encoding) {
          Log.i(
              TAG,
              "Passthrough on encoding %d is supported on %s.",
              encoding,
              getDeviceTypeName(type));
          return true;
        }
      }
      Log.i(
          TAG,
          "Passthrough on encoding %d is not supported on %s.",
          encoding,
          getDeviceTypeName(type));
    }
    Log.i(TAG, "Passthrough on encoding %d is not supported on any devices.", encoding);
    return false;
  }

  @RequiresApi(29)
  /** Returns whether direct playback on surround `encoding` is supported for API 29 and above. */
  private boolean hasDirectSurroundPlaybackSupportForV29(int encoding, int sampleRate) {
    if (!isPassthroughEncoding(encoding)) {
      Log.w(
          TAG,
          "hasDirectSurroundPlaybackSupportForV29() encountered unsupported encoding %d.",
          encoding);
      return false;
    }

    boolean supported =
        AudioTrack.isDirectPlaybackSupported(
            getPassthroughAudioFormatFor(encoding, sampleRate), getDefaultAudioAttributes());
    Log.i(TAG, "isDirectPlaybackSupported() for encoding %d returned %b.", encoding, supported);
    return supported;
  }

  // TODO: Move utility functions into a separate class.
  /** Returns AudioFormat for surround `encoding` and `sampleRate`. */
  static AudioFormat getPassthroughAudioFormatFor(int encoding, int sampleRate) {
    return new AudioFormat.Builder()
        .setChannelMask(AudioFormat.CHANNEL_OUT_5POINT1)
        .setEncoding(encoding)
        .setSampleRate(sampleRate)
        .build();
  }

  /** Returns default AudioAttributes for surround playbacks. */
  static AudioAttributes getDefaultAudioAttributes() {
    // TODO: Turn this into a static variable after it is moved into a separate class.
    return new AudioAttributes.Builder()
        .setUsage(AudioAttributes.USAGE_MEDIA)
        .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
        .build();
  }

  private static AudioDeviceCallback sAudioDeviceCallback =
      new AudioDeviceCallback() {
        @Override
        public void onAudioDevicesAdded(AudioDeviceInfo[] addedDevices) {
          AudioOutputManagerJni.get().onAudioDeviceChanged();
        }

        @Override
        public void onAudioDevicesRemoved(AudioDeviceInfo[] removedDevices) {
          AudioOutputManagerJni.get().onAudioDeviceChanged();
        }
      };

  private static boolean sAudioDeviceListenerAdded = false;

  public static void addAudioDeviceListener(Context context) {
    if (sAudioDeviceListenerAdded) {
      return;
    }

    AudioManager audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
    audioManager.registerAudioDeviceCallback(sAudioDeviceCallback, null);
    sAudioDeviceListenerAdded = true;
  }

  @NativeMethods
  interface Natives {
    void onAudioDeviceChanged();

    boolean isSeamlessAudioSwitchingEnabled();
  }
}
