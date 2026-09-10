// Copyright 2024 The Cobalt Authors. All Rights Reserved.
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

#ifndef MEDIA_STARBOARD_STARBOARD_RENDERER_H_
#define MEDIA_STARBOARD_STARBOARD_RENDERER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "media/base/cdm_context.h"
#include "media/base/decoder_buffer.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_log.h"
#include "media/base/media_resource.h"
#include "media/base/pipeline_status.h"
#include "media/base/renderer.h"
#include "media/base/renderer_client.h"
#include "media/base/starboard/starboard_renderer_config.h"
#include "media/base/starboard/starboard_rendering_mode.h"
#include "media/base/timestamp_constants.h"
#include "media/starboard/sbplayer_bridge.h"
#include "ui/gfx/color_space.h"

#if BUILDFLAG(IS_ANDROID)
#include "media/base/android_overlay_mojo_factory.h"
#endif  // BUILDFLAG(IS_ANDROID)

namespace media {
using base::Time;
using base::TimeDelta;

// SbPlayer based Renderer implementation, the entry point for all video
// playbacks on Starboard platforms. Every Starboard renderer is usually
// owned by StarboardRendererWrapper and must live on a single
// thread/process/TaskRunner, usually Chrome_InProcGpuThread.
class MEDIA_EXPORT StarboardRenderer : public Renderer,
                                       private SbPlayerBridge::Host {
 public:
  StarboardRenderer(const scoped_refptr<base::SequencedTaskRunner>& task_runner,
                    std::unique_ptr<MediaLog> media_log,
                    const base::UnguessableToken& overlay_plane_id,
                    const std::string& max_video_capabilities,
                    const StarboardRendererConfig::ExperimentalFeatures&
                        experimental_features,
                    const gfx::Size& viewport_size
#if BUILDFLAG(IS_ANDROID)
                    ,
                    const AndroidOverlayMojoFactoryCB android_overlay_factory_cb
#endif  // BUILDFLAG(IS_ANDROID)
  );

  // Disallow copy and assign.
  StarboardRenderer(const StarboardRenderer&) = delete;
  StarboardRenderer& operator=(const StarboardRenderer&) = delete;

  ~StarboardRenderer() override;

  // Renderer implementation.
  void Initialize(MediaResource* media_resource,
                  RendererClient* client,
                  PipelineStatusCallback init_cb) override;
  void SetCdm(CdmContext* cdm_context, CdmAttachedCB cdm_attached_cb) override;
  void SetLatencyHint(std::optional<TimeDelta> latency_hint) override {
    // TODO(b/380935131): Consider to implement `LatencyHint` for SbPlayer.
    NOTIMPLEMENTED();
  }
  void SetPreservesPitch(bool preserves_pitch) override {
    LOG_IF(INFO, !preserves_pitch)
        << "SetPreservesPitch() with preserves_pitch=false is not supported.";
  }
  void SetWasPlayedWithUserActivationAndHighMediaEngagement(
      bool was_played_with_user_activation_and_high_media_engagement) override {
    LOG_IF(INFO, was_played_with_user_activation_and_high_media_engagement)
        << "SetWasPlayedWithUserActivationAndHighMediaEngagement() with "
           "was_played_with_user_activation=true is not supported.";
  }
  void Flush(base::OnceClosure flush_cb) override;
  void StartPlayingFrom(TimeDelta time) override;
  void SetPlaybackRate(double playback_rate) override;
  void SetVolume(float volume) override;
  TimeDelta GetMediaTime() override;
  void OnTracksChanged(DemuxerStream::Type track_type,
                       std::vector<DemuxerStream*> enabled_tracks,
                       base::OnceClosure change_completed_cb) override;
  RendererType GetRendererType() override { return RendererType::kStarboard; }

  using PaintVideoHoleFrameCallback =
      base::RepeatingCallback<void(const gfx::Size&)>;
  using UpdateStarboardRenderingModeCallback =
      base::RepeatingCallback<void(const StarboardRenderingMode mode)>;
  using GetSbWindowHandleCallback = base::RepeatingCallback<void()>;
#if BUILDFLAG(IS_IOS_TVOS)
  using DurationChangeCB =
      base::RepeatingCallback<void(base::TimeDelta duration)>;
  using BufferedRangesCB =
      base::RepeatingCallback<void(base::TimeDelta start,
                                   base::TimeDelta length)>;
  using EncryptedMediaInitDataCB =
      base::RepeatingCallback<void(const std::string& init_data_type,
                                   const std::vector<uint8_t>& init_data)>;

  void SetDurationChangeCB(DurationChangeCB cb) {
    duration_change_cb_ = std::move(cb);
  }
  void SetBufferedRangesCB(BufferedRangesCB cb) {
    buffered_ranges_cb_ = std::move(cb);
  }
  void SetEncryptedMediaInitDataCB(EncryptedMediaInitDataCB cb) {
    encrypted_media_init_data_cb_ = std::move(cb);
  }
  void SetSourceUrl(const std::string& source_url);
#endif  // BUILDFLAG(IS_IOS_TVOS)

#if BUILDFLAG(IS_ANDROID)
  using RequestOverlayInfoCallBack =
      base::RepeatingCallback<void(bool restart_for_transitions)>;
#endif  // BUILDFLAG(IS_ANDROID)
  void SetStarboardRendererCallbacks(
      PaintVideoHoleFrameCallback paint_video_hole_frame_cb,
      UpdateStarboardRenderingModeCallback update_starboard_rendering_mode_cb,
      GetSbWindowHandleCallback get_sb_window_handle_cb
#if BUILDFLAG(IS_ANDROID)
      ,
      RequestOverlayInfoCallBack request_overlay_info_cb
#endif  // BUILDFLAG(IS_ANDROID)
  );

  void OnVideoGeometryChange(const gfx::Rect& output_rect);
  void OnSbWindowHandleReady(const uint64_t sb_window_handle);
#if BUILDFLAG(IS_ANDROID)
  void OnOverlayInfoChanged(const OverlayInfo& overlay_info);
#endif  // BUILDFLAG(IS_ANDROID)
  SbDecodeTarget GetSbDecodeTarget();
  // Call to get the SbDecodeTargetGraphicsContextProvider for SbPlayerCreate().
  using GetDecodeTargetGraphicsContextProviderFunc =
      base::RepeatingCallback<SbDecodeTargetGraphicsContextProvider*()>;
  void set_decode_target_graphics_context_provider(
      const GetDecodeTargetGraphicsContextProviderFunc&
          get_decode_target_graphics_context_provider_func);
  const gfx::ColorSpace& color_space() const { return color_space_; }

  SbPlayerInterface* GetSbPlayerInterface();

 private:
  enum State {
    STATE_UNINITIALIZED,
    STATE_INIT_PENDING_CDM,  // Initialization is waiting for the CDM to be set.
    STATE_INITIALIZING,      // Initializing to create SbPlayerBridge.
    STATE_FLUSHED,           // After initialization or after flush completed.
    STATE_PLAYING,           // After StartPlayingFrom has been called.
    STATE_ERROR
  };

#if BUILDFLAG(IS_IOS_TVOS)
  bool IsUrlPlayer() const;
  // Handles presenting state for URL player: propagates video resolution
  // for hole-punch rendering and re-applies playback rate.
  void OnUrlPlayerPresenting();
  // Reads and propagates a valid URL-player resolution when it changes.
  void UpdateUrlPlayerVideoResolution();
  void OnEncryptedMediaInitDataEncountered(
      const std::string& init_data_type,
      const std::vector<uint8_t>& init_data);
#endif  // BUILDFLAG(IS_IOS_TVOS)

  void UpdateAudioWriteDuration();
  void CreatePlayerBridge();
  void ApplyPendingBounds();
  void UpdateDecoderConfig(DemuxerStream* stream);
  void OnDemuxerStreamRead(DemuxerStream* stream,
                           int max_buffers,
                           DemuxerStream::Status status,
                           DemuxerStream::DecoderBufferVector buffers);
  void OnStatisticsUpdate(const PipelineStatistics& stats);

  // SbPlayerBridge::Host implementation.
  void OnNeedData(DemuxerStream::Type type,
                  int max_number_of_buffers_to_write) override;
  void OnPlayerStatus(SbPlayerState state) override;
  void OnPlayerError(SbPlayerError error, const std::string& message) override;

  // Used to make a delayed call to OnNeedData() if |audio_read_delayed_| is
  // true. If |audio_read_delayed_| is false, that means the delayed call has
  // been cancelled due to a seek.
  void DelayedNeedData(int max_number_of_buffers_to_write);

  // Store the media time retrieved by GetMediaTime so we can cache it as an
  // estimate and avoid calling SbPlayerGetInfo too frequently.
  void StoreMediaTime(TimeDelta media_time);

#if BUILDFLAG(IS_ANDROID)
  // AndroidOverlay callbacks.
  void OnOverlayReady(AndroidOverlay*);
  void OnOverlayFailed(AndroidOverlay*);
  bool IsSecondaryVideoProtected() const;
#endif  // BUILDFLAG(IS_ANDROID)

  int GetDefaultMaxBuffers(AudioCodec codec,
                           TimeDelta duration_to_write,
                           bool is_preroll);

  int GetEstimatedMaxBuffers(TimeDelta write_duration,
                             TimeDelta time_ahead_of_playback,
                             bool is_preroll);

  void OnBufferingStateChange(BufferingState state);

  void NotifyError(PipelineStatus status);

  State state_;
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;
  const std::unique_ptr<MediaLog> media_log_;
  raw_ptr<CdmContext> cdm_context_;
  BufferingState buffering_state_;
  const TimeDelta audio_write_duration_local_;
  const TimeDelta audio_write_duration_remote_;
  const std::string max_video_capabilities_;
  const StarboardRendererConfig::ExperimentalFeatures experimental_features_;
  // TODO: b/375674101 - Connect this to h5vcc setting.
  const int max_samples_per_write_;
  const gfx::Size viewport_size_;
#if BUILDFLAG(IS_ANDROID)
  const AndroidOverlayMojoFactoryCB android_overlay_factory_cb_;
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROID)
  jobject surface_view_ = nullptr;
  std::unique_ptr<AndroidOverlay> overlay_;
#endif  // BUILDFLAG(IS_ANDROID)

