// Copyright 2022 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/android/shared/media_capabilities_cache.h"

#include <jni.h>

#include <mutex>
#include <utility>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "cobalt/android/jni_headers/MediaCodecUtil_jni.h"
#include "starboard/android/shared/audio_output_manager.h"
#include "starboard/android/shared/display_util.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/android/shared/media_drm_bridge.h"
#include "starboard/android/shared/starboard_bridge.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/common/once.h"
#include "starboard/common/string.h"
#include "starboard/shared/starboard/features.h"
#include "starboard/shared/starboard/media/key_system_supportability_cache.h"
#include "starboard/shared/starboard/media/mime_supportability_cache.h"
#include "starboard/shared/starboard/media/resolutions.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {
namespace {

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using features::FeatureList;
using jni_zero::AttachCurrentThread;
using jni_zero::JavaParamRef;
using jni_zero::ScopedJavaGlobalRef;
using jni_zero::ScopedJavaLocalRef;

// https://developer.android.com/reference/android/view/Display.HdrCapabilities.html#HDR_TYPE_HDR10
const jint HDR_TYPE_DOLBY_VISION = 1;
const jint HDR_TYPE_HDR10 = 2;
const jint HDR_TYPE_HLG = 3;
const jint HDR_TYPE_HDR10_PLUS = 4;

const char SECURE_DECODER_SUFFIX[] = ".secure";

Range ConvertJavaRangeToRange(JNIEnv* env,
                              const jni_zero::JavaRef<jobject>& j_range) {
  return Range(Java_MediaCodecUtil_getRangeLower(env, j_range),
               Java_MediaCodecUtil_getRangeUpper(env, j_range));
}

template <typename GetRangeFunc>
Range GetRange(JNIEnv* env,
               const jni_zero::JavaRef<jobject>& j_capabilities,
               GetRangeFunc get_range_func) {
  SB_CHECK(env);
  SB_CHECK(j_capabilities);
  ScopedJavaLocalRef<jobject> j_range = get_range_func(env, j_capabilities);
  SB_CHECK(j_range);
  return ConvertJavaRangeToRange(env, j_range);
}

void ConvertStringToLowerCase(std::string* str) {
  for (size_t i = 0; i < str->length(); i++) {
    (*str)[i] = std::tolower((*str)[i]);
  }
}

class MediaCapabilitiesProviderImpl : public MediaCapabilitiesProvider {
  bool GetIsWidevineSupported() override {
    return MediaDrmBridge::IsWidevineSupported(AttachCurrentThread());
  }
  bool GetIsCbcsSchemeSupported() override {
    return MediaDrmBridge::IsCbcsSupported(AttachCurrentThread());
  }
  std::set<SbMediaTransferId> GetSupportedHdrTypes() override {
    std::set<SbMediaTransferId> supported_transfer_ids;

    JNIEnv* env = AttachCurrentThread();
    std::vector<int> hdr_types = DisplayUtil::GetSupportedHdrTypes(env);
    for (int hdr_type : hdr_types) {
      switch (hdr_type) {
        case HDR_TYPE_DOLBY_VISION:
          continue;
        case HDR_TYPE_HDR10:
          supported_transfer_ids.insert(kSbMediaTransferIdSmpteSt2084);
          continue;
        case HDR_TYPE_HLG:
          supported_transfer_ids.insert(kSbMediaTransferIdAribStdB67);
          continue;
        case HDR_TYPE_HDR10_PLUS:
          continue;
      }
    }

    return supported_transfer_ids;
  }
  bool GetIsPassthroughSupported(SbMediaAudioCodec codec) override {
    SbMediaAudioCodingType coding_type;
    switch (codec) {
      case kSbMediaAudioCodecAc3:
        coding_type = kSbMediaAudioCodingTypeAc3;
        break;
      case kSbMediaAudioCodecEac3:
        coding_type = kSbMediaAudioCodingTypeDolbyDigitalPlus;
        break;
      default:
        return false;
    }
    int encoding = GetAudioFormatSampleType(coding_type);
    JNIEnv* env = AttachCurrentThread();
    return AudioOutputManager::GetInstance()->HasPassthroughSupportFor(
        env, encoding);
  }
  bool GetAudioConfiguration(
      int index,
      SbMediaAudioConfiguration* configuration) override {
    JNIEnv* env = AttachCurrentThread();
    return AudioOutputManager::GetInstance()->GetAudioConfiguration(
        env, index, configuration);
  }
  void GetCodecCapabilities(
      std::map<std::string, AudioCodecCapabilities>& audio_codec_capabilities,
      std::map<std::string, VideoCodecCapabilities>& video_codec_capabilities)
      override {
    JNIEnv* env = AttachCurrentThread();
    std::vector<CodecCapabilityInfo> codec_infos =
        Java_MediaCodecUtil_getAllCodecCapabilityInfos(env);

    // Note: Codec infos are sorted by the framework such that the best
    // decoders come first.
    // This order is maintained in the cache.
    for (const auto& codec_info : codec_infos) {
      if (codec_info.j_audio_capabilities) {
        // Found an audio decoder.
        audio_codec_capabilities[codec_info.mime_type].push_back(
            std::make_unique<AudioCodecCapability>(env, codec_info));
      } else if (codec_info.j_video_capabilities) {
        // Found a video decoder.
        video_codec_capabilities[codec_info.mime_type].push_back(
            std::make_unique<VideoCodecCapability>(env, codec_info));
      }
    }
  }
};
}  // namespace

