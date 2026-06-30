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

#include <jni.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "starboard/android/shared/audio_output_manager.h"
#include "starboard/android/shared/audio_renderer_passthrough.h"
#include "starboard/android/shared/audio_track_audio_sink_type.h"
#include "starboard/android/shared/drm_system.h"
#include "starboard/android/shared/media_capabilities_cache.h"
#include "starboard/android/shared/media_codec_audio_decoder.h"
#include "starboard/android/shared/media_codec_video_decoder.h"
#include "starboard/android/shared/media_common.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/ref_counted.h"
#include "starboard/common/string.h"
#include "starboard/media.h"
#include "starboard/shared/opus/opus_audio_decoder.h"
#include "starboard/shared/starboard/experimental_features.h"
#include "starboard/shared/starboard/features.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/media/mime_type.h"
#include "starboard/shared/starboard/player/buffer_internal.h"
#include "starboard/shared/starboard/player/filter/adaptive_audio_decoder_internal.h"
#include "starboard/shared/starboard/player/filter/audio_decoder_internal.h"
#include "starboard/shared/starboard/player/filter/audio_renderer_sink.h"
#include "starboard/shared/starboard/player/filter/audio_renderer_sink_impl.h"
#include "starboard/shared/starboard/player/filter/player_components.h"
#include "starboard/shared/starboard/player/filter/video_decoder_internal.h"
#include "starboard/shared/starboard/player/filter/video_render_algorithm.h"
#include "starboard/shared/starboard/player/filter/video_render_algorithm_impl.h"
#include "starboard/shared/starboard/player/filter/video_renderer_internal_impl.h"
#include "starboard/shared/starboard/player/filter/video_renderer_sink.h"
#include "third_party/jni_zero/jni_zero.h"

namespace starboard {
namespace {

using features::FeatureList;
using jni_zero::AttachCurrentThread;

// On some platforms tunnel mode is only supported in the secure pipeline.  Set
// the following variable to true to force creating a secure pipeline in tunnel
// mode, even for clear content.
// TODO: Allow this to be configured per playback at run time from the web app.
constexpr bool kForceSecurePipelineInTunnelModeWhenRequired = true;

bool UseLibopusDecoder(SbMediaAudioCodec codec,
                       SbDrmSystem drm_system,
                       bool force_platform_opus_decoder) {
  return codec == kSbMediaAudioCodecOpus && !SbDrmSystemIsValid(drm_system) &&
         !force_platform_opus_decoder;
}

bool IsTunnelModeVideoDecoderSupported(const std::string& mime,
                                       bool is_encrypted) {
  return MediaCapabilitiesCache::GetInstance()->HasVideoDecoderFor(
      mime, is_encrypted, /*must_support_hdr=*/false,
      /*must_support_tunnel_mode=*/true);
}

// This class allows us to force int16 sample type when tunnel mode is enabled.
class AudioRendererSinkAndroid : public AudioRendererSinkImpl {
 public:
  explicit AudioRendererSinkAndroid(
      std::optional<int> tunnel_mode_audio_session_id,
      bool allow_audio_writing_on_pause,
      bool enable_video_renderer_vsp_adjustment,
      bool allow_flush_during_seek)
      : AudioRendererSinkImpl(
            [=](int64_t start_media_time,
                int channels,
                int sampling_frequency_hz,
                SbMediaAudioSampleType audio_sample_type,
                SbMediaAudioFrameStorageType audio_frame_storage_type,
                SbAudioSinkFrameBuffers frame_buffers,
                int frame_buffers_size_in_frames,
                SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
                SbAudioSinkPrivate::ConsumeFramesFunc consume_frames_func,
                SbAudioSinkPrivate::ErrorFunc error_func,
                void* context) {
              auto type = static_cast<AudioTrackAudioSinkType*>(
                  SbAudioSinkImpl::GetPreferredType());

              return type->Create(
                  channels, sampling_frequency_hz, audio_sample_type,
                  audio_frame_storage_type, frame_buffers,
                  frame_buffers_size_in_frames,
                  {update_source_status_func, consume_frames_func, error_func},
                  start_media_time, tunnel_mode_audio_session_id,
                  /*is_web_audio=*/false, allow_audio_writing_on_pause,
                  context);
            }),
        is_tunnel_mode_enabled_(tunnel_mode_audio_session_id.has_value()),
        enable_video_renderer_vsp_adjustment_(
            enable_video_renderer_vsp_adjustment),
        allow_flush_during_seek_(allow_flush_during_seek) {}

