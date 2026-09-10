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

#include "media/mojo/services/starboard/starboard_renderer_wrapper.h"

#include <utility>

#include "base/compiler_specific.h"
#include "base/functional/callback_helpers.h"
#include "base/task/bind_post_task.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_resource.h"
#include "media/base/starboard/starboard_rendering_mode.h"
#include "media/mojo/common/starboard/mojo_renderer_bypass_bridge.h"
#include "media/mojo/services/mojo_media_log.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gl/gl_bindings.h"

namespace media {

namespace {
// Time interval to update media time when bypass is active. This matches
// `kTimeUpdateInterval` in media/mojo/services/mojo_renderer_service.cc.
constexpr auto kTimeUpdateInterval = base::Milliseconds(125);
}  // namespace

// A proxy DemuxerStream that forwards Read calls to the
// MojoRendererBypassBridge.
//
// Lifetime and Ownership: Owned by ProxyMediaResource and exists for the
// duration of the active media bypass session.
//
// Threading Model: This class is thread-affine and must be used on the Mojo
// service thread where StarboardRenderer and StarboardRendererWrapper run.
class ProxyDemuxerStream : public DemuxerStream {
 public:
  ProxyDemuxerStream(scoped_refptr<MojoRendererBypassBridge> bridge,
                     DemuxerStream::Type type)
      : bridge_(bridge), type_(type) {}

  void Read(uint32_t count, ReadCB read_cb) override {
    bridge_->Read(type_, count, std::move(read_cb));
  }

  AudioDecoderConfig audio_decoder_config() override {
    return bridge_->GetAudioConfig();
  }

  VideoDecoderConfig video_decoder_config() override {
    return bridge_->GetVideoConfig();
  }

  Type type() const override { return type_; }

  StreamLiveness liveness() const override {
    return bridge_->GetLiveness(type_);
  }

  bool SupportsConfigChanges() override {
    return bridge_->SupportsConfigChanges(type_);
  }

  void EnableBitstreamConverter() override {
    bridge_->EnableBitstreamConverter(type_);
  }

 private:
  scoped_refptr<MojoRendererBypassBridge> bridge_;
  Type type_;
};

// A MediaResource implementation that holds ProxyDemuxerStreams.
//
// Lifetime and Ownership: Owned by StarboardRendererWrapper and exists for the
// duration of the active media bypass session.
//
// Threading Model: This class is thread-affine and must be used on the Mojo
// service thread where StarboardRenderer and StarboardRendererWrapper run.
class ProxyMediaResource : public MediaResource {
 public:
  ProxyMediaResource(std::unique_ptr<ProxyDemuxerStream> audio,
                     std::unique_ptr<ProxyDemuxerStream> video)
      : audio_(std::move(audio)), video_(std::move(video)) {}

  std::vector<DemuxerStream*> GetAllStreams() override {
    std::vector<DemuxerStream*> streams;
    if (audio_) {
      streams.push_back(audio_.get());
    }
    if (video_) {
      streams.push_back(video_.get());
    }
    return streams;
  }

 private:
  std::unique_ptr<ProxyDemuxerStream> audio_;
  std::unique_ptr<ProxyDemuxerStream> video_;
};

// A RendererClient implementation that intercepts statistics updates to post
// them directly to the bypass bridge.
//
// Lifetime and Ownership: Owned by StarboardRendererWrapper and exists for the
// duration of the active media bypass session.
//
// Threading Model: This class is thread-affine and must be used on the Mojo
// service thread (the same thread where StarboardRendererWrapper is created and
// used).
class ProxyRendererClient final : public RendererClient {
 public:
  ProxyRendererClient(RendererClient* client,
                      scoped_refptr<MojoRendererBypassBridge> bridge)
      : client_(client), bridge_(bridge) {
    DCHECK(client);
  }
  ~ProxyRendererClient() = default;

