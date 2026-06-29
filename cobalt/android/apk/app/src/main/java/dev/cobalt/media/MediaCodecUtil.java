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

import android.media.MediaCodecInfo;
import android.media.MediaCodecInfo.CodecProfileLevel;
import android.media.MediaCodecList;
import android.os.Build;
import android.util.Range;
import dev.cobalt.util.IsEmulator;
import dev.cobalt.util.Log;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;

/** Utility functions for dealing with MediaCodec related things. */
@JNINamespace("starboard")
public class MediaCodecUtil {
  // A low priority deny list of video codec names that should never be used.
  private static final Set<String> sVideoCodecDenyList = new HashSet<>();
  // A high priority allow list of brands/model that should always attempt to
  // play vp9.
  private static final Map<String, Set<String>> sVp9AllowList = new HashMap<>();
  // Whether we should report vp9 codecs as supported or not.  Will be set
  // based on whether sVp9AllowList contains our brand/model.  If this is set
  // to true, then sVideoCodecDenyList will be ignored.
  private static boolean sIsVp9AllowListed;
  private static final String SECURE_DECODER_SUFFIX = ".secure";
  private static final String VP9_MIME_TYPE = "video/x-vnd.on2.vp9";
  private static final String AV1_MIME_TYPE = "video/av01";