  raw_ptr<DemuxerStream> audio_stream_ = nullptr;
  raw_ptr<DemuxerStream> video_stream_ = nullptr;
  // TODO(b/375274109): Investigate whether we should call
  //                    `void OnVideoFrameRateChange(std::optional<int> fps)`
  //                    on `client_`?
  raw_ptr<RendererClient> client_ = nullptr;
  PaintVideoHoleFrameCallback paint_video_hole_frame_cb_;
  UpdateStarboardRenderingModeCallback update_starboard_rendering_mode_cb_;
  GetSbWindowHandleCallback get_sb_window_handle_cb_;
#if BUILDFLAG(IS_IOS_TVOS)
  std::string source_url_;

  DurationChangeCB duration_change_cb_;
  BufferedRangesCB buffered_ranges_cb_;
  EncryptedMediaInitDataCB encrypted_media_init_data_cb_;

  // Cached values for change-detection; only notify upstream when they differ.
  TimeDelta last_buffer_start_;
  TimeDelta last_buffer_length_;
  TimeDelta last_duration_ = kNoTimestamp;
  gfx::Size url_player_video_size_;
#endif  // BUILDFLAG(IS_IOS_TVOS)
#if BUILDFLAG(IS_ANDROID)
  RequestOverlayInfoCallBack request_overlay_info_cb_;
#endif  // BUILDFLAG(IS_ANDROID)

