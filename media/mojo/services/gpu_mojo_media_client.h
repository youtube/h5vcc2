// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_SERVICES_GPU_MOJO_MEDIA_CLIENT_H_
#define MEDIA_MOJO_SERVICES_GPU_MOJO_MEDIA_CLIENT_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "gpu/config/gpu_driver_bug_workarounds.h"
#include "gpu/config/gpu_feature_info.h"
#include "gpu/config/gpu_info.h"
#include "gpu/config/gpu_preferences.h"
#include "gpu/ipc/service/command_buffer_stub.h"
#include "media/base/android_overlay_mojo_factory.h"
#include "media/base/supported_video_decoder_config.h"
#include "media/media_buildflags.h"
#include "media/mojo/services/mojo_media_client.h"

#if BUILDFLAG(USE_STARBOARD_MEDIA)
#include <string>

#include "media/base/starboard/starboard_renderer_config.h"
#include "media/gpu/starboard/starboard_gpu_factory.h"

namespace cobalt::media {
class VideoGeometrySetterService;
}
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)

namespace media {

class MediaGpuChannelManager;

using GetConfigCacheCB =
    base::RepeatingCallback<SupportedVideoDecoderConfigs()>;
using GetCommandBufferStubCB =
    base::RepeatingCallback<gpu::CommandBufferStub*()>;

// Encapsulate parameters to pass to platform-specific helpers.
struct VideoDecoderTraits {
  scoped_refptr<base::SequencedTaskRunner> task_runner;
  std::unique_ptr<MediaLog> media_log;
  RequestOverlayInfoCB request_overlay_info_cb;
  const raw_ptr<const gfx::ColorSpace> target_color_space;

  // Windows decoders need to ensure that the cache is populated.
  GetConfigCacheCB get_cached_configs_cb;

  // Android uses this twice.
  GetCommandBufferStubCB get_command_buffer_stub_cb;

  mojo::PendingRemote<mojom::VideoDecoder> oop_video_decoder;

  VideoDecoderTraits(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      std::unique_ptr<MediaLog> media_log,
      RequestOverlayInfoCB request_overlay_info_cb,
      const gfx::ColorSpace* target_color_space,
      GetConfigCacheCB get_cached_configs_cb,
      GetCommandBufferStubCB get_command_buffer_stub_cb,
      mojo::PendingRemote<mojom::VideoDecoder> oop_video_decoder);
  ~VideoDecoderTraits();
};

#if BUILDFLAG(USE_STARBOARD_MEDIA)
using GetStarboardCommandBufferStubCB = StarboardGpuFactory::GetStubCB;

// Encapsulate parameters to pass to StarboardRenderer.
struct StarboardRendererTraits {
  scoped_refptr<base::SequencedTaskRunner> task_runner;
  scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner;
  mojo::PendingRemote<mojom::MediaLog> media_log_remote;
  cobalt::media::VideoGeometrySetterService* video_geometry_setter_service;
  const base::UnguessableToken& overlay_plane_id;
  base::TimeDelta audio_write_duration_local;
  base::TimeDelta audio_write_duration_remote;
  const std::string& max_video_capabilities;
  const StarboardRendererConfig::ExperimentalFeatures experimental_features;
  const gfx::Size& viewport_size;
  mojo::PendingReceiver<mojom::StarboardRendererExtension>
        renderer_extension_receiver;
  mojo::PendingRemote<mojom::StarboardRendererClientExtension>
        client_extension_remote;

  // StarboardRenderer uses this to post tasks on gpu thread.
  GetStarboardCommandBufferStubCB get_starboard_command_buffer_stub_cb;
  
  // StarboardRenderer uses this to create an AndroidOverlay.
  AndroidOverlayMojoFactoryCB android_overlay_factory_cb;

  StarboardRendererTraits(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner,
      mojo::PendingRemote<mojom::MediaLog> media_log_remote,
      cobalt::media::VideoGeometrySetterService* video_geometry_setter_service,
      const base::UnguessableToken& overlay_plane_id,
      base::TimeDelta audio_write_duration_local,
      base::TimeDelta audio_write_duration_remote,
      const std::string& max_video_capabilities,
      const StarboardRendererConfig::ExperimentalFeatures& experimental_features,
      const gfx::Size& viewport_size,
      mojo::PendingReceiver<mojom::StarboardRendererExtension>
          renderer_extension_receiver,
      mojo::PendingRemote<mojom::StarboardRendererClientExtension>
          client_extension_remote,
      GetStarboardCommandBufferStubCB
          get_starboard_command_buffer_stub_cb);
  StarboardRendererTraits(StarboardRendererTraits&& that) = default;
  ~StarboardRendererTraits();
};
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)