  void OnError(PipelineStatus status) override { client_->OnError(status); }
  void OnFallback(PipelineStatus fallback) override {
    client_->OnFallback(fallback);
  }
  void OnEnded() override { client_->OnEnded(); }
  void OnStatisticsUpdate(const PipelineStatistics& stats) override {
    if (bridge_) {
      bridge_->PostStatisticsUpdate(stats);
    } else {
      client_->OnStatisticsUpdate(stats);
    }
  }
  void OnBufferingStateChange(BufferingState state,
                              BufferingStateChangeReason reason) override {
    client_->OnBufferingStateChange(state, reason);
  }
  void OnWaiting(WaitingReason reason) override { client_->OnWaiting(reason); }
  void OnAudioConfigChange(const AudioDecoderConfig& config) override {
    client_->OnAudioConfigChange(config);
  }
  void OnVideoConfigChange(const VideoDecoderConfig& config) override {
    client_->OnVideoConfigChange(config);
  }
  void OnVideoNaturalSizeChange(const gfx::Size& size) override {
    client_->OnVideoNaturalSizeChange(size);
  }
  void OnVideoOpacityChange(bool opaque) override {
    client_->OnVideoOpacityChange(opaque);
  }
  void OnVideoFrameRateChange(std::optional<int> fps) override {
    client_->OnVideoFrameRateChange(fps);
  }

 private:
  raw_ptr<RendererClient> client_;
  scoped_refptr<MojoRendererBypassBridge> bridge_;
};

StarboardRendererWrapper::StarboardRendererWrapper(
    StarboardRendererTraits traits
#if BUILDFLAG(IS_ANDROID)
    ,
    scoped_refptr<gpu::RefCountedLock> drdc_lock)
    : gpu::RefCountedLockHelperDrDc(std::move(drdc_lock)),
#else   // BUILDFLAG(IS_ANDROID)
    )
    :
#endif  // BUILDFLAG(IS_ANDROID)
      video_geometry_setter_service_(traits.video_geometry_setter_service),
      overlay_plane_id_(traits.overlay_plane_id),
      renderer_(
          traits.task_runner,
          std::make_unique<MojoMediaLog>(std::move(traits.media_log_remote),
                                         traits.task_runner),
          traits.overlay_plane_id,
          traits.audio_write_duration_local,
          traits.audio_write_duration_remote,
          traits.max_video_capabilities,
          traits.experimental_features,
          traits.viewport_size
#if BUILDFLAG(IS_ANDROID)
          ,
          std::move(traits.android_overlay_factory_cb)
#endif  // BUILDFLAG(IS_ANDROID)
              ),
      renderer_extension_receiver_(
          this,
          std::move(traits.renderer_extension_receiver)),
      client_extension_remote_(std::move(traits.client_extension_remote),
                               traits.task_runner) {
  DETACH_FROM_THREAD(thread_checker_);
  base::SequenceBound<StarboardGpuFactoryImpl> gpu_factory_impl(
      traits.gpu_task_runner,
      std::move(traits.get_starboard_command_buffer_stub_cb));
  gpu_factory_ = std::move(gpu_factory_impl);
  gpu_task_runner_ = std::move(traits.gpu_task_runner);
}

StarboardRendererWrapper::~StarboardRendererWrapper() = default;

void StarboardRendererWrapper::Initialize(MediaResource* media_resource,
                                          RendererClient* client,
                                          PipelineStatusCallback init_cb) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(init_cb);

  DCHECK(video_geometry_setter_service_);
  video_geometry_setter_service_->GetVideoGeometryChangeSubscriber(
      video_geometry_change_subcriber_remote_.BindNewPipeAndPassReceiver());
  DCHECK(video_geometry_change_subcriber_remote_);
  video_geometry_change_subcriber_remote_->SubscribeToVideoGeometryChange(
      overlay_plane_id_,
      video_geometry_change_client_receiver_.BindNewPipeAndPassRemote(),
      base::BindOnce(
          &StarboardRendererWrapper::OnSubscribeToVideoGeometryChange,
          base::Unretained(this), media_resource, client));

  GetRenderer()->SetStarboardRendererCallbacks(
      base::BindRepeating(
          &StarboardRendererWrapper::OnPaintVideoHoleFrameByStarboard,
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          &StarboardRendererWrapper::OnUpdateStarboardRenderingModeByStarboard,
          weak_factory_.GetWeakPtr()),
#if BUILDFLAG(IS_STARBOARD)
      base::BindRepeating(&StarboardRendererWrapper::OnGetSbWindowHandle,
                          weak_factory_.GetWeakPtr())
#else   // BUILDFLAG(IS_STARBOARD)
      base::NullCallback()
#endif  // BUILDFLAG(IS_STARBOARD)
#if BUILDFLAG(IS_ANDROID)
          ,
      base::BindRepeating(
          &StarboardRendererWrapper::OnRequestOverlayInfoByStarboard,
          weak_factory_.GetWeakPtr())
#endif  // BUILDFLAG(IS_ANDROID)
  );