CodecCapability::CodecCapability(const CodecCapabilityInfo& info)
    : name_(info.name),
      is_secure_required_(info.is_secure_required),
      is_secure_supported_(info.is_secure_supported),
      is_tunnel_mode_required_(info.is_tunnel_mode_required),
      is_tunnel_mode_supported_(info.is_tunnel_mode_supported) {}

CodecCapability::CodecCapability(std::string name,
                                 bool is_secure_req,
                                 bool is_secure_sup,
                                 bool is_tunnel_req,
                                 bool is_tunnel_sup)
    : name_(std::move(name)),
      is_secure_required_(is_secure_req),
      is_secure_supported_(is_secure_sup),
      is_tunnel_mode_required_(is_tunnel_req),
      is_tunnel_mode_supported_(is_tunnel_sup) {}

AudioCodecCapability::AudioCodecCapability(JNIEnv* env,
                                           const CodecCapabilityInfo& info)
    : CodecCapability(info), supported_bitrates_([env, &info] {
        Range supported_bitrates =
            GetRange(env, info.j_audio_capabilities,
                     &Java_MediaCodecUtil_getAudioBitrateRange);
        // Overwrite the lower bound to 0.
        supported_bitrates.minimum = 0;
        return supported_bitrates;
      }()) {}

AudioCodecCapability::AudioCodecCapability(std::string name,
                                           bool is_secure_req,
                                           bool is_secure_sup,
                                           bool is_tunnel_req,
                                           bool is_tunnel_sup,
                                           Range supported_bitrates)
    : CodecCapability(std::move(name),
                      is_secure_req,
                      is_secure_sup,
                      is_tunnel_req,
                      is_tunnel_sup),
      supported_bitrates_(supported_bitrates) {}

bool AudioCodecCapability::IsBitrateSupported(int bitrate) const {
  return supported_bitrates_.Contains(bitrate);
}

VideoCodecCapability::VideoCodecCapability(JNIEnv* env,
                                           const CodecCapabilityInfo& info)
    : CodecCapability(info),
      is_software_decoder_(info.is_software_decoder),
      is_hdr_capable_(info.is_hdr_capable),
      j_video_capabilities_(env, info.j_video_capabilities),
      supported_widths_(GetRange(env,
                                 j_video_capabilities_,
                                 &Java_MediaCodecUtil_getVideoWidthRange)),
      supported_heights_(GetRange(env,
                                  j_video_capabilities_,
                                  &Java_MediaCodecUtil_getVideoHeightRange)),
      supported_bitrates_(GetRange(env,
                                   j_video_capabilities_,
                                   &Java_MediaCodecUtil_getVideoBitrateRange)),
      supported_frame_rates_(
          GetRange(env,
                   j_video_capabilities_,
                   &Java_MediaCodecUtil_getVideoFrameRateRange)) {}