  static {
    if (Build.BRAND.equals("google")) {
      sVideoCodecDenyList.add("OMX.Nvidia.vp9.decode");
    }
    if (Build.BRAND.equals("LGE")) {
      sVideoCodecDenyList.add("OMX.qcom.video.decoder.vp9");
    }
    if (Build.VERSION.RELEASE.startsWith("6.0.1")) {
      sVideoCodecDenyList.add("OMX.Exynos.vp9.dec");
      sVideoCodecDenyList.add("OMX.Intel.VideoDecoder.VP9.hwr");
      sVideoCodecDenyList.add("OMX.MTK.VIDEO.DECODER.VP9");
      sVideoCodecDenyList.add("OMX.qcom.video.decoder.vp9");
    }
    if (Build.VERSION.RELEASE.startsWith("6.0")) {
      sVideoCodecDenyList.add("OMX.MTK.VIDEO.DECODER.VP9");
      sVideoCodecDenyList.add("OMX.Nvidia.vp9.decode");
    }
    if (Build.VERSION.RELEASE.startsWith("5.1.1")) {
      sVideoCodecDenyList.add("OMX.allwinner.video.decoder.vp9");
      sVideoCodecDenyList.add("OMX.Exynos.vp9.dec");
      sVideoCodecDenyList.add("OMX.Intel.VideoDecoder.VP9.hwr");
      sVideoCodecDenyList.add("OMX.MTK.VIDEO.DECODER.VP9");
      sVideoCodecDenyList.add("OMX.qcom.video.decoder.vp9");
    }
    if (Build.VERSION.RELEASE.startsWith("5.1")) {
      sVideoCodecDenyList.add("OMX.Exynos.VP9.Decoder");
      sVideoCodecDenyList.add("OMX.Intel.VideoDecoder.VP9.hwr");
      sVideoCodecDenyList.add("OMX.MTK.VIDEO.DECODER.VP9");
    }
    if (Build.VERSION.RELEASE.startsWith("5.0")) {
      sVideoCodecDenyList.add("OMX.allwinner.video.decoder.vp9");
      sVideoCodecDenyList.add("OMX.Exynos.vp9.dec");
      sVideoCodecDenyList.add("OMX.Intel.VideoDecoder.VP9.hwr");
      sVideoCodecDenyList.add("OMX.MTK.VIDEO.DECODER.VP9");
    }

    if (Build.BRAND.equals("google")) {
      sVideoCodecDenyList.add("OMX.Intel.VideoDecoder.VP9.hybrid");
    }

    // Denylist non hardware media codec names if we aren't running on an emulator.
    if (!IsEmulator.isEmulator()) {
      sVideoCodecDenyList.add("OMX.ffmpeg.vp9.decoder");
      sVideoCodecDenyList.add("OMX.Intel.sw_vd.vp9");
      sVideoCodecDenyList.add("OMX.MTK.VIDEO.DECODER.SW.VP9");
    }

    // Denylist the Google software vp9 decoder both on hardware and on the emulator.
    // On the emulator it fails with the log: "storeMetaDataInBuffers failed w/ err -1010"
    sVideoCodecDenyList.add("OMX.google.vp9.decoder");

    sVp9AllowList.put("Amazon", new HashSet<String>());
    sVp9AllowList.put("Amlogic", new HashSet<String>());
    sVp9AllowList.put("Arcadyan", new HashSet<String>());
    sVp9AllowList.put("arcelik", new HashSet<String>());
    sVp9AllowList.put("BNO", new HashSet<String>());
    sVp9AllowList.put("BROADCOM", new HashSet<String>());
    sVp9AllowList.put("broadcom", new HashSet<String>());
    sVp9AllowList.put("Foxconn", new HashSet<String>());
    sVp9AllowList.put("Freebox", new HashSet<String>());
    sVp9AllowList.put("Funai", new HashSet<String>());
    sVp9AllowList.put("gfiber", new HashSet<String>());
    sVp9AllowList.put("Google", new HashSet<String>());
    sVp9AllowList.put("google", new HashSet<String>());
    sVp9AllowList.put("Hisense", new HashSet<String>());
    sVp9AllowList.put("HUAWEI", new HashSet<String>());
    sVp9AllowList.put("KaonMedia", new HashSet<String>());
    sVp9AllowList.put("LeTV", new HashSet<String>());
    sVp9AllowList.put("LGE", new HashSet<String>());
    sVp9AllowList.put("MediaTek", new HashSet<String>());
    sVp9AllowList.put("MStar", new HashSet<String>());
    sVp9AllowList.put("MTK", new HashSet<String>());
    sVp9AllowList.put("NVIDIA", new HashSet<String>());
    sVp9AllowList.put("PHILIPS", new HashSet<String>());
    sVp9AllowList.put("Philips", new HashSet<String>());
    sVp9AllowList.put("PIXELA CORPORATION", new HashSet<String>());
    sVp9AllowList.put("RCA", new HashSet<String>());
    sVp9AllowList.put("Sagemcom", new HashSet<String>());
    sVp9AllowList.put("samsung", new HashSet<String>());
    sVp9AllowList.put("SHARP", new HashSet<String>());
    sVp9AllowList.put("Skyworth", new HashSet<String>());
    sVp9AllowList.put("Sony", new HashSet<String>());
    sVp9AllowList.put("STMicroelectronics", new HashSet<String>());
    sVp9AllowList.put("SumitomoElectricIndustries", new HashSet<String>());
    sVp9AllowList.put("TCL", new HashSet<String>());
    sVp9AllowList.put("Technicolor", new HashSet<String>());
    sVp9AllowList.put("Vestel", new HashSet<String>());
    sVp9AllowList.put("wnc", new HashSet<String>());
    sVp9AllowList.put("Xiaomi", new HashSet<String>());
    sVp9AllowList.put("ZTE TV", new HashSet<String>());

    sVp9AllowList.get("Amazon").add("AFTS");
    sVp9AllowList.get("Amlogic").add("p212");
    sVp9AllowList.get("Arcadyan").add("Bouygtel4K");
    sVp9AllowList.get("Arcadyan").add("HMB2213PW22TS");
    sVp9AllowList.get("Arcadyan").add("IPSetTopBox");
    sVp9AllowList.get("arcelik").add("arcelik_uhd_powermax_at");
    sVp9AllowList.get("BNO").add("QM153E");
    sVp9AllowList.get("broadcom").add("avko");
    sVp9AllowList.get("broadcom").add("banff");
    sVp9AllowList.get("BROADCOM").add("BCM7XXX_TEST_SETTOP");
    sVp9AllowList.get("broadcom").add("cypress");
    sVp9AllowList.get("broadcom").add("dawson");
    sVp9AllowList.get("broadcom").add("elfin");
    sVp9AllowList.get("Foxconn").add("ba101");
    sVp9AllowList.get("Foxconn").add("bd201");
    sVp9AllowList.get("Freebox").add("Freebox Player Mini v2");
    sVp9AllowList.get("Funai").add("PHILIPS 4K TV");
    sVp9AllowList.get("gfiber").add("GFHD254");
    sVp9AllowList.get("google").add("avko");
    sVp9AllowList.get("google").add("marlin");
    sVp9AllowList.get("Google").add("Pixel XL");
    sVp9AllowList.get("Google").add("Pixel");
    sVp9AllowList.get("google").add("sailfish");
    sVp9AllowList.get("google").add("sprint");
    sVp9AllowList.get("Hisense").add("HAT4KDTV");
    sVp9AllowList.get("HUAWEI").add("X21");
    sVp9AllowList.get("KaonMedia").add("IC1110");
    sVp9AllowList.get("KaonMedia").add("IC1130");
    sVp9AllowList.get("KaonMedia").add("MCM4000");
    sVp9AllowList.get("KaonMedia").add("PRDMK100T");
    sVp9AllowList.get("KaonMedia").add("SFCSTB2LITE");
    sVp9AllowList.get("LeTV").add("uMax85");
    sVp9AllowList.get("LeTV").add("X4-43Pro");
    sVp9AllowList.get("LeTV").add("X4-55");
    sVp9AllowList.get("LeTV").add("X4-65");
    sVp9AllowList.get("LGE").add("S60CLI");
    sVp9AllowList.get("LGE").add("S60UPA");
    sVp9AllowList.get("LGE").add("S60UPI");
    sVp9AllowList.get("LGE").add("S70CDS");
    sVp9AllowList.get("LGE").add("S70PCI");
    sVp9AllowList.get("LGE").add("SH960C-DS");
    sVp9AllowList.get("LGE").add("SH960C-LN");
    sVp9AllowList.get("LGE").add("SH960S-AT");
    sVp9AllowList.get("MediaTek").add("Archer");
    sVp9AllowList.get("MediaTek").add("augie");
    sVp9AllowList.get("MediaTek").add("kane");
    sVp9AllowList.get("MStar").add("Denali");
    sVp9AllowList.get("MStar").add("Rainier");
    sVp9AllowList.get("MTK").add("Generic Android on sharp_2k15_us_android");
    sVp9AllowList.get("NVIDIA").add("SHIELD Android TV");
    sVp9AllowList.get("NVIDIA").add("SHIELD Console");
    sVp9AllowList.get("NVIDIA").add("SHIELD Portable");
    sVp9AllowList.get("PHILIPS").add("QM151E");
    sVp9AllowList.get("PHILIPS").add("QM161E");
    sVp9AllowList.get("PHILIPS").add("QM163E");
    sVp9AllowList.get("Philips").add("TPM171E");
    sVp9AllowList.get("PIXELA CORPORATION").add("POE-MP4000");
    sVp9AllowList.get("RCA").add("XLDRCAV1");
    sVp9AllowList.get("Sagemcom").add("DNA Android TV");
    sVp9AllowList.get("Sagemcom").add("GigaTV");
    sVp9AllowList.get("Sagemcom").add("M387_QL");
    sVp9AllowList.get("Sagemcom").add("Sagemcom Android STB");
    sVp9AllowList.get("Sagemcom").add("Sagemcom ATV Demo");
    sVp9AllowList.get("Sagemcom").add("Telecable ATV");
    sVp9AllowList.get("samsung").add("c71kw200");
    sVp9AllowList.get("samsung").add("GX-CJ680CL");
    sVp9AllowList.get("samsung").add("SAMSUNG-SM-G890A");
    sVp9AllowList.get("samsung").add("SAMSUNG-SM-G920A");
    sVp9AllowList.get("samsung").add("SAMSUNG-SM-G920AZ");
    sVp9AllowList.get("samsung").add("SAMSUNG-SM-G925A");
    sVp9AllowList.get("samsung").add("SAMSUNG-SM-G928A");
    sVp9AllowList.get("samsung").add("SM-G9200");
    sVp9AllowList.get("samsung").add("SM-G9208");
    sVp9AllowList.get("samsung").add("SM-G9209");
    sVp9AllowList.get("samsung").add("SM-G920A");
    sVp9AllowList.get("samsung").add("SM-G920D");
    sVp9AllowList.get("samsung").add("SM-G920F");
    sVp9AllowList.get("samsung").add("SM-G920FD");
    sVp9AllowList.get("samsung").add("SM-G920FQ");
    sVp9AllowList.get("samsung").add("SM-G920I");
    sVp9AllowList.get("samsung").add("SM-G920K");
    sVp9AllowList.get("samsung").add("SM-G920L");
    sVp9AllowList.get("samsung").add("SM-G920P");
    sVp9AllowList.get("samsung").add("SM-G920R4");
    sVp9AllowList.get("samsung").add("SM-G920R6");
    sVp9AllowList.get("samsung").add("SM-G920R7");
    sVp9AllowList.get("samsung").add("SM-G920S");
    sVp9AllowList.get("samsung").add("SM-G920T");
    sVp9AllowList.get("samsung").add("SM-G920T1");
    sVp9AllowList.get("samsung").add("SM-G920V");
    sVp9AllowList.get("samsung").add("SM-G920W8");
    sVp9AllowList.get("samsung").add("SM-G9250");
    sVp9AllowList.get("samsung").add("SM-G925A");
    sVp9AllowList.get("samsung").add("SM-G925D");
    sVp9AllowList.get("samsung").add("SM-G925F");
    sVp9AllowList.get("samsung").add("SM-G925FQ");
    sVp9AllowList.get("samsung").add("SM-G925I");
    sVp9AllowList.get("samsung").add("SM-G925J");
    sVp9AllowList.get("samsung").add("SM-G925K");
    sVp9AllowList.get("samsung").add("SM-G925L");
    sVp9AllowList.get("samsung").add("SM-G925P");
    sVp9AllowList.get("samsung").add("SM-G925R4");
    sVp9AllowList.get("samsung").add("SM-G925R6");
    sVp9AllowList.get("samsung").add("SM-G925R7");
    sVp9AllowList.get("samsung").add("SM-G925S");
    sVp9AllowList.get("samsung").add("SM-G925T");
    sVp9AllowList.get("samsung").add("SM-G925V");
    sVp9AllowList.get("samsung").add("SM-G925W8");
    sVp9AllowList.get("samsung").add("SM-G925Z");
    sVp9AllowList.get("samsung").add("SM-G9280");
    sVp9AllowList.get("samsung").add("SM-G9287");
    sVp9AllowList.get("samsung").add("SM-G9287C");
    sVp9AllowList.get("samsung").add("SM-G928A");
    sVp9AllowList.get("samsung").add("SM-G928C");
    sVp9AllowList.get("samsung").add("SM-G928F");
    sVp9AllowList.get("samsung").add("SM-G928G");
    sVp9AllowList.get("samsung").add("SM-G928I");
    sVp9AllowList.get("samsung").add("SM-G928K");
    sVp9AllowList.get("samsung").add("SM-G928L");
    sVp9AllowList.get("samsung").add("SM-G928N0");
    sVp9AllowList.get("samsung").add("SM-G928P");
    sVp9AllowList.get("samsung").add("SM-G928S");
    sVp9AllowList.get("samsung").add("SM-G928T");
    sVp9AllowList.get("samsung").add("SM-G928V");
    sVp9AllowList.get("samsung").add("SM-G928W8");
    sVp9AllowList.get("samsung").add("SM-G928X");
    sVp9AllowList.get("samsung").add("SM-G9300");
    sVp9AllowList.get("samsung").add("SM-G9308");
    sVp9AllowList.get("samsung").add("SM-G930A");
    sVp9AllowList.get("samsung").add("SM-G930AZ");
    sVp9AllowList.get("samsung").add("SM-G930F");
    sVp9AllowList.get("samsung").add("SM-G930FD");
    sVp9AllowList.get("samsung").add("SM-G930K");
    sVp9AllowList.get("samsung").add("SM-G930L");
    sVp9AllowList.get("samsung").add("SM-G930P");
    sVp9AllowList.get("samsung").add("SM-G930R4");
    sVp9AllowList.get("samsung").add("SM-G930R6");
    sVp9AllowList.get("samsung").add("SM-G930R7");
    sVp9AllowList.get("samsung").add("SM-G930S");
    sVp9AllowList.get("samsung").add("SM-G930T");
    sVp9AllowList.get("samsung").add("SM-G930T1");
    sVp9AllowList.get("samsung").add("SM-G930U");
    sVp9AllowList.get("samsung").add("SM-G930V");
    sVp9AllowList.get("samsung").add("SM-G930VL");
    sVp9AllowList.get("samsung").add("SM-G930W8");
    sVp9AllowList.get("samsung").add("SM-G9350");
    sVp9AllowList.get("samsung").add("SM-G935A");
    sVp9AllowList.get("samsung").add("SM-G935D");
    sVp9AllowList.get("samsung").add("SM-G935F");
    sVp9AllowList.get("samsung").add("SM-G935FD");
    sVp9AllowList.get("samsung").add("SM-G935J");
    sVp9AllowList.get("samsung").add("SM-G935K");
    sVp9AllowList.get("samsung").add("SM-G935L");
    sVp9AllowList.get("samsung").add("SM-G935P");
    sVp9AllowList.get("samsung").add("SM-G935R4");
    sVp9AllowList.get("samsung").add("SM-G935S");
    sVp9AllowList.get("samsung").add("SM-G935T");
    sVp9AllowList.get("samsung").add("SM-G935U");
    sVp9AllowList.get("samsung").add("SM-G935V");
    sVp9AllowList.get("samsung").add("SM-G935W8");
    sVp9AllowList.get("samsung").add("SM-N9200");
    sVp9AllowList.get("samsung").add("SM-N9208");
    sVp9AllowList.get("samsung").add("SM-N920A");
    sVp9AllowList.get("samsung").add("SM-N920C");
    sVp9AllowList.get("samsung").add("SM-N920F");
    sVp9AllowList.get("samsung").add("SM-N920G");
    sVp9AllowList.get("samsung").add("SM-N920I");
    sVp9AllowList.get("samsung").add("SM-N920K");
    sVp9AllowList.get("samsung").add("SM-N920L");
    sVp9AllowList.get("samsung").add("SM-N920R4");
    sVp9AllowList.get("samsung").add("SM-N920R6");
    sVp9AllowList.get("samsung").add("SM-N920R7");
    sVp9AllowList.get("samsung").add("SM-N920S");
    sVp9AllowList.get("samsung").add("SM-N920T");
    sVp9AllowList.get("samsung").add("SM-N920TP");
    sVp9AllowList.get("samsung").add("SM-N920V");
    sVp9AllowList.get("samsung").add("SM-N920W8");
    sVp9AllowList.get("samsung").add("SM-N920X");
    sVp9AllowList.get("SHARP").add("AN-NP40");
    sVp9AllowList.get("SHARP").add("AQUOS-4KTVJ17");
    sVp9AllowList.get("SHARP").add("AQUOS-4KTVT17");
    sVp9AllowList.get("SHARP").add("AQUOS-4KTVX17");
    sVp9AllowList.get("SHARP").add("LC-U35T");
    sVp9AllowList.get("SHARP").add("LC-UE630X");
    sVp9AllowList.get("SHARP").add("LC-Ux30US");
    sVp9AllowList.get("SHARP").add("LC-XU35T");
    sVp9AllowList.get("SHARP").add("LC-XU930X_830X");
    sVp9AllowList.get("Skyworth").add("globe");
    sVp9AllowList.get("Sony").add("Amai VP9");
    sVp9AllowList.get("Sony").add("BRAVIA 4K 2015");
    sVp9AllowList.get("Sony").add("BRAVIA 4K GB");
    sVp9AllowList.get("STMicroelectronics").add("sti4k");
    sVp9AllowList.get("SumitomoElectricIndustries").add("C02AS");
    sVp9AllowList.get("SumitomoElectricIndustries").add("ST4173");
    sVp9AllowList.get("SumitomoElectricIndustries").add("test_STW2000");
    sVp9AllowList.get("TCL").add("Percee TV");
    sVp9AllowList.get("Technicolor").add("AirTV Player");
    sVp9AllowList.get("Technicolor").add("Bouygtel4K");
    sVp9AllowList.get("Technicolor").add("CM-7600");
    sVp9AllowList.get("Technicolor").add("cooper");
    sVp9AllowList.get("Technicolor").add("Foxtel Now box");
    sVp9AllowList.get("Technicolor").add("pearl");
    sVp9AllowList.get("Technicolor").add("Sapphire");
    sVp9AllowList.get("Technicolor").add("Shortcut");
    sVp9AllowList.get("Technicolor").add("skipper");
    sVp9AllowList.get("Technicolor").add("STING");
    sVp9AllowList.get("Technicolor").add("TIM_BOX");
    sVp9AllowList.get("Technicolor").add("uzx8020chm");
    sVp9AllowList.get("Vestel").add("S7252");
    sVp9AllowList.get("Vestel").add("SmartTV");
    sVp9AllowList.get("wnc").add("c71kw400");
    sVp9AllowList.get("Xiaomi").add("MIBOX3");
    sVp9AllowList.get("ZTE TV").add("AV-ATB100");
    sVp9AllowList.get("ZTE TV").add("B860H");

    sIsVp9AllowListed =
        sVp9AllowList.containsKey(Build.BRAND)
            && sVp9AllowList.get(Build.BRAND).contains(Build.MODEL);
  }