struct MEDIA_MOJO_EXPORT GpuMojoMediaClientTraits {
  gpu::GpuPreferences gpu_preferences;
  gpu::GpuDriverBugWorkarounds gpu_workarounds;
  gpu::GpuFeatureInfo gpu_feature_info;
  gpu::GPUInfo gpu_info;
  scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner;

  // Only used on Android.
  AndroidOverlayMojoFactoryCB android_overlay_factory_cb;

  // |media_gpu_channel_manager| must only be used on |gpu_task_runner|, which
  // is expected to be the GPU main thread task runner.
  base::WeakPtr<MediaGpuChannelManager> media_gpu_channel_manager;

#if BUILDFLAG(USE_STARBOARD_MEDIA)
  cobalt::media::VideoGeometrySetterService* video_geometry_setter_service;
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)

  GpuMojoMediaClientTraits(
      const gpu::GpuPreferences& gpu_preferences,
      const gpu::GpuDriverBugWorkarounds& gpu_workarounds,
      const gpu::GpuFeatureInfo& gpu_feature_info,
      const gpu::GPUInfo& gpu_info,
      scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner,
      AndroidOverlayMojoFactoryCB android_overlay_factory_cb,
      base::WeakPtr<MediaGpuChannelManager> media_gpu_channel_manager
#if BUILDFLAG(USE_STARBOARD_MEDIA)
      ,
      cobalt::media::VideoGeometrySetterService* video_geometry_setter_service
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)
      );
 
  ~GpuMojoMediaClientTraits();
};

class MEDIA_MOJO_EXPORT GpuMojoMediaClient : public MojoMediaClient {
 public:
  // Creates a platform specific GpuMojoMediaClient. Must be called on
  // `traits.gpu_task_runner`.
  static std::unique_ptr<GpuMojoMediaClient> Create(
      GpuMojoMediaClientTraits& traits);
  ~GpuMojoMediaClient() override;

  GpuMojoMediaClient(const GpuMojoMediaClient&) = delete;
  GpuMojoMediaClient& operator=(const GpuMojoMediaClient&) = delete;

  const gpu::GPUInfo& gpu_info() const { return gpu_info_; }

  // MojoMediaClient implementation.
  SupportedAudioDecoderConfigs GetSupportedAudioDecoderConfigs() final;
  SupportedVideoDecoderConfigs GetSupportedVideoDecoderConfigs() final;
  VideoDecoderType GetDecoderImplementationType() final;

  std::unique_ptr<AudioDecoder> CreateAudioDecoder(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      std::unique_ptr<MediaLog> media_log) final;
  std::unique_ptr<AudioEncoder> CreateAudioEncoder(
      scoped_refptr<base::SequencedTaskRunner> task_runner) final;
#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
  void NotifyDecoderSupportKnown(
      mojo::PendingRemote<mojom::VideoDecoder> oop_video_decoder,
      base::OnceCallback<void(mojo::PendingRemote<mojom::VideoDecoder>)> cb)
      final;
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
  std::unique_ptr<VideoDecoder> CreateVideoDecoder(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      MediaLog* media_log,
      mojom::CommandBufferIdPtr command_buffer_id,
      RequestOverlayInfoCB request_overlay_info_cb,
      const gfx::ColorSpace& target_color_space,
      mojo::PendingRemote<mojom::VideoDecoder> oop_video_decoder) final;
#if BUILDFLAG(USE_STARBOARD_MEDIA)
  std::unique_ptr<Renderer> CreateStarboardRenderer(
      mojom::FrameInterfaceFactory* frame_interfaces,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      mojo::PendingRemote<mojom::MediaLog> media_log_remote,
      const StarboardRendererConfig& config,
      mojo::PendingReceiver<mojom::StarboardRendererExtension>
          renderer_extension_receiver,
      mojo::PendingRemote<mojom::StarboardRendererClientExtension>
          client_extension_remote) final;
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)
#if BUILDFLAG(USE_STARBOARD_URL_PLAYER)
  std::unique_ptr<Renderer> CreateUrlPlayerRenderer(
      mojom::FrameInterfaceFactory* frame_interfaces,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      mojo::PendingRemote<mojom::MediaLog> media_log_remote,
      const StarboardRendererConfig& config,
      mojo::PendingReceiver<mojom::StarboardRendererExtension>
          renderer_extension_receiver,
      mojo::PendingRemote<mojom::StarboardRendererClientExtension>
          client_extension_remote) final;