VideoCodecCapability::VideoCodecCapability(
    std::string name,
    bool is_secure_req,
    bool is_secure_sup,
    bool is_tunnel_req,
    bool is_tunnel_sup,
    bool is_software_decoder,
    bool is_hdr_capable,
    ScopedJavaGlobalRef<jobject> j_video_cap,
    Range supported_widths,
    Range supported_heights,
    Range supported_bitrates,
    Range supported_frame_rates)
    : CodecCapability(std::move(name),
                      is_secure_req,
                      is_secure_sup,
                      is_tunnel_req,
                      is_tunnel_sup),
      is_software_decoder_(is_software_decoder),
      is_hdr_capable_(is_hdr_capable),
      j_video_capabilities_(std::move(j_video_cap)),
      supported_widths_(supported_widths),
      supported_heights_(supported_heights),
      supported_bitrates_(supported_bitrates),
      supported_frame_rates_(supported_frame_rates) {}

bool VideoCodecCapability::IsBitrateSupported(int bitrate) const {
  return supported_bitrates_.Contains(bitrate);
}

bool VideoCodecCapability::AreResolutionAndRateSupported(Size size,
                                                         int fps) const {
  if (!(j_video_capabilities_.is_null())) {
    JNIEnv* env = AttachCurrentThread();
    if (!size.IsEmpty() && fps != 0) {
      return Java_MediaCodecUtil_areSizeAndRateSupported(
          env, j_video_capabilities_, size.width, size.height,
          static_cast<jdouble>(fps));
    } else if (!size.IsEmpty()) {
      return Java_MediaCodecUtil_isSizeSupported(env, j_video_capabilities_,
                                                 size.width, size.height);
    }
  }
  if (size.width != 0 && !supported_widths_.Contains(size.width)) {
    return false;
  }
  if (size.height != 0 && !supported_heights_.Contains(size.height)) {
    return false;
  }
  if (fps != 0 && !supported_frame_rates_.Contains(fps)) {
    return false;
  }
  return true;
}

// static
SB_ONCE_INITIALIZE_FUNCTION(MediaCapabilitiesCache,
                            MediaCapabilitiesCache::GetInstance)

bool MediaCapabilitiesCache::IsWidevineSupported() {
  if (!is_enabled_) {
    return media_capabilities_provider_->GetIsWidevineSupported();
  }
  std::lock_guard scoped_lock(mutex_);
  UpdateMediaCapabilities_Locked();
  return is_widevine_supported_;
}

bool MediaCapabilitiesCache::IsCbcsSchemeSupported() {
  if (!is_enabled_) {
    return media_capabilities_provider_->GetIsCbcsSchemeSupported();
  }
  std::lock_guard scoped_lock(mutex_);
  UpdateMediaCapabilities_Locked();
  return is_cbcs_supported_;
}

bool MediaCapabilitiesCache::IsHDRTransferCharacteristicsSupported(
    SbMediaTransferId transfer_id) {
  if (!is_enabled_) {
    std::set<SbMediaTransferId> supported_transfer_ids =
        media_capabilities_provider_->GetSupportedHdrTypes();
    return supported_transfer_ids.find(transfer_id) !=
           supported_transfer_ids.end();
  }
  std::lock_guard scoped_lock(mutex_);
  UpdateMediaCapabilities_Locked();
  return supported_transfer_ids_.find(transfer_id) !=
         supported_transfer_ids_.end();
}