  private MediaCodecUtil() {}

  /** A wrapper class of codec capability infos. */
  public static class CodecCapabilityInfo {
    private final MediaCodecInfo mCodecInfo;
    private final String mMimeType;
    private final String mDecoderName;
    private final MediaCodecInfo.CodecCapabilities mCodecCapabilities;
    private final MediaCodecInfo.AudioCapabilities mAudioCapabilities;
    private final MediaCodecInfo.VideoCapabilities mVideoCapabilities;

    CodecCapabilityInfo(MediaCodecInfo codecInfo, String mimeType) {
      mCodecInfo = codecInfo;
      mMimeType = mimeType;
      mDecoderName = codecInfo.getName();
      mCodecCapabilities = codecInfo.getCapabilitiesForType(mimeType);
      mAudioCapabilities = mCodecCapabilities.getAudioCapabilities();
      mVideoCapabilities = mCodecCapabilities.getVideoCapabilities();
    }

    @CalledByNative("CodecCapabilityInfo")
    public String getMimeType() {
      return mMimeType;
    }

    @CalledByNative("CodecCapabilityInfo")
    public String getDecoderName() {
      return mDecoderName;
    }

    public MediaCodecInfo.CodecCapabilities getCodecCapabilities() {
      return mCodecCapabilities;
    }