  // The current overlay info, which possibly specifies an overlay to render to.
  OverlayInfo overlay_info_;

  std::optional<gfx::Rect> output_rect_;

  // Temporary callback used for Initialize().
  PipelineStatusCallback init_cb_;

  DefaultSbPlayerInterface sbplayer_interface_;

  TimeDelta seek_time_;

  // The two variables below should always contain the same value.  They are
  // kept as separate variables so we can keep the existing implementation as
  // is, which simplifies the implementation across multiple Starboard versions.
  TimeDelta audio_write_duration_;
  TimeDelta audio_write_duration_for_preroll_ = audio_write_duration_;
  // Only call GetMediaTime() from OnNeedData if it has been
  // |kMediaTimeCheckInterval| since the last call to GetMediaTime().
  static constexpr TimeDelta kMediaTimeCheckInterval = base::Milliseconds(100);
  // Timestamp for the last written audio.
  TimeDelta timestamp_of_last_written_audio_;
  // Indicates if video end of stream has been written into the underlying
  // player.
  bool is_video_eos_written_ = false;
  TimeDelta last_audio_sample_interval_ = base::Microseconds(0);
  int last_estimated_max_buffers_for_preroll_ = 1;
  // Last media time reported by GetMediaTime().
  TimeDelta last_media_time_;
  // Timestamp microseconds when we last checked the media time.
  Time last_time_media_time_retrieved_;

  bool audio_read_delayed_ = false;

  SbDrmSystem drm_system_{kSbDrmSystemInvalid};

  std::unique_ptr<SbPlayerBridge> player_bridge_;

  base::OnceClosure pending_flush_cb_;

  bool audio_read_in_progress_ = false;
  bool video_read_in_progress_ = false;

  double playback_rate_ = 0.;
  float volume_ = 1.0f;

  uint32_t last_video_frames_decoded_ = 0;
  uint32_t last_video_frames_dropped_ = 0;

  SbWindow sb_window_ = kSbWindowInvalid;

  // Call to get the SbDecodeTargetGraphicsContextProvider for SbPlayerCreate().
  GetDecodeTargetGraphicsContextProviderFunc
      get_decode_target_graphics_context_provider_func_;

  // TODO: b/508699637 - Check if HDR is supported.
  gfx::ColorSpace color_space_ = gfx::ColorSpace::CreateSRGB();

  // Message to signal a capability changed error.
  // "MEDIA_ERR_CAPABILITY_CHANGED" must be in the error message to be
  // understood as a capability changed error. Do not change this message.
  static inline constexpr const char* kSbPlayerCapabilityChangedErrorMessage =
      "MEDIA_ERR_CAPABILITY_CHANGED";

  // NOTE: Do not add member variables after weak_factory_
  // It should be the first one destroyed among all members.
  // See base/memory/weak_ptr.h.
  base::WeakPtrFactory<StarboardRenderer> weak_factory_{this};
};

}  // namespace media

#endif  // MEDIA_STARBOARD_STARBOARD_RENDERER_H_