  bool AllowOverflowAudioSamples() const override {
    return is_tunnel_mode_enabled_;
  }

  bool AllowDirectPlaybackRateSetting() const override {
    return is_tunnel_mode_enabled_ && !enable_video_renderer_vsp_adjustment_;
  }

  bool HasStarted() const override {
    return !is_flushed_ && AudioRendererSinkImpl::HasStarted();
  }

  void GetAudioRendererParams(const AudioStreamInfo& audio_stream_info,
                              int* max_cached_frames,
                              int* min_frames_per_append) const override {
    SB_CHECK(max_cached_frames);
    SB_CHECK(min_frames_per_append);
    SB_DCHECK_EQ(AudioRendererSink::kDefaultAudioSinkMinFramesPerAppend %
                     AudioRendererSink::kAudioSinkFramesAlignment,
                 0);
    *min_frames_per_append =
        AudioRendererSink::kDefaultAudioSinkMinFramesPerAppend;

    // AudioRenderer prefers to use kSbMediaAudioSampleTypeFloat32 and only uses
    // kSbMediaAudioSampleTypeInt16Deprecated when float32 is not supported.
    const auto sample_type =
        SbAudioSinkIsAudioSampleTypeSupported(kSbMediaAudioSampleTypeFloat32)
            ? kSbMediaAudioSampleTypeFloat32
            : kSbMediaAudioSampleTypeInt16Deprecated;

    int min_frames_required = SbAudioSinkGetMinBufferSizeInFrames(
        audio_stream_info.number_of_channels, sample_type,
        audio_stream_info.samples_per_second);

    if (is_tunnel_mode_enabled_) {
      // AudioTrack.setPlaybackParams() might need extra buffer to support
      // playback speed greater than 1.0x.
      const double kMaxPlaybackSpeed = 2.0;
      JNIEnv* env = AttachCurrentThread();
      min_frames_required = std::max<int>(
          min_frames_required,
          AudioOutputManager::GetInstance()->GetMinBufferSizeInFrames(
              env, sample_type, audio_stream_info.number_of_channels,
              audio_stream_info.samples_per_second) *
              kMaxPlaybackSpeed);
    }

    // On Android 5.0, the size of audio renderer sink buffer need to be two
    // times larger than AudioTrack minBufferSize. Otherwise, AudioTrack may
    // stop working after pause.
    *max_cached_frames = min_frames_required * 2 +
                         AudioRendererSink::kDefaultAudioSinkMinFramesPerAppend;
    *max_cached_frames = AlignUp(*max_cached_frames,
                                 AudioRendererSink::kAudioSinkFramesAlignment);
  }

  void Start(int64_t media_start_time,
             int channels,
             int sampling_frequency_hz,
             SbMediaAudioSampleType audio_sample_type,
             SbMediaAudioFrameStorageType audio_frame_storage_type,
             SbAudioSinkFrameBuffers frame_buffers,
             int frames_per_channel,
             RenderCallback* render_callback) override {
    is_flushed_ = false;
    // Re-use the existing audio sink if the new audio parameters match the
    // existing ones. Otherwise, fall back to the default behavior of destroying
    // and re-creating the sink.
    if (allow_flush_during_seek_ && audio_sink_ &&
        audio_sink_->IsType(SbAudioSinkImpl::GetPreferredType()) &&
        channels == channels_ &&
        sampling_frequency_hz == sampling_frequency_hz_ &&
        audio_sample_type == audio_sample_type_ &&
        audio_frame_storage_type == audio_frame_storage_type_) {
      SB_LOG(INFO) << "Audio track is already started with the same config, "
                   << "skipping Start().";
      auto* track_sink = static_cast<AudioTrackAudioSink*>(audio_sink_);
      track_sink->SetStartTime(media_start_time);
      // Explicitly set the playback rate and volume because HasStarted()
      // returns false while in the flushed state, causing the renderer to
      // skip updating the sink with these parameters during seek.
      track_sink->SetPlaybackRate(playback_rate_);
      track_sink->SetVolume(volume_);
      render_callback_ = render_callback;
      return;
    }

    channels_ = channels;
    sampling_frequency_hz_ = sampling_frequency_hz;
    audio_sample_type_ = audio_sample_type;
    audio_frame_storage_type_ = audio_frame_storage_type;

    AudioRendererSinkImpl::Start(media_start_time, channels,
                                 sampling_frequency_hz, audio_sample_type,
                                 audio_frame_storage_type, frame_buffers,
                                 frames_per_channel, render_callback);
  }