#if BUILDFLAG(IS_IOS_TVOS)
  // Wire duration and buffered ranges callbacks.
  GetRenderer()->SetDurationChangeCB(base::BindRepeating(
      &StarboardRendererWrapper::OnDurationChange, weak_factory_.GetWeakPtr()));
  GetRenderer()->SetBufferedRangesCB(
      base::BindRepeating(&StarboardRendererWrapper::OnBufferedTimeRangesChange,
                          weak_factory_.GetWeakPtr()));
  GetRenderer()->SetEncryptedMediaInitDataCB(
      base::BindRepeating(&StarboardRendererWrapper::OnEncryptedMediaInitData,
                          weak_factory_.GetWeakPtr()));
#endif  // BUILDFLAG(IS_IOS_TVOS)

  base::ScopedClosureRunner scoped_init_cb(
      base::BindOnce(&StarboardRendererWrapper::ContinueInitialization,
                     weak_factory_.GetWeakPtr(), std::move(media_resource),
                     std::move(client), std::move(init_cb)));

  // OnGpuChannelTokenReady() is called before Initialize()
  // in StarboardRendererClient if GpuVideoAcceleratorFactories is available.
  // In this case, it is safe to access |command_buffer_id_| for posting gpu
  // tasks.
  if (IsGpuChannelTokenAvailable()) {
    // Create StarboardGpuFactory using |command_buffer_id_|.
    base::OnceClosure init_callback = scoped_init_cb.Release();
    GetGpuFactory()
        ->AsyncCall(&StarboardGpuFactory::Initialize)
        .WithArgs(command_buffer_id_->channel_token,
                  command_buffer_id_->route_id,
                  base::BindPostTaskToCurrentDefault(std::move(init_callback)));
    return;
  }
}

void StarboardRendererWrapper::Flush(base::OnceClosure flush_cb) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  StopMediaTimePolling();
  GetRenderer()->Flush(std::move(flush_cb));
}

void StarboardRendererWrapper::StartPlayingFrom(base::TimeDelta time) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->StartPlayingFrom(time);
  StartMediaTimePollingIfNeeded();
}

void StarboardRendererWrapper::SetPlaybackRate(double playback_rate) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  playback_rate_ = playback_rate;
  GetRenderer()->SetPlaybackRate(playback_rate);
  if (playback_rate_ > 0) {
    StartMediaTimePollingIfNeeded();
  } else {
    StopMediaTimePolling();
  }
}

void StarboardRendererWrapper::SetVolume(float volume) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->SetVolume(volume);
}

void StarboardRendererWrapper::SetCdm(CdmContext* cdm_context,
                                      CdmAttachedCB cdm_attached_cb) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->SetCdm(cdm_context, std::move(cdm_attached_cb));
}

void StarboardRendererWrapper::SetLatencyHint(
    std::optional<base::TimeDelta> latency_hint) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->SetLatencyHint(latency_hint);
}

base::TimeDelta StarboardRendererWrapper::GetMediaTime() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return GetRenderer()->GetMediaTime();
}

RendererType StarboardRendererWrapper::GetRendererType() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return RendererType::kStarboard;
}

void StarboardRendererWrapper::OnGpuChannelTokenReady(
    mojom::CommandBufferIdPtr command_buffer_id) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  command_buffer_id_ = std::move(command_buffer_id);

  // Clear cached shared image and texture service IDs from previous context.
  current_shared_image_ = nullptr;
  last_texture_service_ids_.clear();

  // Forward token updates (e.g. after sleep/wake context loss) to update
  // |stub_| on the GPU thread.
  base::UnguessableToken channel_token;
  int32_t route_id = 0;
  if (command_buffer_id_) {
    channel_token = command_buffer_id_->channel_token;
    route_id = command_buffer_id_->route_id;
  }

  GetGpuFactory()
      ->AsyncCall(&StarboardGpuFactory::Initialize)
      .WithArgs(std::move(channel_token), route_id, base::NullCallback());
}