bool MediaCapabilitiesCache::IsPassthroughSupported(SbMediaAudioCodec codec) {
  if (!is_enabled_) {
    return media_capabilities_provider_->GetIsPassthroughSupported(codec);
  }
  // IsPassthroughSupported() caches the results of previous quiries, and does
  // not rely on LazyInitialize(), which is different from other functions.
  std::lock_guard scoped_lock(mutex_);
  auto iter = passthrough_supportabilities_.find(codec);
  if (iter != passthrough_supportabilities_.end()) {
    return iter->second;
  }
  bool supported =
      media_capabilities_provider_->GetIsPassthroughSupported(codec);
  passthrough_supportabilities_[codec] = supported;
  return supported;
}

bool MediaCapabilitiesCache::IsAv18kCappedAt30() {
  if (!is_enabled_) {
    // When the cache is not enabled, always checks video fps.
    return true;
  }

#if !BUILDFLAG(IS_STARBOARD)
  const bool enable_av1_startup_optimization =
      FeatureList::IsEnabled(features::kEnableAv1StartupOptimization);
#else
  const bool enable_av1_startup_optimization = false;
#endif
  if (!enable_av1_startup_optimization && !is_av1_opt_enabled_) {
    return true;
  }

  return is_av1_8k_capped_at_30_;
}

bool MediaCapabilitiesCache::GetAudioConfiguration(
    int index,
    SbMediaAudioConfiguration* configuration) {
  SB_CHECK_GE(index, 0);
  if (!is_enabled_) {
    return media_capabilities_provider_->GetAudioConfiguration(index,
                                                               configuration);
  }

  std::lock_guard scoped_lock(mutex_);
  UpdateMediaCapabilities_Locked();
  if (static_cast<size_t>(index) < audio_configurations_.size()) {
    *configuration = audio_configurations_[index];
    return true;
  }
  return false;
}

bool MediaCapabilitiesCache::HasAudioDecoderFor(const std::string& mime_type,
                                                int bitrate) {
  return !FindAudioDecoder(mime_type, bitrate).empty();
}

bool MediaCapabilitiesCache::HasVideoDecoderFor(const std::string& mime_type,
                                                bool must_support_secure,
                                                bool must_support_hdr,
                                                bool must_support_tunnel_mode,
                                                Size frame_size,
                                                int bitrate,
                                                int fps) {
  return !FindVideoDecoder(mime_type, must_support_secure, must_support_hdr,
                           /*require_software_codec=*/false,
                           must_support_tunnel_mode, frame_size, bitrate, fps)
              .empty();
}

bool MediaCapabilitiesCache::HasVideoDecoderFor(const std::string& mime_type,
                                                bool must_support_secure,
                                                bool must_support_hdr,
                                                bool must_support_tunnel_mode) {
  return HasVideoDecoderFor(mime_type, must_support_secure, must_support_hdr,
                            must_support_tunnel_mode, Size(),
                            /*bitrate=*/0, /*fps=*/0);
}

std::string MediaCapabilitiesCache::FindAudioDecoder(
    const std::string& mime_type,
    int bitrate) {
  if (!is_enabled_) {
    JNIEnv* env = AttachCurrentThread();
    auto j_mime = ConvertUTF8ToJavaString(env, mime_type);
    auto j_decoder_name =
        Java_MediaCodecUtil_findAudioDecoder(env, j_mime, bitrate);
    return ConvertJavaStringToUTF8(env, j_decoder_name);
  }

  std::lock_guard scoped_lock(mutex_);
  UpdateMediaCapabilities_Locked();

  for (auto& audio_capability : audio_codec_capabilities_map_[mime_type]) {
    // Reject if bitrate is not supported.
    if (!audio_capability->IsBitrateSupported(bitrate)) {
      continue;
    }
    return audio_capability->name();
  }

  return "";
}

std::string MediaCapabilitiesCache::FindVideoDecoder(
    const std::string& mime_type,
    bool must_support_secure,
    bool must_support_hdr,
    bool require_software_codec,
    bool must_support_tunnel_mode) {
  return FindVideoDecoder(mime_type, must_support_secure, must_support_hdr,
                          require_software_codec, must_support_tunnel_mode,
                          Size(), /*bitrate=*/0, /*fps=*/0);
}