#endif  // BUILDFLAG(USE_STARBOARD_URL_PLAYER)
  std::unique_ptr<CdmFactory> CreateCdmFactory(
      mojom::FrameInterfaceFactory* interface_provider) final;

 protected:
  GpuMojoMediaClient(GpuMojoMediaClientTraits& traits);

  // Find platform specific implementations of these in
  // gpu_mojo_media_client_{platform}.cc
  // Creates a platform-specific media::VideoDecoder.
  //
  // Note: Implementations must not store a reference to |traits| because it
  // will not remain valid after this method returns.
  virtual std::unique_ptr<VideoDecoder> CreatePlatformVideoDecoder(
      VideoDecoderTraits& traits) = 0;

  // Queries the platform-specific VideoDecoder implementation for its
  // supported profiles.
  virtual std::optional<SupportedVideoDecoderConfigs>
  GetPlatformSupportedVideoDecoderConfigs() = 0;

  // Queries the platform decoder type.
  virtual VideoDecoderType GetPlatformDecoderImplementationType() = 0;

#if BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION)
  // Ensures that the platform video decoder supported configurations are known.
  // When they are, |cb| is called with a PendingRemote that corresponds to the
  // same connection as |oop_video_decoder| (which may be |oop_video_decoder|
  // itself). |oop_video_decoder| may be used internally to query the supported
  // configurations of an out-of-process video decoder.
  //
  // |cb| is called with |oop_video_decoder| before
  // NotifyPlatformDecoderSupport() returns if the supported configurations are
  // already known.
  //
  // This function is thread- and sequence-safe. |cb| is always called on the
  // same sequence as NotifyPlatformDecoderSupport().
  virtual void NotifyPlatformDecoderSupport(
      mojo::PendingRemote<mojom::VideoDecoder> oop_video_decoder,
      base::OnceCallback<void(mojo::PendingRemote<mojom::VideoDecoder>)>
          cb) = 0;
#endif

  // ------------------- [ Optional implementations below ] -------------------

  // Creates a platform-specific media::AudioDecoder. Most platforms don't do
  // anything here, but android, for example, does.
  virtual std::unique_ptr<AudioDecoder> CreatePlatformAudioDecoder(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      std::unique_ptr<MediaLog> media_log);

  // Creates a platform-specific media::AudioEncoder. Most platforms don't do
  // anything here.
  virtual std::unique_ptr<AudioEncoder> CreatePlatformAudioEncoder(
      scoped_refptr<base::SequencedTaskRunner> task_runner);
  
#if BUILDFLAG(USE_STARBOARD_MEDIA)
  // Creates a platform-specific media::StarboardRenderer.
  // This is used on Cobalt (android/linux).
  virtual std::unique_ptr<Renderer> CreatePlatformStarboardRenderer(
        StarboardRendererTraits traits);
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)

  // Creates a CDM factory, right now only used on android and chromeos.
  virtual std::unique_ptr<CdmFactory> CreatePlatformCdmFactory(
      mojom::FrameInterfaceFactory* frame_interfaces);

  // Queries the platform-specific AudioDecoder implementation for its
  // supported codecs.
  virtual std::optional<SupportedAudioDecoderConfigs>
  GetPlatformSupportedAudioDecoderConfigs();

#if BUILDFLAG(USE_STARBOARD_MEDIA)
  cobalt::media::VideoGeometrySetterService* video_geometry_setter_service_;
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)
  const gpu::GpuPreferences gpu_preferences_;
  const gpu::GpuDriverBugWorkarounds gpu_workarounds_;
  const gpu::GpuFeatureInfo gpu_feature_info_;
  const gpu::GPUInfo gpu_info_;
  const scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner_;

 private:
  // Note this should not be passed outside of this class since it must be
  // carefully used only on the GPU thread and not the media service thread.
  const base::WeakPtr<MediaGpuChannelManager> media_gpu_channel_manager_;

  // Cross-platform cache supported config cache.
  std::optional<SupportedVideoDecoderConfigs> supported_config_cache_;
};

}  // namespace media

#endif  // MEDIA_MOJO_SERVICES_GPU_MOJO_MEDIA_CLIENT_H_