 private:
  bool IsAudioSampleTypeSupported(
      SbMediaAudioSampleType audio_sample_type) const override {
    if (is_tunnel_mode_enabled_) {
      // Currently the implementation only supports tunnel mode with int16 audio
      // samples.
      return audio_sample_type == kSbMediaAudioSampleTypeInt16Deprecated;
    }

    return SbAudioSinkIsAudioSampleTypeSupported(audio_sample_type);
  }

  void Reset() override {
    if (allow_flush_during_seek_ && audio_sink_ &&
        audio_sink_->IsType(SbAudioSinkImpl::GetPreferredType())) {
      auto* track_sink = static_cast<AudioTrackAudioSink*>(audio_sink_);
      if (track_sink->Flush()) {
        SB_LOG(INFO) << "Flushing AudioTrack.";
        is_flushed_ = true;
        return;
      }
    }
    SB_LOG(INFO) << "Resetting AudioTrack.";
    is_flushed_ = false;
    AudioRendererSink::Reset();
  }

  void Stop() override {
    is_flushed_ = false;
    AudioRendererSinkImpl::Stop();
  }

  const bool is_tunnel_mode_enabled_;
  const bool enable_video_renderer_vsp_adjustment_;
  const bool allow_flush_during_seek_;

  mutable bool is_flushed_ = false;

  int channels_ = -1;
  int sampling_frequency_hz_ = -1;
  SbMediaAudioSampleType audio_sample_type_ =
      kSbMediaAudioSampleTypeInt16Deprecated;
  SbMediaAudioFrameStorageType audio_frame_storage_type_ =
      kSbMediaAudioFrameStorageTypeInterleaved;
};

class PlayerComponentsPassthrough : public PlayerComponents {
 public:
  PlayerComponentsPassthrough(
      std::unique_ptr<AudioRendererPassthrough> audio_renderer,
      std::unique_ptr<VideoRenderer> video_renderer)
      : audio_renderer_(std::move(audio_renderer)),
        video_renderer_(std::move(video_renderer)) {}

 private:
  // PlayerComponents methods
  MediaTimeProvider* GetMediaTimeProvider() override {
    return audio_renderer_.get();
  }
  AudioRenderer* GetAudioRenderer() override { return audio_renderer_.get(); }
  VideoRenderer* GetVideoRenderer() override { return video_renderer_.get(); }

  std::unique_ptr<AudioRendererPassthrough> audio_renderer_;
  std::unique_ptr<VideoRenderer> video_renderer_;
};

class PlayerComponentsFactory : public PlayerComponents::Factory {
 public:
  PlayerComponentsFactory()
      : force_platform_opus_decoder_(features::FeatureList::IsEnabled(
            features::kForcePlatformOpusDecoder)) {
    SB_LOG_IF(INFO, force_platform_opus_decoder_)
        << "kForcePlatformOpusDecoder is set to true, force using platform opus"
        << " codec instead of libopus.";
  }