    @CalledByNative("CodecCapabilityInfo")
    public MediaCodecInfo.AudioCapabilities getAudioCapabilities() {
      return mAudioCapabilities;
    }

    @CalledByNative("CodecCapabilityInfo")
    public MediaCodecInfo.VideoCapabilities getVideoCapabilities() {
      return mVideoCapabilities;
    }

    @CalledByNative("CodecCapabilityInfo")
    public boolean isSecureRequired() {
      // MediaCodecList is supposed to feed us names of decoders that do NOT end in ".secure".  We
      // are then supposed to check if FEATURE_SecurePlayback is supported, and if it is and we
      // want a secure codec, we append ".secure" ourselves, and then pass that to
      // MediaCodec.createDecoderByName.  Some devices, do not follow this spec, and show us
      // decoders that end in ".secure".  Empirically, FEATURE_SecurePlayback has still been
      // correct when this happens.
      if (mDecoderName.endsWith(SECURE_DECODER_SUFFIX)) {
        // If a decoder name ends with ".secure", then we don't want to use it for clear content.
        return true;
      }
      return mCodecCapabilities.isFeatureRequired(
          MediaCodecInfo.CodecCapabilities.FEATURE_SecurePlayback);
    }

    @CalledByNative("CodecCapabilityInfo")
    public boolean isSecureSupported() {
      return mCodecCapabilities.isFeatureSupported(
          MediaCodecInfo.CodecCapabilities.FEATURE_SecurePlayback);
    }