std::string MediaCapabilitiesCache::FindVideoDecoder(
    const std::string& mime_type,
    bool must_support_secure,
    bool must_support_hdr,
    bool require_software_codec,
    bool must_support_tunnel_mode,
    Size frame_size,
    int bitrate,
    int fps) {
  if (!is_enabled_) {
    JNIEnv* env = AttachCurrentThread();
    auto j_mime = ConvertUTF8ToJavaString(env, mime_type);
    auto j_decoder_name = Java_MediaCodecUtil_findVideoDecoder(
        env, j_mime, must_support_secure, must_support_hdr,
        /*mustSupportSoftwareCodec=*/false, must_support_tunnel_mode,
        /*decoderCacheTtlMs=*/-1, frame_size.width, frame_size.height, bitrate,
        fps);
    return ConvertJavaStringToUTF8(env, j_decoder_name);
  }

  std::lock_guard scoped_lock(mutex_);
  UpdateMediaCapabilities_Locked();

  for (auto& video_capability : video_codec_capabilities_map_[mime_type]) {
    // Reject if secure decoder is required but codec doesn't support it.
    if (must_support_secure && !video_capability->is_secure_supported()) {
      continue;
    }
    // Reject if non secure decoder is required but codec doesn't support it.
    if (!must_support_secure && video_capability->is_secure_required()) {
      continue;
    }
    // Reject if tunnel mode is required but codec doesn't support it.
    if (must_support_tunnel_mode &&
        !video_capability->is_tunnel_mode_supported()) {
      continue;
    }
    // Reject if non tunnel mode is required but codec doesn't support it.
    if (!must_support_tunnel_mode &&
        video_capability->is_tunnel_mode_required()) {
      continue;
    }
    // Reject if software codec is required but codec is not.
    if (require_software_codec && !video_capability->is_software_decoder()) {
      continue;
    }
    // Reject low performance software codec if software codec is not required.
    // This can help ensure that a hardware decoder is chosen when a device
    // reports support for a low performing software decoder.
    if (!require_software_codec && video_capability->is_software_decoder()) {
      if (!video_capability->AreResolutionAndRateSupported(Resolution::k1080p,
                                                           /*fps=*/0)) {
        continue;
      }
    }
    // Reject if hdr is required but codec doesn't support it.
    if (must_support_hdr && !video_capability->is_hdr_capable()) {
      continue;
    }
    // Reject if resolution or frame rate is not supported.
    if (!video_capability->AreResolutionAndRateSupported(frame_size, fps)) {
      continue;
    }
    // Reject if bitrate is not supported.
    if (bitrate != 0 && !video_capability->IsBitrateSupported(bitrate)) {
      continue;
    }

    // Append ".secure" for secure decoder if not represents.
    if (must_support_secure &&
        !EndsWith(video_capability->name(), SECURE_DECODER_SUFFIX)) {
      return video_capability->name() + SECURE_DECODER_SUFFIX;
    }
    return video_capability->name();
  }

  return "";
}

MediaCapabilitiesCache::MediaCapabilitiesCache()
    : MediaCapabilitiesCache(
          std::make_unique<MediaCapabilitiesProviderImpl>()) {}

MediaCapabilitiesCache::MediaCapabilitiesCache(
    std::unique_ptr<MediaCapabilitiesProvider> media_capabilities_provider)
    : media_capabilities_provider_(std::move(media_capabilities_provider)) {
  // Enable mime and key system caches.
  MimeSupportabilityCache::GetInstance()->SetCacheEnabled(true);
  KeySystemSupportabilityCache::GetInstance()->SetCacheEnabled(true);
}