  NonNullResult<std::unique_ptr<PlayerComponents>> CreateComponents(
      const CreationParameters& creation_parameters) override {
    const auto& experimental_features =
        creation_parameters.experimental_features();

    Buffer::SetPoolEnabled(
        experimental_features.GetBool(kMediaDecodedAudioBufferPool));
    MediaCodecVideoDecoder::SetVideoFramePoolEnabled(
        experimental_features.GetBool(kMediaVideoFrameImplPool));

    if (experimental_features.GetBool(kMediaEnableAv1StartupOptimization)) {
      MediaCapabilitiesCache::GetInstance()->SetAv1OptEnabled(true);
      SB_LOG(INFO) << "`enable_av1_startup_optimization` is set to true.";
    }
    if (creation_parameters.audio_codec() != kSbMediaAudioCodecAc3 &&
        creation_parameters.audio_codec() != kSbMediaAudioCodecEac3) {
      SB_LOG(INFO) << "Creating non-passthrough components.";
      return PlayerComponents::Factory::CreateComponents(creation_parameters);
    }

    if (!creation_parameters.audio_mime().empty()) {
      auto audio_mime_type = MimeType::Create(creation_parameters.audio_mime());
      if (!audio_mime_type ||
          !audio_mime_type->ValidateBoolParameter("audiopassthrough")) {
        return Failure("Invalid audio mime type.");
      }
      if (!audio_mime_type->GetParamBoolValue("audiopassthrough", true)) {
        SB_LOG(INFO) << "Mime attribute \"audiopassthrough\" is set to: "
                        "false. Passthrough is disabled.";
        return Failure("Passthrough disabled by mime attribute.");
      }
    }

    bool enable_flush_during_seek =
        FeatureList::IsEnabled(features::kForceFlushDecoderDuringReset) ||
        creation_parameters.experimental_features().GetBool(
            kMediaEnableFlushDuringSeek);
    if (creation_parameters.video_codec() != kSbMediaVideoCodecNone &&
        !creation_parameters.video_mime().empty()) {
      auto video_mime_type = MimeType::Create(creation_parameters.video_mime());
      if (video_mime_type &&
          video_mime_type->ValidateBoolParameter("enableflushduringseek")) {
        enable_flush_during_seek =
            enable_flush_during_seek ||
            video_mime_type->GetParamBoolValue("enableflushduringseek", false);
      }
    }
    SB_LOG_IF(INFO, enable_flush_during_seek)
        << "`kForceFlushDecoderDuringReset` is set to true, force flushing"
        << " audio passthrough decoder during Reset().";

    SB_LOG(INFO) << "Creating passthrough components.";
    // TODO: Enable tunnel mode for passthrough
    auto audio_renderer = AudioRendererPassthrough::Create(
        creation_parameters.job_queue(),
        creation_parameters.audio_stream_info(),
        creation_parameters.drm_system(), enable_flush_during_seek);
    if (!audio_renderer) {
      return Failure("Failed to create audio renderer.");
    }

    // Set max_video_input_size with a positive value to overwrite
    // MediaFormat.KEY_MAX_INPUT_SIZE. Use 0 as default value.
    int max_video_input_size = creation_parameters.max_video_input_size();
    SB_LOG_IF(INFO, max_video_input_size > 0)
        << "The maximum size in bytes of a buffer of data is "
        << max_video_input_size;

    std::unique_ptr<VideoRenderer> video_renderer;
    if (creation_parameters.video_codec() != kSbMediaVideoCodecNone) {
      constexpr std::optional<int> kTunnelModeAudioSessionId = std::nullopt;
      constexpr bool kForceSecurePipelineUnderTunnelMode = false;

      auto video_decoder = CreateVideoDecoder(
          creation_parameters, kTunnelModeAudioSessionId,
          kForceSecurePipelineUnderTunnelMode, max_video_input_size);
      if (video_decoder) {
        auto video_decoder_impl = std::move(video_decoder.value());
        auto video_render_algorithm = video_decoder_impl->GetRenderAlgorithm();
        auto video_renderer_sink = video_decoder_impl->GetSink();
        auto media_time_provider = audio_renderer.value().get();

        video_renderer = std::make_unique<VideoRendererImpl>(
            creation_parameters.job_queue(), std::move(video_decoder_impl),
            media_time_provider, std::move(video_render_algorithm),
            video_renderer_sink, creation_parameters.experimental_features());
      } else {
        return Failure("Failed to create video decoder: " +
                       video_decoder.error());
      }
    }
    return std::make_unique<PlayerComponentsPassthrough>(
        std::move(audio_renderer.value()), std::move(video_renderer));
  }