    @CalledByNative("CodecCapabilityInfo")
    public boolean isTunnelModeRequired() {
      return mCodecCapabilities.isFeatureRequired(
          MediaCodecInfo.CodecCapabilities.FEATURE_TunneledPlayback);
    }

    @CalledByNative("CodecCapabilityInfo")
    public boolean isTunnelModeSupported() {
      return mCodecCapabilities.isFeatureSupported(
          MediaCodecInfo.CodecCapabilities.FEATURE_TunneledPlayback);
    }

    @CalledByNative("CodecCapabilityInfo")
    public boolean isSoftware() {
      return isSoftwareDecoder(mCodecInfo);
    }

    @CalledByNative("CodecCapabilityInfo")
    public boolean isHdrCapable() {
      return isHdrCapableVideoDecoder(mMimeType, mCodecCapabilities);
    }
  }

  /** Returns an array of CodecCapabilityInfo for all available decoders. */
  @CalledByNative
  public static @JniType("std::vector<CodecCapabilityInfo>") CodecCapabilityInfo[]
      getAllCodecCapabilityInfos() {
    List<CodecCapabilityInfo> codecCapabilityInfos = new ArrayList<>();

    for (MediaCodecInfo codecInfo : new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()) {
      // We don't use encoder.
      if (codecInfo.isEncoder()) {
        continue;
      }

      // Filter blacklisted video decoders.
      String name = codecInfo.getName();
      if (!sIsVp9AllowListed && isCodecDenyListed(name)) {
        Log.v(TAG, "Rejecting %s, reason: codec is on deny list", name);
        continue;
      }
      if (name.endsWith(SECURE_DECODER_SUFFIX)) {
        // If we want a secure decoder, then make sure the version without ".secure" isn't
        // denylisted.
        String nameWithoutSecureSuffix =
            name.substring(0, name.length() - SECURE_DECODER_SUFFIX.length());
        if (!sIsVp9AllowListed && isCodecDenyListed(nameWithoutSecureSuffix)) {
          String format = "Rejecting %s, reason: offpsec denylisted secure decoder";
          Log.v(TAG, format, name);
          continue;
        }
      }

      for (String mimeType : codecInfo.getSupportedTypes()) {
        codecCapabilityInfos.add(new CodecCapabilityInfo(codecInfo, mimeType));
      }
    }
    CodecCapabilityInfo[] array = new CodecCapabilityInfo[codecCapabilityInfos.size()];
    return codecCapabilityInfos.toArray(array);
  }