void StarboardRendererWrapper::GetCurrentVideoFrame(
    GetCurrentVideoFrameCallback callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  {
    // Post GetRenderer()->GetSbDecodeTarget() on the gpu thread.
    base::OnceCallback<void()> get_current_decode_target_cb =
        base::BindOnce(&StarboardRendererWrapper::GetCurrentDecodeTarget,
                       base::Unretained(this));
    base::WaitableEvent done_event(
        base::WaitableEvent::ResetPolicy::MANUAL,
        base::WaitableEvent::InitialState::NOT_SIGNALED);
    GetGpuFactory()
        ->AsyncCall(&StarboardGpuFactory::RunCallbackOnGpu)
        .WithArgs(std::move(get_current_decode_target_cb), &done_event);
    // This call blocks because the underlying Starboard API
    // (SbPlayerGetCurrentFrame) is synchronous and needs to be executed on the
    // GPU thread.
    base::ScopedAllowBaseSyncPrimitivesOutsideBlockingScope allow_wait;
    done_event.Wait();
  }
  if (SbDecodeTargetIsValid(decode_target_)) {
    auto info = std::make_unique<SbDecodeTargetInfo>();
    *info = {};
    if (!SbDecodeTargetGetInfo(decode_target_, info.get())) {
      LOG(ERROR) << "SbDecodeTargetGetInfo failed";
      GetGpuFactory()
          ->AsyncCall(&StarboardGpuFactory::PostCallbackToGpu)
          .WithArgs(base::BindOnce(
              [](void* target) {
                SbDecodeTarget decode_target =
                    reinterpret_cast<SbDecodeTarget>(target);
                if (SbDecodeTargetIsValid(decode_target)) {
                  SbDecodeTargetRelease(decode_target);
                }
              },
              reinterpret_cast<void*>(decode_target_)));
      decode_target_ = kSbDecodeTargetInvalid;
      std::move(callback).Run(nullptr);
      return;
    }

    VideoPixelFormat format;
    viz::SharedImageFormat viz_format;
    std::vector<uint32_t> texture_service_ids;
    std::vector<uint32_t> texture_targets;
    scoped_refptr<gpu::ClientSharedImage> shared_image;
    int plane_count = SbDecodeTargetNumberOfPlanesForFormat(info.get()->format);
    DCHECK_GE(plane_count, 1);
    const SbDecodeTargetInfoPlane& plane = info.get()->planes[0];
    auto coded_size = gfx::Size(info.get()->width, info.get()->height);
    auto visible_rect = gfx::Rect(
        gfx::Point(
            static_cast<int>(std::round(std::min(plane.content_region.left,
                                                 plane.content_region.right))),
            static_cast<int>(std::round(std::min(
                plane.content_region.top, plane.content_region.bottom)))),
        gfx::Size(
            static_cast<int>(std::round(std::abs(plane.content_region.right -
                                                 plane.content_region.left))),
            static_cast<int>(std::round(std::abs(
                plane.content_region.top - plane.content_region.bottom)))));
    auto natural_size = visible_rect.size();

    if (info.get()->format == kSbDecodeTargetFormat1PlaneRGBA) {
      DCHECK_EQ(static_cast<size_t>(plane_count), 1u);
      format = PIXEL_FORMAT_ABGR;
      viz_format = viz::SinglePlaneFormat::kRGBA_8888;
    } else if (info.get()->format == kSbDecodeTargetFormat3PlaneYUVI420) {
      DCHECK_EQ(static_cast<size_t>(plane_count), 3u);
      format = PIXEL_FORMAT_I420;
      viz_format = viz::MultiPlaneFormat::kI420;
    } else {
      LOG(ERROR) << "Unsupported SbDecodeTargetFormat: "
                 << static_cast<int>(info.get()->format);
      GetGpuFactory()
          ->AsyncCall(&StarboardGpuFactory::PostCallbackToGpu)
          .WithArgs(base::BindOnce(
              [](void* target) {
                SbDecodeTarget decode_target =
                    reinterpret_cast<SbDecodeTarget>(target);
                if (SbDecodeTargetIsValid(decode_target)) {
                  SbDecodeTargetRelease(decode_target);
                }
              },
              reinterpret_cast<void*>(decode_target_)));
      decode_target_ = kSbDecodeTargetInvalid;
      std::move(callback).Run(nullptr);
      return;
    }

    for (int plane_index = 0; plane_index < plane_count; plane_index++) {
      texture_service_ids.push_back(info.get()->planes[plane_index].texture);
      texture_targets.push_back(
          info.get()->planes[plane_index].gl_texture_target);
    }
    DCHECK_EQ(texture_service_ids.size(), static_cast<size_t>(plane_count));
    DCHECK_EQ(texture_targets.size(), static_cast<size_t>(plane_count));

    if (current_shared_image_ &&
        texture_service_ids == last_texture_service_ids_) {
      shared_image = current_shared_image_;
      GetGpuFactory()
          ->AsyncCall(&StarboardGpuFactory::PostCallbackToGpu)
          .WithArgs(base::BindOnce(
              [](void* target) {
                SbDecodeTarget decode_target =
                    reinterpret_cast<SbDecodeTarget>(target);
                if (SbDecodeTargetIsValid(decode_target)) {
                  SbDecodeTargetRelease(decode_target);
                }
              },
              reinterpret_cast<void*>(decode_target_)));
      decode_target_ = kSbDecodeTargetInvalid;
    } else {
      base::WaitableEvent done_event(
          base::WaitableEvent::ResetPolicy::MANUAL,
          base::WaitableEvent::InitialState::NOT_SIGNALED);
      GetGpuFactory()
          ->AsyncCall(&StarboardGpuFactory::CreateImageOnGpu)
          .WithArgs(coded_size, GetRenderer()->color_space(), viz_format,
                    std::ref(shared_image), std::ref(texture_service_ids),
                    std::ref(texture_targets),
                    reinterpret_cast<uint64_t>(decode_target_),
#if BUILDFLAG(IS_ANDROID)
                    GetDrDcLock(),
#endif  // BUILDFLAG(IS_ANDROID)
                    &done_event);
      // Blocking is okay here to create image from textures on gpu thread.
      base::ScopedAllowBaseSyncPrimitivesOutsideBlockingScope allow_wait;
      done_event.Wait();
      if (shared_image) {
        current_shared_image_ = shared_image;
        last_texture_service_ids_ = texture_service_ids;
        decode_target_ = kSbDecodeTargetInvalid;
      }
    }

    if (shared_image) {
      CreateVideoFrame_OnImageReady(format, coded_size, visible_rect,
                                    natural_size, std::move(shared_image));
    }
  }
  std::move(callback).Run(current_frame_);
}