  Result<MediaComponents> CreateSubComponents(
      const CreationParameters& creation_parameters) override {
    const std::string audio_mime =
        creation_parameters.audio_codec() != kSbMediaAudioCodecNone
            ? creation_parameters.audio_mime()
            : "";
    if (!audio_mime.empty() && !MimeType::Create(audio_mime)) {
      return Failure("Invalid audio MIME: '" + audio_mime + "'");
    }

    const std::string video_mime =
        creation_parameters.video_codec() != kSbMediaVideoCodecNone
            ? creation_parameters.video_mime()
            : "";
    auto video_mime_type = MimeType::Create(video_mime);
    if (!video_mime.empty() &&
        (!video_mime_type ||
         !video_mime_type->ValidateBoolParameter("tunnelmode") ||
         !video_mime_type->ValidateBoolParameter("enableflushduringseek") ||
         !video_mime_type->ValidateBoolParameter("enableresetaudiodecoder"))) {
      return Failure("Invalid video MIME: '" + video_mime + "'");
    }

    std::optional<int> tunnel_mode_audio_session_id = std::nullopt;
    bool enable_tunnel_mode = false;
    if (creation_parameters.audio_codec() != kSbMediaAudioCodecNone &&
        creation_parameters.video_codec() != kSbMediaVideoCodecNone) {
      const bool force_tunnel_mode =
          FeatureList::IsEnabled(features::kForceTunnelMode);
      enable_tunnel_mode =
          force_tunnel_mode ||
          (video_mime_type &&
           video_mime_type->GetParamBoolValue("tunnelmode", false));

      SB_LOG(INFO) << "Tunnel mode is "
                   << (enable_tunnel_mode ? "enabled. " : "disabled. ")
                   << "Video mime parameter \"tunnelmode\" value: "
                   << (video_mime_type ? video_mime_type->GetParamStringValue(
                                             "tunnelmode", "<not provided>")
                                       : "<not provided>")
                   << (force_tunnel_mode ? ", force tunnel mode is on." : ".");
    } else {
      SB_LOG(INFO) << "Tunnel mode requires both an audio and video stream. "
                   << "Audio codec: "
                   << GetMediaAudioCodecName(creation_parameters.audio_codec())
                   << ", Video codec: "
                   << GetMediaVideoCodecName(creation_parameters.video_codec())
                   << ". Tunnel mode is disabled.";
    }

    bool force_secure_pipeline_under_tunnel_mode = false;
    if (enable_tunnel_mode) {
      if (IsTunnelModeSupported(creation_parameters,
                                &force_secure_pipeline_under_tunnel_mode)) {
        tunnel_mode_audio_session_id =
            GenerateAudioSessionId(creation_parameters);
        SB_LOG(INFO) << "Generated tunnel mode audio session id "
                     << ToString(tunnel_mode_audio_session_id);
      } else {
        SB_LOG(INFO) << "IsTunnelModeSupported() failed, disable tunnel mode.";
      }
    } else {
      SB_LOG(INFO) << "Tunnel mode not enabled.";
    }

    if (!tunnel_mode_audio_session_id) {
      SB_LOG(INFO) << "Create non-tunnel mode pipeline.";
    } else {
      SB_LOG(INFO) << "Create tunnel mode pipeline with audio session id "
                   << *tunnel_mode_audio_session_id << '.';
    }

    const auto& experimental_features =
        creation_parameters.experimental_features();
    bool enable_reset_audio_decoder =
        FeatureList::IsEnabled(features::kForceResetAudioDecoder) ||
        experimental_features.GetBool(kMediaEnableResetAudioDecoder) ||
        (video_mime_type &&
         video_mime_type->GetParamBoolValue("enableresetaudiodecoder", false));
    SB_LOG_IF(INFO, enable_reset_audio_decoder)
        << "`enable_reset_audio_decoder` is set to true, force resetting"
        << " audio decoder during Reset(). Video mime parameter "
        << "\"enableresetaudiodecoder\" value: "
        << (video_mime_type ? video_mime_type->GetParamStringValue(
                                  "enableresetaudiodecoder", "<not provided>")
                            : "<not provided>")
        << ".";

    bool enable_flush_during_seek =
        FeatureList::IsEnabled(features::kForceFlushDecoderDuringReset) ||
        experimental_features.GetBool(kMediaEnableFlushDuringSeek) ||
        (video_mime_type &&
         video_mime_type->GetParamBoolValue("enableflushduringseek", false));
    SB_LOG_IF(INFO, enable_flush_during_seek)
        << "`enable_flush_during_seek` is set to true, force flushing"
        << " audio decoder during Reset(). Video mime parameter "
        << "\"enableflushduringseek\" value: "
        << (video_mime_type ? video_mime_type->GetParamStringValue(
                                  "enableflushduringseek", "<not provided>")
                            : "<not provided>")
        << ".";

    bool allow_flush_audio_track_during_seek =
        FeatureList::IsEnabled(features::kForceFlushAudioTrackDuringReset) ||
        experimental_features.GetBool(kMediaFlushAudioTrackDuringSeek);
    SB_LOG_IF(INFO, allow_flush_audio_track_during_seek)
        << "`kForceFlushAudioTrackDuringReset` is set to true, force flushing"
        << " audio track during Reset().";

    MediaComponents components;
    JobQueue* job_queue = creation_parameters.job_queue();

    if (creation_parameters.audio_codec() != kSbMediaAudioCodecNone) {
      // TODO: b/500811542 - Connect to H5VCC.
      const bool allow_audio_writing_on_pause =
          experimental_features.GetBool(kMediaAllowAudioWritingOnPause);
      SB_LOG_IF(INFO, allow_audio_writing_on_pause)
          << "allow_audio_writing_on_pause is set to true.";

      const bool enable_video_renderer_vsp_adjustment =
          experimental_features.GetBool(kMediaEnableVideoRendererVspAdjustment);
      SB_LOG_IF(INFO, enable_video_renderer_vsp_adjustment)
          << "enable_video_renderer_vsp_adjustment is set to true.";

      const bool force_platform_opus_decoder = force_platform_opus_decoder_;
      auto decoder_creator =
          [enable_flush_during_seek, force_platform_opus_decoder, job_queue](
              const AudioStreamInfo& audio_stream_info,
              SbDrmSystem drm_system) -> std::unique_ptr<AudioDecoder> {
        if (UseLibopusDecoder(audio_stream_info.codec, drm_system,
                              force_platform_opus_decoder)) {
          return OpusAudioDecoder::Create(job_queue, audio_stream_info);
        } else if (audio_stream_info.codec == kSbMediaAudioCodecAac ||
                   audio_stream_info.codec == kSbMediaAudioCodecOpus) {
          auto audio_decoder_impl = MediaCodecAudioDecoder::Create(
              job_queue, audio_stream_info, drm_system,
              enable_flush_during_seek);
          if (audio_decoder_impl) {
            return std::move(audio_decoder_impl.value());
          }
        } else {
          SB_LOG(ERROR) << "Unsupported audio codec "
                        << audio_stream_info.codec;
        }
        return nullptr;
      };

      components.audio.decoder = std::make_unique<AdaptiveAudioDecoder>(
          job_queue, creation_parameters.audio_stream_info(),
          creation_parameters.drm_system(), decoder_creator,
          enable_reset_audio_decoder);

      components.audio.renderer_sink =
          std::make_unique<AudioRendererSinkAndroid>(
              tunnel_mode_audio_session_id, allow_audio_writing_on_pause,
              enable_video_renderer_vsp_adjustment,
              allow_flush_audio_track_during_seek);
    }

    if (creation_parameters.video_codec() != kSbMediaVideoCodecNone) {
      // Set max_video_input_size with a positive value to overwrite
      // MediaFormat.KEY_MAX_INPUT_SIZE. Use 0 as default value.
      int max_video_input_size = creation_parameters.max_video_input_size();
      SB_LOG_IF(INFO, max_video_input_size > 0)
          << "The maximum size in bytes of a buffer of data is "
          << max_video_input_size;

      if (experimental_features.GetBool(
              kMediaEnableVideoRendererVspAdjustment) &&
          !experimental_features.GetBool(kMediaAllowAudioWritingOnPause)) {
        return Failure(
            "Video renderer vsp adjustment needs to be enabled with audio "
            "writing on pause.");
      }

      if (!tunnel_mode_audio_session_id) {
        force_secure_pipeline_under_tunnel_mode = false;
      }

      auto video_decoder_result = CreateVideoDecoder(
          creation_parameters, tunnel_mode_audio_session_id,
          force_secure_pipeline_under_tunnel_mode, max_video_input_size);
      if (video_decoder_result) {
        auto video_decoder_impl = std::move(video_decoder_result.value());
        components.video.render_algorithm =
            video_decoder_impl->GetRenderAlgorithm();
        components.video.renderer_sink = video_decoder_impl->GetSink();
        components.video.decoder = std::move(video_decoder_impl);
      } else {
        return Failure("Failed to create video decoder: " +
                       video_decoder_result.error());
      }
    }

    return components;
  }