  /** Returns whether the codec is denylisted. */
  public static boolean isCodecDenyListed(String codecName) {
    return sVideoCodecDenyList.contains(codecName);
  }

  /** Simply returns SECURE_DECODER_SUFFIX to allow access to it. */
  public static String getSecureDecoderSuffix() {
    return SECURE_DECODER_SUFFIX;
  }

  /** Determine whether codecCapabilities is capable of playing HDR. */
  public static boolean isHdrCapableVideoDecoder(
      String mimeType, MediaCodecInfo.CodecCapabilities codecCapabilities) {
    // AV1ProfileMain10HDR10 value was not added until API level 29.  See
    // https://developer.android.com/reference/android/media/MediaCodecInfo.CodecProfileLevel.html.
    if (mimeType.equals(AV1_MIME_TYPE) && Build.VERSION.SDK_INT < 29) {
      return false;
    }

    if (codecCapabilities == null) {
      return false;
    }
    CodecProfileLevel[] codecProfileLevels = codecCapabilities.profileLevels;
    if (codecProfileLevels == null) {
      return false;
    }
    for (CodecProfileLevel codecProfileLevel : codecProfileLevels) {
      if (mimeType.equals(VP9_MIME_TYPE)) {
        if (codecProfileLevel.profile == CodecProfileLevel.VP9Profile2HDR
            || codecProfileLevel.profile == CodecProfileLevel.VP9Profile3HDR) {
          return true;
        }
      } else if (mimeType.equals(AV1_MIME_TYPE)) {
        if (codecProfileLevel.profile == CodecProfileLevel.AV1ProfileMain10HDR10) {
          return true;
        }
      }
    }

    return false;
  }