void MediaCapabilitiesCache::UpdateMediaCapabilities_Locked() {
  if (!capabilities_is_dirty_.exchange(false)) {
    return;
  }
  // We use a different cache strategy (load and cache) for passthrough
  // supportabilities, so we only clear |passthrough_supportabilities_| here.
  passthrough_supportabilities_.clear();

  audio_codec_capabilities_map_.clear();
  video_codec_capabilities_map_.clear();
  audio_configurations_.clear();
  is_widevine_supported_ =
      media_capabilities_provider_->GetIsWidevineSupported();
  is_cbcs_supported_ = media_capabilities_provider_->GetIsCbcsSchemeSupported();
  supported_transfer_ids_ =
      media_capabilities_provider_->GetSupportedHdrTypes();
  media_capabilities_provider_->GetCodecCapabilities(
      audio_codec_capabilities_map_, video_codec_capabilities_map_);
  LoadAudioConfigurations_Locked();
  LoadIsAv18kCappedAt30_Locked();
}

void MediaCapabilitiesCache::LoadAudioConfigurations_Locked() {
  SB_CHECK(audio_configurations_.empty());

  // SbPlayerBridge::GetAudioConfigurations() reads up to 32 configurations.
  // The limit here is to avoid infinite loop and also match
  // SbPlayerBridge::GetAudioConfigurations().
  const int kMaxAudioConfigurations = 32;
  SbMediaAudioConfiguration configuration;
  while (audio_configurations_.size() < kMaxAudioConfigurations &&
         media_capabilities_provider_->GetAudioConfiguration(
             static_cast<int>(audio_configurations_.size()), &configuration)) {
    audio_configurations_.push_back(configuration);
  }
}

void MediaCapabilitiesCache::LoadIsAv18kCappedAt30_Locked() {
  is_av1_8k_capped_at_30_ = false;
  for (const auto& video_capability :
       video_codec_capabilities_map_[SupportedVideoCodecToMimeType(
           kSbMediaVideoCodecAv1)]) {
    if (video_capability->AreResolutionAndRateSupported(Resolution::k8k,
                                                        /*fps=*/0) &&
        !video_capability->AreResolutionAndRateSupported(Resolution::k8k,
                                                         /*fps=*/60)) {
      is_av1_8k_capped_at_30_ = true;
      break;
    }
  }
}

}  // namespace starboard

namespace jni_zero {
template <>
starboard::CodecCapabilityInfo FromJniType<starboard::CodecCapabilityInfo>(
    JNIEnv* env,
    const JavaRef<jobject>& j_codec_capability_info) {
  starboard::CodecCapabilityInfo info;

  ScopedJavaLocalRef<jstring> j_mime_type =
      starboard::Java_CodecCapabilityInfo_getMimeType(env,
                                                      j_codec_capability_info);
  info.mime_type = FromJniType<std::string>(env, j_mime_type);
  starboard::ConvertStringToLowerCase(&info.mime_type);

  ScopedJavaLocalRef<jstring> j_decoder_name =
      starboard::Java_CodecCapabilityInfo_getDecoderName(
          env, j_codec_capability_info);
  info.name = FromJniType<std::string>(env, j_decoder_name);

  info.is_secure_required =
      starboard::Java_CodecCapabilityInfo_isSecureRequired(
          env, j_codec_capability_info);
  info.is_secure_supported =
      starboard::Java_CodecCapabilityInfo_isSecureSupported(
          env, j_codec_capability_info);
  info.is_tunnel_mode_required =
      starboard::Java_CodecCapabilityInfo_isTunnelModeRequired(
          env, j_codec_capability_info);
  info.is_tunnel_mode_supported =
      starboard::Java_CodecCapabilityInfo_isTunnelModeSupported(
          env, j_codec_capability_info);
  info.is_software_decoder = starboard::Java_CodecCapabilityInfo_isSoftware(
      env, j_codec_capability_info);
  info.is_hdr_capable = starboard::Java_CodecCapabilityInfo_isHdrCapable(
      env, j_codec_capability_info);

  info.j_audio_capabilities =
      starboard::Java_CodecCapabilityInfo_getAudioCapabilities(
          env, j_codec_capability_info);
  info.j_video_capabilities =
      starboard::Java_CodecCapabilityInfo_getVideoCapabilities(
          env, j_codec_capability_info);

  return info;
}
}  // namespace jni_zero