void StarboardRendererWrapper::OnSbWindowHandleReady(
    const uint64_t sb_window_handle) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->OnSbWindowHandleReady(sb_window_handle);
}

void StarboardRendererWrapper::InitializeWithBypassBridge(
    uint32_t bypass_bridge_id,
    InitializeWithBypassBridgeCallback callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  bypass_bridge_ = BypassBridgeRegistry::Get(bypass_bridge_id);
  if (!bypass_bridge_) {
    LOG(WARNING) << __func__ << ": Bypass bridge not found for ID "
                 << bypass_bridge_id;
    std::move(callback).Run(false);
    return;
  }

  std::unique_ptr<ProxyDemuxerStream> audio_proxy;
  std::unique_ptr<ProxyDemuxerStream> video_proxy;

  AudioDecoderConfig audio_config = bypass_bridge_->GetAudioConfig();
  if (audio_config.IsValidConfig()) {
    audio_proxy = std::make_unique<ProxyDemuxerStream>(bypass_bridge_,
                                                       DemuxerStream::AUDIO);
  }
  VideoDecoderConfig video_config = bypass_bridge_->GetVideoConfig();
  if (video_config.IsValidConfig()) {
    video_proxy = std::make_unique<ProxyDemuxerStream>(bypass_bridge_,
                                                       DemuxerStream::VIDEO);
  }

  if (!audio_proxy && !video_proxy) {
    LOG(ERROR) << __func__
               << ": No valid audio or video config found in bypass bridge.";
    bypass_bridge_ = nullptr;
    std::move(callback).Run(false);
    return;
  }

  proxy_media_resource_ = std::make_unique<ProxyMediaResource>(
      std::move(audio_proxy), std::move(video_proxy));
  std::move(callback).Run(true);
}