  /** Return true if and only if info belongs to a software decoder. */
  public static boolean isSoftwareDecoder(MediaCodecInfo codecInfo) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      if (!codecInfo.isHardwareAccelerated() || codecInfo.isSoftwareOnly()) {
        return true;
      }
    }
    String name = codecInfo.getName().toLowerCase(Locale.ROOT);
    // This is taken from libstagefright/OMXCodec.cpp for pre codec2.
    if (name.startsWith("omx.google.")) {
      return true;
    }

    // Codec2 names sw decoders this way.
    // See hardware/google/av/codec2/vndk/C2Store.cpp.
    if (name.startsWith("c2.google.") || name.startsWith("c2.android.")) {
      return true;
    }

    return false;
  }

  @CalledByNative
  public static int getRangeUpper(Range<Integer> range) {
    return range.getUpper();
  }

  @CalledByNative
  public static int getRangeLower(Range<Integer> range) {
    return range.getLower();
  }

  @CalledByNative
  public static Range<Integer> getAudioBitrateRange(
      MediaCodecInfo.AudioCapabilities audioCapabilities) {
    return audioCapabilities.getBitrateRange();
  }

  @CalledByNative
  public static Range<Integer> getVideoWidthRange(
      MediaCodecInfo.VideoCapabilities videoCapabilities) {
    return videoCapabilities.getSupportedWidths();
  }

  @CalledByNative
  public static Range<Integer> getVideoHeightRange(
      MediaCodecInfo.VideoCapabilities videoCapabilities) {
    return videoCapabilities.getSupportedHeights();
  }

  @CalledByNative
  public static Range<Integer> getVideoBitrateRange(
      MediaCodecInfo.VideoCapabilities videoCapabilities) {
    return videoCapabilities.getBitrateRange();
  }

  @CalledByNative
  public static Range<Integer> getVideoFrameRateRange(
      MediaCodecInfo.VideoCapabilities videoCapabilities) {
    return videoCapabilities.getSupportedFrameRates();
  }

  @CalledByNative
  public static boolean areSizeAndRateSupported(
      MediaCodecInfo.VideoCapabilities videoCapabilities, int width, int height, double frameRate) {
    return videoCapabilities.areSizeAndRateSupported(width, height, frameRate);
  }

  @CalledByNative
  public static boolean isSizeSupported(
      MediaCodecInfo.VideoCapabilities videoCapabilities, int width, int height) {
    return videoCapabilities.isSizeSupported(width, height);
  }

  /**
   * Returns the name of the video decoder if it is found, or "" otherwise.
   *
   * <p>NOTE: This code path is called repeatedly by the player to determine the decoding
   * capabilities of the device. To ensure speedy playback the code below should be kept performant.
   */
  @CalledByNative
  public static String findVideoDecoder(
      String mimeType,
      boolean mustSupportSecure,
      boolean mustSupportHdr,
      boolean mustSupportSoftwareCodec,
      boolean mustSupportTunnelMode,
      int decoderCacheTtlMs,
      int frameWidth,
      int frameHeight,
      int bitrate,
      int fps) {
    String decoderInfo =
        String.format(
            Locale.US,
            "Searching for video decoder with parameters mimeType: %s, secure: %b, frameWidth:"
                + " %d, frameHeight: %d, bitrate: %d, fps: %d, mustSupportHdr: %b,"
                + " mustSupportSoftwareCodec: %b, mustSupportTunnelMode: %b",
            mimeType,
            mustSupportSecure,
            frameWidth,
            frameHeight,
            bitrate,
            fps,
            mustSupportHdr,
            mustSupportSoftwareCodec,
            mustSupportTunnelMode);
    Log.v(TAG, decoderInfo);
    String deviceInfo =
        String.format(
            Locale.US,
            "brand: %s, model: %s, version: %s, API level: %d, sIsVp9AllowListed: %b",
            Build.BRAND,
            Build.MODEL,
            Build.VERSION.RELEASE,
            Build.VERSION.SDK_INT,
            sIsVp9AllowListed);
    Log.v(TAG, deviceInfo);

    for (VideoDecoderCache.CachedDecoder decoder :
        VideoDecoderCache.getCachedDecoders(mimeType, decoderCacheTtlMs)) {
      String name = decoder.info.getName();
      if (!sIsVp9AllowListed && isCodecDenyListed(name)) {
        Log.v(TAG, "Rejecting " + name + ", reason: codec is on deny list");
        continue;
      }

      if (mustSupportSoftwareCodec && !isSoftwareDecoder(decoder.info)) {
        Log.v(TAG, "Rejecting " + name + ", reason: require software codec");
        continue;
      }

      // MediaCodecList is supposed to feed us names of decoders that do NOT end in ".secure".  We
      // are then supposed to check if FEATURE_SecurePlayback is supported, and if it is and we
      // want a secure codec, we append ".secure" ourselves, and then pass that to
      // MediaCodec.createDecoderByName.  Some devices, do not follow this spec, and show us
      // decoders that end in ".secure".  Empirically, FEATURE_SecurePlayback has still been
      // correct when this happens.
      if (name.endsWith(SECURE_DECODER_SUFFIX)) {
        // If we don't want a secure decoder, then don't bother messing around with this thing.
        if (!mustSupportSecure) {
          Log.v(TAG, "Rejecting " + name + ", reason: want !secure decoder and ends with .secure");
          continue;
        }
        // If we want a secure decoder, then make sure the version without ".secure" isn't
        // denylisted.
        String nameWithoutSecureSuffix =
            name.substring(0, name.length() - SECURE_DECODER_SUFFIX.length());
        if (!sIsVp9AllowListed && isCodecDenyListed(nameWithoutSecureSuffix)) {
          Log.v(TAG, "Rejecting " + name + ", reason: denylisted secure decoder");
        }
      }

      boolean requiresSecurePlayback =
          decoder.codecCapabilities.isFeatureRequired(
              MediaCodecInfo.CodecCapabilities.FEATURE_SecurePlayback);
      boolean supportsSecurePlayback =
          decoder.codecCapabilities.isFeatureSupported(
              MediaCodecInfo.CodecCapabilities.FEATURE_SecurePlayback);
      if (mustSupportSecure && supportsSecurePlayback
          || !mustSupportSecure && requiresSecurePlayback) {
        String message =
            String.format(
                Locale.US,
                "Rejecting %s, reason: secure decoder requested: %b, "
                    + "codec FEATURE_SecurePlayback supported: %b, required: %b",
                name,
                mustSupportSecure,
                supportsSecurePlayback,
                requiresSecurePlayback);
        Log.v(TAG, message);
        continue;
      }

      boolean requiresTunneledPlayback =
          decoder.codecCapabilities.isFeatureRequired(
              MediaCodecInfo.CodecCapabilities.FEATURE_TunneledPlayback);
      boolean supportsTunneledPlayback =
          decoder.codecCapabilities.isFeatureSupported(
              MediaCodecInfo.CodecCapabilities.FEATURE_TunneledPlayback);
      if (mustSupportTunnelMode && !supportsTunneledPlayback
          || !mustSupportTunnelMode && requiresTunneledPlayback) {
        String message =
            String.format(
                Locale.US,
                "Rejecting %s, reason: tunneled playback requested: %b, "
                    + "codec FEATURE_TunneledPlayback supported: %b, required: %b",
                name,
                mustSupportTunnelMode,
                supportsTunneledPlayback,
                requiresTunneledPlayback);
        Log.v(TAG, message);
        continue;
      }

      MediaCodecInfo.VideoCapabilities videoCapabilities = decoder.videoCapabilities;
      Range<Integer> supportedWidths = decoder.supportedWidths;
      Range<Integer> supportedHeights = decoder.supportedHeights;

      if (frameWidth != 0 && frameHeight != 0) {
        if (!videoCapabilities.isSizeSupported(frameWidth, frameHeight)) {
          String format = "Rejecting %s, reason: width %s is not compatible with height %d";
          Log.v(TAG, format, name, frameWidth, frameHeight);
          continue;
        }
      } else if (frameWidth != 0) {
        if (!supportedWidths.contains(frameWidth)) {
          String format = "Rejecting %s, reason: supported widths %s does not contain %d";
          Log.v(TAG, format, name, supportedWidths.toString(), frameWidth);
          continue;
        }
      } else if (frameHeight != 0) {
        if (!supportedHeights.contains(frameHeight)) {
          String format = "Rejecting %s, reason: supported heights %s does not contain %d";
          Log.v(TAG, format, name, supportedHeights.toString(), frameHeight);
          continue;
        }
      }

      Range<Integer> bitrates = videoCapabilities.getBitrateRange();
      if (bitrate != 0 && !bitrates.contains(bitrate)) {
        String format = "Rejecting %s, reason: bitrate range %s does not contain %d";
        Log.v(TAG, format, name, bitrates.toString(), bitrate);
        continue;
      }

      Range<Integer> supportedFrameRates = videoCapabilities.getSupportedFrameRates();
      if (fps != 0) {
        if (frameHeight != 0 && frameWidth != 0) {
          if (!videoCapabilities.areSizeAndRateSupported(frameWidth, frameHeight, fps)) {
            String format = "Rejecting %s, reason: supported frame rates %s does not contain %d";
            Log.v(TAG, format, name, supportedFrameRates.toString(), fps);
            continue;
          }
        } else {
          // At least one of frameHeight or frameWidth is 0
          if (!supportedFrameRates.contains(fps)) {
            String format = "Rejecting %s, reason: supported frame rates %s does not contain %d";
            Log.v(TAG, format, name, supportedFrameRates.toString(), fps);
            continue;
          }
        }
      }

      if (mustSupportHdr && !isHdrCapableVideoDecoder(mimeType, decoder.codecCapabilities)) {
        Log.v(TAG, "Rejecting " + name + ", reason: codec does not support HDR");
        continue;
      }

      String resultName =
          (mustSupportSecure && !name.endsWith(SECURE_DECODER_SUFFIX))
              ? (name + SECURE_DECODER_SUFFIX)
              : name;
      Log.v(TAG, "Found suitable decoder, " + name);
      return resultName;
    }
    return "";
  }

  /** Return the name of the audio decoder if it is found, and "" otherwise. */
  @CalledByNative
  public static String findAudioDecoder(String mimeType, int bitrate) {
    // Note: MediaCodecList is sorted by the framework such that the best decoders come first.
    for (MediaCodecInfo info : new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()) {
      if (info.isEncoder()) {
        continue;
      }
      for (String supportedType : info.getSupportedTypes()) {
        if (!supportedType.equalsIgnoreCase(mimeType)) {
          continue;
        }
        String name = info.getName();
        MediaCodecInfo.CodecCapabilities codecCapabilities =
            info.getCapabilitiesForType(supportedType);
        MediaCodecInfo.AudioCapabilities audioCapabilities =
            codecCapabilities.getAudioCapabilities();
        Range<Integer> bitrateRange =
            Range.create(0, audioCapabilities.getBitrateRange().getUpper());
        if (!bitrateRange.contains(bitrate)) {
          continue;
        }
        return name;
      }
    }
    return "";
  }

  public static ByteBuffer getHdrStaticInfo(float primaryRChromaticityX,
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
    final int maxChromaticity = 50000; // Defined in CTA-861.3.
    final int defaultMaxCll = 1000;
    final int defaultMaxFall = 200;

    if (maxCll <= 0) {
      maxCll = defaultMaxCll;
    }
    if (maxFall <= 0) {
      maxFall = defaultMaxFall;
    }

    // This logic is inspired by
    // https://cs.android.com/android/_/android/platform/external/exoplayer/+/3423b4bbfffbb62b5f2d8f16cfdc984dc107cd02:tree/library/extractor/src/main/java/com/google/android/exoplayer2/extractor/mkv/MatroskaExtractor.java;l=2200-2215;drc=9af07bc62f8115cbaa6f1178ce8aa3533d2b9e29.
    ByteBuffer hdrStaticInfo = ByteBuffer.allocateDirect(25);
    // Force big endian in case the HDR metadata causes problems in production.
    if (forceBigEndianHdrMetadata) {
      hdrStaticInfo.order(ByteOrder.BIG_ENDIAN);
    } else {
      hdrStaticInfo.order(ByteOrder.LITTLE_ENDIAN);
    }

    hdrStaticInfo.put((byte) 0);
    hdrStaticInfo.putShort((short) ((primaryRChromaticityX * maxChromaticity) + 0.5f));
    hdrStaticInfo.putShort((short) ((primaryRChromaticityY * maxChromaticity) + 0.5f));
    hdrStaticInfo.putShort((short) ((primaryGChromaticityX * maxChromaticity) + 0.5f));
    hdrStaticInfo.putShort((short) ((primaryGChromaticityY * maxChromaticity) + 0.5f));
    hdrStaticInfo.putShort((short) ((primaryBChromaticityX * maxChromaticity) + 0.5f));
    hdrStaticInfo.putShort((short) ((primaryBChromaticityY * maxChromaticity) + 0.5f));
    hdrStaticInfo.putShort((short) ((whitePointChromaticityX * maxChromaticity) + 0.5f));
    hdrStaticInfo.putShort((short) ((whitePointChromaticityY * maxChromaticity) + 0.5f));
    hdrStaticInfo.putShort((short) (maxMasteringLuminance + 0.5f));
    hdrStaticInfo.putShort((short) (minMasteringLuminance + 0.5f));
    hdrStaticInfo.putShort((short) maxCll);
    hdrStaticInfo.putShort((short) maxFall);
    hdrStaticInfo.rewind();

    return hdrStaticInfo;
  }
}