  NonNullResult<std::unique_ptr<MediaCodecVideoDecoder>> CreateVideoDecoder(
      const CreationParameters& creation_parameters,
      std::optional<int> tunnel_mode_audio_session_id,
      bool force_secure_pipeline_under_tunnel_mode,
      int max_video_input_size) {
    auto experimental_features = creation_parameters.experimental_features();

    bool force_big_endian_hdr_metadata = false;
    bool enable_flush_during_seek =
        FeatureList::IsEnabled(features::kForceFlushDecoderDuringReset) ||
        experimental_features.GetBool(kMediaEnableFlushDuringSeek);
    int64_t flush_delay_usec = features::kFlushDelayUsec.Get();
    int64_t reset_delay_usec = features::kResetDelayUsec.Get();

    if (creation_parameters.video_codec() != kSbMediaVideoCodecNone &&
        !creation_parameters.video_mime().empty()) {
      // Use mime param to determine endianness of HDR metadata. If param is
      // missing or invalid it defaults to Little Endian.
      auto video_mime_type = MimeType::Create(creation_parameters.video_mime());
      if (video_mime_type && video_mime_type->ValidateStringParameter(
                                 "hdrinfoendianness", "big|little")) {
        const std::string& hdr_info_endianness =
            video_mime_type->GetParamStringValue("hdrinfoendianness",
                                                 /*default=*/"little");
        force_big_endian_hdr_metadata = hdr_info_endianness == "big";
      }
      if (video_mime_type &&
          video_mime_type->ValidateBoolParameter("enableflushduringseek")) {
        enable_flush_during_seek =
            enable_flush_during_seek ||
            video_mime_type->GetParamBoolValue("enableflushduringseek", false);
      }
    }

    SB_LOG_IF(INFO, enable_flush_during_seek)
        << "`kForceFlushDecoderDuringReset` is set to true, force flushing"
        << " video decoder during Reset().";
    SB_LOG_IF(INFO, flush_delay_usec > 0)
        << "`kFlushDelayUsec` is set to > 0, force a delay of "
        << flush_delay_usec << "us during Flush().";
    SB_LOG_IF(INFO, reset_delay_usec > 0)
        << "`kResetDelayUsec` is set to > 0, force a delay of "
        << reset_delay_usec << "us during Reset().";

    bool use_dual_threads = true;
    if (creation_parameters.audio_codec() != kSbMediaAudioCodecNone) {
      // `use_dual_threads` should be disabled if the libopus audio
      // decoder isn't used, as we want to limit the initial behavior to
      // playbacks with software based audio where their threading behavior is
      // more straightforward.
      // TODO(b/329686979): Make this work better with AdaptiveAudioDecoder,
      // where technically the stream can start with aac then transit into opus.
      if (!UseLibopusDecoder(creation_parameters.audio_codec(),
                             creation_parameters.drm_system(),
                             force_platform_opus_decoder_)) {
        use_dual_threads = false;
      }
    }

    return MediaCodecVideoDecoder::Create(
        creation_parameters.job_queue(),
        {creation_parameters.video_stream_info(),
         creation_parameters.drm_system(), creation_parameters.output_mode(),
         creation_parameters.decode_target_graphics_context_provider(),
         creation_parameters.surface_view(),
         creation_parameters.max_video_capabilities()},
        {tunnel_mode_audio_session_id, force_secure_pipeline_under_tunnel_mode},
        {max_video_input_size, enable_flush_during_seek, use_dual_threads,
         experimental_features},
        {force_big_endian_hdr_metadata, reset_delay_usec, flush_delay_usec});
  }