#if BUILDFLAG(IS_IOS_TVOS)
void StarboardRendererWrapper::SetSourceUrl(const std::string& source_url) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->SetSourceUrl(source_url);
}
#endif  // BUILDFLAG(IS_IOS_TVOS)

#if BUILDFLAG(IS_ANDROID)
void StarboardRendererWrapper::OnOverlayInfoChanged(
    const OverlayInfo& overlay_info) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetRenderer()->OnOverlayInfoChanged(overlay_info);
}
#endif  // BUILDFLAG(IS_ANDROID)

StarboardRenderer* StarboardRendererWrapper::GetRenderer() {
  if (test_renderer_) {
    return test_renderer_;
  }
  return &renderer_;
}

base::SequenceBound<StarboardGpuFactory>*
StarboardRendererWrapper::GetGpuFactory() {
  if (test_gpu_factory_) {
    return test_gpu_factory_;
  }
  return &gpu_factory_;
}

void StarboardRendererWrapper::OnVideoGeometryChange(
    const gfx::RectF& rect_f,
    gfx::OverlayTransform /* transform */) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // Use gfx::ToEnclosedRect() to align with `NotifyOverlayPromotion()`
  // in //components/viz/service/display/overlay_processor_android.cc.
  gfx::Rect new_bounds = gfx::ToEnclosedRect(rect_f);
  GetRenderer()->OnVideoGeometryChange(new_bounds);
}

void StarboardRendererWrapper::ContinueInitialization(
    MediaResource* media_resource,
    RendererClient* client,
    PipelineStatusCallback init_cb) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(init_cb);
  is_gpu_factory_initialized_ = true;
  decode_target_graphics_context_provider_.gles_context_runner_context = this;
  decode_target_graphics_context_provider_.gles_context_runner =
      &StarboardRendererWrapper::GraphicsContextRunner;
  GetRenderer()->set_decode_target_graphics_context_provider(
      base::BindRepeating(
          &StarboardRendererWrapper::GetSbDecodeTargetGraphicsContextProvider,
          base::Unretained(this)));

  if (bypass_bridge_) {
    proxy_renderer_client_ =
        std::make_unique<ProxyRendererClient>(client, bypass_bridge_);
    client = proxy_renderer_client_.get();
  }

  GetRenderer()->Initialize(
      proxy_media_resource_ ? proxy_media_resource_.get() : media_resource,
      client, std::move(init_cb));
}

void StarboardRendererWrapper::PollMediaTime() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!bypass_bridge_) {
    return;
  }
  base::TimeDelta media_time = GetRenderer()->GetMediaTime();
  base::TimeDelta max_time = media_time + 2 * kTimeUpdateInterval;
  bypass_bridge_->PostTimeUpdate(media_time, max_time, base::TimeTicks::Now());
}

void StarboardRendererWrapper::StartMediaTimePollingIfNeeded() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!bypass_bridge_) {
    return;
  }
  if (playback_rate_ > 0 && !time_update_timer_.IsRunning()) {
    time_update_timer_.Start(FROM_HERE, kTimeUpdateInterval, this,
                             &StarboardRendererWrapper::PollMediaTime);
  }
}

void StarboardRendererWrapper::StopMediaTimePolling() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (time_update_timer_.IsRunning()) {
    time_update_timer_.Stop();
  }
}

void StarboardRendererWrapper::OnPaintVideoHoleFrameByStarboard(
    const gfx::Size& size) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->PaintVideoHoleFrame(size);
}

void StarboardRendererWrapper::OnUpdateStarboardRenderingModeByStarboard(
    const StarboardRenderingMode mode) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->UpdateStarboardRenderingMode(mode);
}

