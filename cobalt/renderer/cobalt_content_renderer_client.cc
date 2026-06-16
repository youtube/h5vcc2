// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

#include "cobalt/renderer/cobalt_content_renderer_client.h"

#include <string>
#include <variant>

#include "base/task/bind_post_task.h"
#include "base/time/time.h"
#include "cobalt/media/service/mojom/platform_window_provider.mojom.h"
#include "cobalt/renderer/cobalt_render_frame_observer.h"
#include "cobalt/shell/common/url_constants.h"
#include "components/cdm/renderer/widevine_key_system_info.h"
#include "components/js_injection/renderer/js_communication.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_thread.h"
#include "media/base/decoder_buffer.h"
#include "media/base/key_systems_support_registration.h"
#include "media/base/media_log.h"
#include "media/base/renderer_factory.h"
#include "media/mojo/clients/starboard/starboard_renderer_client_factory.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "starboard/media.h"
#include "starboard/player.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_security_policy.h"
#include "third_party/blink/public/web/web_view.h"
#include "ui/gfx/geometry/size_conversions.h"

namespace cobalt {

namespace {

const char kWidevineL3KeySystem[] = "com.youtube.widevine.l3";

const char kH5vccSettingsKeyMediaAllowAudioWritingOnPause[] =
    "Media.AllowAudioWritingOnPause";
const char kH5vccSettingsKeyMediaBypassMojoForMedia[] =
    "Media.BypassMojoForMedia";
const char kH5vccSettingsKeyMediaEnableAv1StartupOptimization[] =
    "Media.EnableAv1StartupOptimization";
// TODO: b/474454335 - Remove once seek experiment is done.
const char kH5vccSettingsKeyMediaEnableFlushDuringSeek[] =
    "Media.EnableFlushDuringSeek";
const char kH5vccSettingsKeyMediaEnableLowLatency[] = "Media.EnableLowLatency";
// TODO: b/474454335 - Remove once seek experiment is done.
const char kH5vccSettingsKeyMediaEnableResetAudioDecoder[] =
    "Media.EnableResetAudioDecoder";
const char kH5vccSettingsKeyMediaEnableTrivialOptimizations[] =
    "Media.EnableTrivialOptimizations";
const char kH5vccSettingsKeyMediaEnableSimdBasedAudioFormatSwitching[] =
    "Media.EnableSimdBasedAudioFormatSwitching";
const char kH5vccSettingsKeyMediaEnableVideoRendererVspAdjustment[] =
    "Media.EnableVideoRendererVspAdjustment";
const char kH5vccSettingsKeyMediaFlushAudioTrackDuringSeek[] =
    "Media.FlushAudioTrackDuringSeek";
const char kH5vccSettingsKeyMediaForceDecodeToTexture[] =
    "Media.ForceDecodeToTexture";
const char kH5vccSettingsKeyMediaForceClearSurfaceView[] =
    "Media.ForceClearSurfaceView";
const char kH5vccSettingsKeyMediaIgnoreMediaCodecCallbacksDuringFlushing[] =
    "Media.IgnoreMediaCodecCallbacksDuringFlushing";
const char kH5vccSettingsKeyMediaVideoDecoderInitialPrerollCount[] =
    "Media.VideoDecoderInitialPrerollCount";
const char kH5vccSettingsKeyMediaVideoRendererMinInputBuffers[] =
    "Media.VideoRendererMinInputBuffers";
const char kH5vccSettingsKeyMediaVideoRendererMinDecodedFrames[] =
    "Media.VideoRendererMinDecodedFrames";
const char kH5vccSettingsKeyMediaMaxSamplesPerWrite[] =
    "Media.MaxSamplesPerWrite";
const char kH5vccSettingsKeyMediaSkipFlushOnDecoderTeardown[] =
    "Media.SkipFlushOnDecoderTeardown";
const char kH5vccSettingsKeyMediaSkipVideoFramesOver60Fps[] =
    "Media.SkipVideoFramesOver60Fps";

using ExperimentalFeatures =
    ::media::StarboardRendererConfig::ExperimentalFeatures;
using H5vccSettingValue = std::variant<std::string, int64_t>;

// TODO(b/376542844): Eliminate the usage of hardcoded MIME string once we
// support to query codec capabilities with configs. The profile information
// gets lost with hardcoded MIME string. This can sometimes cause issues. For
// example, vp9 profile 2 indicates hdr support, so an implementation accepts
// "codecs=vp9" may reject "codecs=vp9.2".
std::string GetMimeFromVideoType(const ::media::VideoType& type) {
  // The MIME string is for very basic video codec supportability check.
  switch (type.codec) {
    case ::media::VideoCodec::kH264:
      return "video/mp4; codecs=\"avc1.4d4015\"";
    case ::media::VideoCodec::kVP9:
      return "video/webm; codecs=\"vp9\"";
    case ::media::VideoCodec::kAV1:
      return "video/mp4; codecs=\"av01.0.08M.08\"";
    default:
      return "";
  }
}

// TODO(b/376542844): Eliminate the usage of hardcoded MIME string once we
// support to query codec capabilities with configs.
std::string GetMimeFromAudioType(const ::media::AudioType& type) {
  // The MIME string is for very basic audio codec supportability check.
  switch (type.codec) {
    case ::media::AudioCodec::kAAC:
      return "audio/mp4; codecs=\"mp4a.40.2\"";
    case ::media::AudioCodec::kAC3:
      return "audio/mp4; codecs=\"ac-3\"";
    case ::media::AudioCodec::kEAC3:
      return "audio/mp4; codecs=\"ec-3\"";
    case ::media::AudioCodec::kOpus:
      return "audio/webm; codecs=\"opus\"";
    // TODO(b/375232937): Support IAMF
    default:
      return "";
  }
}

::media::SupportedCodecs GetStarboardEmeSupportedCodecs() {
  ::media::SupportedCodecs codecs =
      ::media::EME_CODEC_AAC | ::media::EME_CODEC_AVC1 |
      ::media::EME_CODEC_VP9_PROFILE0 | ::media::EME_CODEC_VP9_PROFILE2 |
      ::media::EME_CODEC_VP8 | ::media::EME_CODEC_OPUS |
      ::media::EME_CODEC_VORBIS | ::media::EME_CODEC_MPEG_H_AUDIO |
      ::media::EME_CODEC_FLAC | ::media::EME_CODEC_HEVC_PROFILE_MAIN |
      ::media::EME_CODEC_HEVC_PROFILE_MAIN10 | ::media::EME_CODEC_AC3 |
      ::media::EME_CODEC_EAC3;

#if BUILDFLAG(ENABLE_AV1_DECODER)
  codecs |= ::media::EME_CODEC_AV1;
#endif

  // TODO(b/375232937) Add IAMF
  return codecs;
}

std::map<std::string, H5vccSettingValue> ParseH5vccSettings(
    cobalt::mojom::SettingsPtr settings) {
  std::map<std::string, H5vccSettingValue> h5vcc_settings;
  for (auto& [key, value] : settings->settings) {
    if (value->is_string_value()) {
      h5vcc_settings.emplace(key, std::move(value->get_string_value()));
    } else if (value->is_int_value()) {
      h5vcc_settings.emplace(key, value->get_int_value());
    } else {
      NOTREACHED();
    }
  }
  return h5vcc_settings;
}

template <typename T>
const T* GetSettingValue(
    const std::map<std::string, H5vccSettingValue>& settings,
    const std::string& key) {
  auto it = settings.find(key);
  if (it == settings.end()) {
    return nullptr;
  }
  return std::get_if<T>(&it->second);
}

// Experiment framework uses 0 as the sentinel value for unset.
// e.g.)
// http://go/latestexpcl/player_web/features/player_web_cobalt.impl.gcl;l=332;rcl=862772714
constexpr int kH5vccUnsetSentinel = 0;

std::optional<int> ProcessRangedIntH5vccSetting(
    const std::map<std::string, H5vccSettingValue>& settings,
    const char* key,
    int min_val,
    int max_val,
    int unset_sentinel) {
  auto* val = GetSettingValue<int64_t>(settings, key);
  if (!val) {
    return std::nullopt;
  }
  if (*val == unset_sentinel) {
    LOG(INFO) << "Value for " << key << " matches unset sentinel (" << *val
              << "); falling back to system default.";
    return std::nullopt;
  }
  if (*val < min_val || max_val < *val) {
    LOG(WARNING) << "Invalid value for " << key << ": " << *val;
    return std::nullopt;
  }

  return static_cast<int>(*val);
}

ExperimentalFeatures ProcessH5vccSettings(
    const std::map<std::string, H5vccSettingValue>& settings) {
  ExperimentalFeatures parsed;
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaAllowAudioWritingOnPause)) {
    parsed.allow_audio_writing_on_pause = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaBypassMojoForMedia)) {
    parsed.bypass_mojo_for_media = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaEnableAv1StartupOptimization)) {
    parsed.enable_av1_startup_optimization = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaEnableFlushDuringSeek)) {
    parsed.enable_flush_during_seek = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaEnableLowLatency)) {
    parsed.enable_low_latency = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaEnableResetAudioDecoder)) {
    parsed.enable_reset_audio_decoder = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaEnableTrivialOptimizations)) {
    parsed.enable_trivial_optimizations = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings,
          kH5vccSettingsKeyMediaEnableSimdBasedAudioFormatSwitching)) {
    parsed.enable_simd_based_audio_format_switching = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaEnableVideoRendererVspAdjustment)) {
    parsed.enable_video_renderer_vsp_adjustment = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaFlushAudioTrackDuringSeek)) {
    parsed.flush_audio_track_during_seek = *val != 0;
  }

  parsed.force_decode_to_texture = true;
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaForceDecodeToTexture)) {
    parsed.force_decode_to_texture = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaForceClearSurfaceView)) {
    parsed.force_clear_surface_view = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings,
          kH5vccSettingsKeyMediaIgnoreMediaCodecCallbacksDuringFlushing)) {
    parsed.ignore_mediacodec_callbacks_during_flushing = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaSkipFlushOnDecoderTeardown)) {
    parsed.skip_flush_on_decoder_teardown = *val != 0;
  }
  if (auto* val = GetSettingValue<int64_t>(
          settings, kH5vccSettingsKeyMediaSkipVideoFramesOver60Fps)) {
    parsed.skip_video_frames_over_60_fps = *val != 0;
  }

  parsed.video_decoder_initial_preroll_count = ProcessRangedIntH5vccSetting(
      settings, kH5vccSettingsKeyMediaVideoDecoderInitialPrerollCount,
      /*min_val=*/1, /*max_val=*/100'000, kH5vccUnsetSentinel);
  parsed.video_renderer_min_input_buffers = ProcessRangedIntH5vccSetting(
      settings, kH5vccSettingsKeyMediaVideoRendererMinInputBuffers,
      /*min_val=*/1, /*max_val=*/100'000, kH5vccUnsetSentinel);
  parsed.video_renderer_min_decoded_frames = ProcessRangedIntH5vccSetting(
      settings, kH5vccSettingsKeyMediaVideoRendererMinDecodedFrames,
      /*min_val=*/1, /*max_val=*/100'000, kH5vccUnsetSentinel);
  parsed.max_samples_per_write = ProcessRangedIntH5vccSetting(
      settings, kH5vccSettingsKeyMediaMaxSamplesPerWrite, /*min_val=*/1,
      /*max_val=*/100'000, kH5vccUnsetSentinel);
  return parsed;
}

class CobaltWidevineL3KeySystemInfo : public cdm::WidevineKeySystemInfo {
 public:
  using cdm::WidevineKeySystemInfo::WidevineKeySystemInfo;

  std::string GetBaseKeySystemName() const override {
    return kWidevineL3KeySystem;
  }

  bool IsSupportedKeySystem(const std::string& key_system) const override {
    return key_system == kWidevineL3KeySystem;
  }
};

}  // namespace

void CobaltContentRendererClient::EnsureH5vccSettingsRemoteInitialized() {
  CHECK(content::RenderThread::IsMainThread());
  if (h5vcc_settings_remote_) {
    return;
  }

  h5vcc_settings_remote_ = {
      new mojo::Remote<cobalt::mojom::H5vccSettings>(),
      base::OnTaskRunnerDeleter(
          base::SequencedTaskRunner::GetCurrentDefault())};
  content::RenderThread::Get()->BindHostReceiver(
      h5vcc_settings_remote_->BindNewPipeAndPassReceiver());
}

CobaltContentRendererClient::CobaltContentRendererClient()
    : h5vcc_settings_remote_(nullptr, base::OnTaskRunnerDeleter(nullptr)) {
  CHECK_CALLED_ON_VALID_THREAD(main_thread_checker_);
}

CobaltContentRendererClient::~CobaltContentRendererClient() {
  CHECK_CALLED_ON_VALID_THREAD(main_thread_checker_);
}

void CobaltContentRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  CHECK(content::RenderThread::IsMainThread());
  new js_injection::JsCommunication(render_frame);
  new CobaltRenderFrameObserver(render_frame);
  if (render_frame->GetWebView()) {
    viewport_size_ =
        gfx::ToCeiledSize(render_frame->GetWebView()->VisualViewportSize());
  } else {
    LOG(WARNING) << "RenderFrameCreated is called with no webview.";
  }

  if (sb_window_handle_.load() == 0 && !window_handle_requested_) {
    window_handle_requested_ = true;
    mojo::Remote<media::mojom::PlatformWindowProvider> window_provider;
    render_frame->GetBrowserInterfaceBroker().GetInterface(
        window_provider.BindNewPipeAndPassReceiver());

    auto* window_provider_ptr = window_provider.get();
    window_provider_ptr->GetSbWindow(
        base::BindPostTaskToCurrentDefault(base::BindOnce(
            [](base::WeakPtr<CobaltContentRendererClient> client,
               mojo::Remote<media::mojom::PlatformWindowProvider> remote,
               uint64_t handle) {
              if (client) {
                client->OnGetSbWindow(handle);
              }
            },
            weak_factory_.GetWeakPtr(), std::move(window_provider))));
  }

  EnsureH5vccSettingsRemoteInitialized();
}

void CobaltContentRendererClient::OnGetSbWindow(uint64_t handle) {
  CHECK(content::RenderThread::IsMainThread());
  if (!handle) {
    LOG(ERROR) << "Renderer received invalid SbWindow handle.";
    return;
  }

  LOG(INFO) << "Renderer received SbWindow handle: "
            << reinterpret_cast<void*>(handle);
  sb_window_handle_ = handle;
}

void CobaltContentRendererClient::RenderThreadStarted() {
  CHECK(content::RenderThread::IsMainThread());

  // Register h5vcc scheme for renders to use Fetch API.
  blink::WebSecurityPolicy::RegisterURLSchemeAsSupportingFetchAPI(
      blink::WebString::FromASCII(content::kH5vccEmbeddedScheme));
}

void AddStarboardCmaKeySystems(::media::KeySystemInfos* key_system_infos) {
  CHECK(content::RenderThread::IsMainThread());
  ::media::SupportedCodecs codecs = GetStarboardEmeSupportedCodecs();

  using Robustness = cdm::WidevineKeySystemInfo::Robustness;

  const base::flat_set<::media::EncryptionScheme> kEncryptionSchemes = {
      ::media::EncryptionScheme::kCenc, ::media::EncryptionScheme::kCbcs};

  const base::flat_set<::media::CdmSessionType> kSessionTypes = {
      ::media::CdmSessionType::kTemporary};

  key_system_infos->emplace_back(new cdm::WidevineKeySystemInfo(
      codecs,                        // Regular codecs.
      kEncryptionSchemes,            // Encryption schemes.
      kSessionTypes,                 // Session types.
      codecs,                        // Hardware secure codecs.
      kEncryptionSchemes,            // Hardware secure encryption schemes.
      kSessionTypes,                 // Hardware secure session types.
      Robustness::HW_SECURE_CRYPTO,  // Max audio robustness.
      Robustness::HW_SECURE_ALL,     // Max video robustness.
      ::media::EmeFeatureSupport::ALWAYS_ENABLED,    // Persistent state.
      ::media::EmeFeatureSupport::ALWAYS_ENABLED));  // Distinctive identifier.

  key_system_infos->emplace_back(new CobaltWidevineL3KeySystemInfo(
      codecs,                        // Regular codecs.
      kEncryptionSchemes,            // Encryption schemes.
      kSessionTypes,                 // Session types.
      {},                            // Hardware secure codecs.
      {},                            // Hardware secure encryption schemes.
      {},                            // Hardware secure session types.
      Robustness::SW_SECURE_CRYPTO,  // Max audio robustness.
      Robustness::SW_SECURE_DECODE,  // Max video robustness.
      ::media::EmeFeatureSupport::ALWAYS_ENABLED,    // Persistent state.
      ::media::EmeFeatureSupport::ALWAYS_ENABLED));  // Distinctive identifier.
}

std::unique_ptr<::media::KeySystemSupportRegistration>
CobaltContentRendererClient::GetSupportedKeySystems(
    content::RenderFrame* render_frame,
    ::media::GetSupportedKeySystemsCB cb) {
  CHECK(content::RenderThread::IsMainThread());
  ::media::KeySystemInfos key_systems;
  AddStarboardCmaKeySystems(&key_systems);
  std::move(cb).Run(std::move(key_systems));
  return nullptr;
}

bool CobaltContentRendererClient::IsDecoderSupportedAudioType(
    const ::media::AudioType& type) {
  std::string mime = GetMimeFromAudioType(type);
  SbMediaSupportType support_type = kSbMediaSupportTypeNotSupported;
  if (!mime.empty()) {
    support_type = SbMediaCanPlayMimeAndKeySystem(mime.c_str(), "");
  }
  bool result = support_type != kSbMediaSupportTypeNotSupported;
  LOG(INFO) << __func__ << "(" << type.codec << ") -> "
            << (result ? "true" : "false");
  return result;
}

bool CobaltContentRendererClient::IsDecoderSupportedVideoType(
    const ::media::VideoType& type) {
  std::string mime = GetMimeFromVideoType(type);
  SbMediaSupportType support_type = kSbMediaSupportTypeNotSupported;
  if (!mime.empty()) {
    support_type = SbMediaCanPlayMimeAndKeySystem(mime.c_str(), "");
  }
  bool result = support_type != kSbMediaSupportTypeNotSupported;
  LOG(INFO) << __func__ << "(" << type.codec << ") -> "
            << (result ? "true" : "false");
  return result;
}

void CobaltContentRendererClient::RunScriptsAtDocumentStart(
    content::RenderFrame* render_frame) {
  CHECK(content::RenderThread::IsMainThread());
  js_injection::JsCommunication* communication =
      js_injection::JsCommunication::Get(render_frame);
  communication->RunScriptsAtDocumentStart();
}

void CobaltContentRendererClient::GetStarboardRendererFactoryTraits(
    ::media::RendererFactoryTraits* renderer_factory_traits) {
  CHECK(content::RenderThread::IsMainThread());

  // TODO(b/383327725) - Cobalt: Inject these values from the web app.
  renderer_factory_traits->audio_write_duration_local =
      base::Microseconds(kSbPlayerWriteDurationLocal);
  renderer_factory_traits->audio_write_duration_remote =
      base::Microseconds(kSbPlayerWriteDurationRemote);
  renderer_factory_traits->viewport_size = viewport_size_;
#if BUILDFLAG(IS_STARBOARD)
  // Using base::Unretained(this) is safe here because
  // CobaltContentRendererClient is a process-global singleton that outlives
  // the media pipeline, and GetSbWindowHandle() only accesses an atomic
  // member variable.
  renderer_factory_traits->get_sb_window_handle_callback = base::BindRepeating(
      &CobaltContentRendererClient::GetSbWindowHandle, base::Unretained(this));
#endif  // BUILDFLAG(IS_STARBOARD)

  EnsureH5vccSettingsRemoteInitialized();

  cobalt::mojom::SettingsPtr settings;
  ExperimentalFeatures experimental_features;
  if ((*h5vcc_settings_remote_)->GetSettings(&settings) && settings) {
    auto h5vcc_settings = ParseH5vccSettings(std::move(settings));
    experimental_features = ProcessH5vccSettings(h5vcc_settings);
  }
  renderer_factory_traits->experimental_features = experimental_features;
}

void CobaltContentRendererClient::PostSandboxInitialized() {
  CHECK(content::RenderThread::IsMainThread());

  // Register the current thread (which is the InProcessRendererThread in
  // single- process mode) for hang watching. Store the ScopedClosureRunner to
  // keep the registration active until this client object is destroyed.
  if (base::HangWatcher::IsEnabled() && base::HangWatcher::GetInstance()) {
    // Use kRendererThread as the type for this in-process renderer thread.
    unregister_thread_closure = base::HangWatcher::RegisterThread(
        base::HangWatcher::ThreadType::kRendererThread);
  }
}

}  // namespace cobalt