  bool IsTunnelModeSupported(const CreationParameters& creation_parameters,
                             bool* force_secure_pipeline_under_tunnel_mode) {
    SB_DCHECK(force_secure_pipeline_under_tunnel_mode);
    *force_secure_pipeline_under_tunnel_mode = false;

    if (!SbAudioSinkIsAudioSampleTypeSupported(
            kSbMediaAudioSampleTypeInt16Deprecated)) {
      SB_LOG(INFO) << "Disable tunnel mode because int16 sample is required "
                      "but not supported.";
      return false;
    }

    if (creation_parameters.output_mode() != kSbPlayerOutputModePunchOut) {
      SB_LOG(INFO)
          << "Disable tunnel mode because output mode is not punchout.";
      return false;
    }

    if (creation_parameters.audio_codec() == kSbMediaAudioCodecNone) {
      SB_LOG(INFO) << "Disable tunnel mode because audio codec is none.";
      return false;
    }

    if (creation_parameters.video_codec() == kSbMediaVideoCodecNone) {
      SB_LOG(INFO) << "Disable tunnel mode because video codec is none.";
      return false;
    }

    const char* mime =
        SupportedVideoCodecToMimeType(creation_parameters.video_codec());
    if (!mime) {
      SB_LOG(INFO) << "Disable tunnel mode because "
                   << creation_parameters.video_codec() << " is not supported.";
      return false;
    }
    DrmSystem* drm_system_ptr =
        static_cast<DrmSystem*>(creation_parameters.drm_system());
    bool is_encrypted = drm_system_ptr && drm_system_ptr->GetMediaCrypto();
    if (IsTunnelModeVideoDecoderSupported(mime, is_encrypted)) {
      return true;
    }

    if (kForceSecurePipelineInTunnelModeWhenRequired && !is_encrypted) {
      auto support_tunnel_mode_under_secure_pipeline =
          IsTunnelModeVideoDecoderSupported(mime, /*is_encrypted=*/true);
      if (support_tunnel_mode_under_secure_pipeline) {
        *force_secure_pipeline_under_tunnel_mode = true;
        return true;
      }
    }

    SB_LOG(INFO) << "Disable tunnel mode because no tunneled decoder for "
                 << mime << '.';
    return false;
  }

