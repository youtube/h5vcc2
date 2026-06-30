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

#ifndef MEDIA_STARBOARD_URL_PLAYER_RENDERER_H_
#define MEDIA_STARBOARD_URL_PLAYER_RENDERER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "media/base/cdm_context.h"
#include "media/base/media_export.h"
#include "media/base/media_log.h"
#include "media/base/pipeline_status.h"
#include "media/base/renderer.h"
#include "media/base/renderer_client.h"
#include "media/base/starboard/starboard_rendering_mode.h"
#include "media/starboard/sbplayer_bridge.h"
#include "ui/gfx/geometry/rect.h"

namespace media {

// Renderer for URL-based playback on tvOS (e.g., HLS via AVPlayer).
// Delegates all media loading, demuxing, buffering, and decoding to the
// platform URL player through SbPlayerBridge. Streams from MediaResource
// are ignored; the URL is supplied by the wrapper before initialization.
//
// Lifetime and Ownership: Owned by UrlPlayerRendererWrapper in the
// GPU process.
//
// Threading Model: Thread-affine, must be used on the media task
// runner sequence.
class MEDIA_EXPORT UrlPlayerRenderer : public Renderer,
                                       private SbPlayerBridge::Host {
 public:
  UrlPlayerRenderer(const scoped_refptr<base::SequencedTaskRunner>& task_runner,
                    std::unique_ptr<MediaLog> media_log,
                    const base::UnguessableToken& overlay_plane_id,
                    const gfx::Size& viewport_size);

  UrlPlayerRenderer(const UrlPlayerRenderer&) = delete;
  UrlPlayerRenderer& operator=(const UrlPlayerRenderer&) = delete;

  ~UrlPlayerRenderer() override;

  // Renderer implementation.
  void Initialize(MediaResource* media_resource,
                  RendererClient* client,
                  PipelineStatusCallback init_cb) override;
  void SetCdm(CdmContext* cdm_context, CdmAttachedCB cdm_attached_cb) override;
  void SetLatencyHint(std::optional<base::TimeDelta> latency_hint) override;
  void Flush(base::OnceClosure flush_cb) override;
  void StartPlayingFrom(base::TimeDelta time) override;
  void SetPlaybackRate(double playback_rate) override;
  void SetVolume(float volume) override;
  base::TimeDelta GetMediaTime() override;
  void OnTracksChanged(DemuxerStream::Type track_type,
                       std::vector<DemuxerStream*> enabled_tracks,
                       base::OnceClosure change_completed_cb) override;
  RendererType GetRendererType() override { return RendererType::kUrlPlayer; }

  // URL setup — called by the wrapper before Initialize().
  void SetSourceUrl(const std::string& source_url);

  using PaintVideoHoleFrameCallback =
      base::RepeatingCallback<void(const gfx::Size&)>;
  using UpdateStarboardRenderingModeCallback =
      base::RepeatingCallback<void(const StarboardRenderingMode mode)>;
  using GetSbWindowHandleCallback = base::RepeatingCallback<void()>;

  void SetUrlPlayerRendererCallbacks(
      PaintVideoHoleFrameCallback paint_video_hole_frame_cb,
      UpdateStarboardRenderingModeCallback update_starboard_rendering_mode_cb,
      GetSbWindowHandleCallback get_sb_window_handle_cb);

  void OnVideoGeometryChange(const gfx::Rect& output_rect);
  void OnSbWindowHandleReady(uint64_t sb_window_handle);

  SbDecodeTarget GetSbDecodeTarget();

  SbPlayerInterface* GetSbPlayerInterface();
  void SetSbPlayerInterfaceForTesting(SbPlayerInterface* sbplayer_interface) {
    test_sbplayer_interface_ = sbplayer_interface;
  }

 private:
  enum State {
    STATE_UNINITIALIZED,
    STATE_INITIALIZING,
    STATE_FLUSHED,
    STATE_PLAYING,
    STATE_ERROR,
  };

  void CreatePlayerBridge();
  void ApplyPendingBounds();

  // SbPlayerBridge::Host implementation.
  void OnNeedData(DemuxerStream::Type type,
                  int max_number_of_buffers_to_write) override;
  void OnPlayerStatus(SbPlayerState state) override;
  void OnPlayerError(SbPlayerError error, const std::string& message) override;

  void TryReportVideoDimensions();
  void OnStatisticsUpdate(const PipelineStatistics& stats);
  void OnBufferingStateChange(BufferingState state);
  void NotifyError(PipelineStatus status);
  void OnEncryptedMediaInitDataEncountered(const char* init_data_type,
                                           const unsigned char* init_data,
                                           unsigned int init_data_length);

  State state_ = STATE_UNINITIALIZED;
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;
  const std::unique_ptr<MediaLog> media_log_;
  const gfx::Size viewport_size_;

  std::string source_url_;
  bool has_reported_dimensions_ = false;
  raw_ptr<CdmContext> cdm_context_ = nullptr;
  SbDrmSystem drm_system_{kSbDrmSystemInvalid};
  raw_ptr<RendererClient> client_ = nullptr;
  PipelineStatusCallback init_cb_;
  BufferingState buffering_state_ = BUFFERING_HAVE_NOTHING;

  PaintVideoHoleFrameCallback paint_video_hole_frame_cb_;
  UpdateStarboardRenderingModeCallback update_starboard_rendering_mode_cb_;
  GetSbWindowHandleCallback get_sb_window_handle_cb_;

  std::optional<gfx::Rect> output_rect_;
  double playback_rate_ = 0.0;
  float volume_ = 1.0f;

  SbWindow sb_window_ = kSbWindowInvalid;
  DefaultSbPlayerInterface sbplayer_interface_;
  raw_ptr<SbPlayerInterface> test_sbplayer_interface_ = nullptr;
  std::unique_ptr<SbPlayerBridge> player_bridge_;

  uint32_t last_video_frames_decoded_ = 0;
  uint32_t last_video_frames_dropped_ = 0;

  static inline constexpr const char* kSbPlayerCapabilityChangedErrorMessage =
      "MEDIA_ERR_CAPABILITY_CHANGED";

  base::WeakPtrFactory<UrlPlayerRenderer> weak_factory_{this};
};

}  // namespace media

#endif  // MEDIA_STARBOARD_URL_PLAYER_RENDERER_H_