void StarboardRendererWrapper::OnGetSbWindowHandle() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->GetSbWindowHandle();
}

#if BUILDFLAG(IS_IOS_TVOS)
void StarboardRendererWrapper::OnEncryptedMediaInitData(
    const std::string& init_data_type,
    const std::vector<uint8_t>& init_data) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->OnEncryptedMediaInitDataEncountered(init_data_type,
                                                                init_data);
}

void StarboardRendererWrapper::OnDurationChange(base::TimeDelta duration) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->OnDurationChange(duration);
}

void StarboardRendererWrapper::OnBufferedTimeRangesChange(
    base::TimeDelta start,
    base::TimeDelta length) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->OnBufferedTimeRangesChange(start, length);
}
#endif  // BUILDFLAG(IS_IOS_TVOS)

void StarboardRendererWrapper::OnSubscribeToVideoGeometryChange(
    MediaResource* /* media_resource */,
    RendererClient* /* client */) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

#if BUILDFLAG(IS_ANDROID)
void StarboardRendererWrapper::OnRequestOverlayInfoByStarboard(
    bool restart_for_transitions) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  client_extension_remote_->RequestOverlayInfo(restart_for_transitions);
}
#endif  // BUILDFLAG(IS_ANDROID)

SbDecodeTargetGraphicsContextProvider*
StarboardRendererWrapper::GetSbDecodeTargetGraphicsContextProvider() {
  return &decode_target_graphics_context_provider_;
}

void StarboardRendererWrapper::GetCurrentDecodeTarget() {
  decode_target_ = GetRenderer()->GetSbDecodeTarget();
}

void StarboardRendererWrapper::CreateVideoFrame_OnImageReady(
    VideoPixelFormat format,
    const gfx::Size& coded_size,
    const gfx::Rect& visible_rect,
    const gfx::Size& natural_size,
    scoped_refptr<gpu::ClientSharedImage> shared_image) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  auto release_cb = base::BindOnce(
      [](scoped_refptr<gpu::ClientSharedImage> shared_image,
         const gpu::SyncToken& sync_token) {
        if (sync_token.HasData()) {
          shared_image->BackingWasExternallyUpdated(sync_token);
        }
      },
      shared_image);

  auto frame = VideoFrame::WrapSharedImage(
      format, std::move(shared_image), gpu::SyncToken(), std::move(release_cb),
      coded_size, visible_rect, natural_size, base::TimeDelta());
  if (!frame) {
    LOG(ERROR) << __func__ << " failed to create video frame";
    return;
  }
  current_frame_ = std::move(frame);
}

// static
// Disable CFI checks for this method because it executes function pointers
// provided by the Starboard library, which cannot be verified across the
// DSO boundary.
NO_SANITIZE("cfi-icall")
void StarboardRendererWrapper::GraphicsContextRunner(
    SbDecodeTargetGraphicsContextProvider* graphics_context_provider,
    SbDecodeTargetGlesContextRunnerTarget target_function,
    void* target_function_context) {
  auto provider = reinterpret_cast<StarboardRendererWrapper*>(
      graphics_context_provider->gles_context_runner_context);
  if (!provider || !provider->is_gpu_factory_initialized_) {
    return;
  }
  if (provider->gpu_task_runner_->RunsTasksInCurrentSequence()) {
    // If it is on the gpu thread, post target_function() directly on it.
    target_function(target_function_context);
  } else if (provider->gpu_factory_) {
    // If it is not on the gpu thread, post target_function() with
    // |gpu_factory_|.
    base::WaitableEvent done_event(
        base::WaitableEvent::ResetPolicy::MANUAL,
        base::WaitableEvent::InitialState::NOT_SIGNALED);
    provider->gpu_factory_
        .AsyncCall(&StarboardGpuFactory::RunSbDecodeTargetFunctionOnGpu)
        .WithArgs(target_function, target_function_context, &done_event);
    // Blocking is okay here to allow SbPlayer to post |target_function|
    // on gpu thread, and StarboardRenderer waits for the execution.
    base::ScopedAllowBaseSyncPrimitives allow_wait;
    done_event.Wait();
  }
}

}  // namespace media