  std::optional<int> GenerateAudioSessionId(
      const CreationParameters& creation_parameters) {
    bool force_secure_pipeline_under_tunnel_mode = false;
    SB_DCHECK(IsTunnelModeSupported(creation_parameters,
                                    &force_secure_pipeline_under_tunnel_mode));

    JNIEnv* env = AttachCurrentThread();
    std::optional<int> tunnel_mode_audio_session_id =
        AudioOutputManager::GetInstance()->GenerateTunnelModeAudioSessionId(
            env, creation_parameters.audio_stream_info().number_of_channels);

    // AudioManager.generateAudioSessionId() return ERROR (-1) to indicate a
    // failure, please see the following url for more details:
    // https://developer.android.com/reference/android/media/AudioManager#generateAudioSessionId()
    SB_LOG_IF(WARNING, !tunnel_mode_audio_session_id)
        << "Failed to generate audio session id for tunnel mode.";

    return tunnel_mode_audio_session_id;
  }

 private:
  const bool force_platform_opus_decoder_;
};

}  // namespace
// static
std::unique_ptr<PlayerComponents::Factory> PlayerComponents::Factory::Create() {
  return std::make_unique<PlayerComponentsFactory>();
}

// static
bool PlayerComponents::Factory::OutputModeSupported(
    SbPlayerOutputMode output_mode,
    SbMediaVideoCodec codec,
    SbDrmSystem drm_system) {
  if (output_mode == kSbPlayerOutputModePunchOut) {
    return true;
  }

  if (output_mode == kSbPlayerOutputModeDecodeToTexture) {
    if (!SbDrmSystemIsValid(drm_system)) {
      return true;
    }
    DrmSystem* android_drm_system = static_cast<DrmSystem*>(drm_system);
    bool require_secure_decoder = android_drm_system->require_secured_decoder();
    SB_LOG_IF(INFO, require_secure_decoder)
        << "Output mode under decode-to-texture is not supported due to secure "
           "decoder is required.";
    return !require_secure_decoder;
  }

  return false;
}

}  // namespace starboard
