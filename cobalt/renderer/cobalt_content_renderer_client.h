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

#ifndef COBALT_RENDERER_COBALT_CONTENT_RENDERER_CLIENT_H_
#define COBALT_RENDERER_COBALT_CONTENT_RENDERER_CLIENT_H_

#include <atomic>
#include <memory>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/hang_watcher.h"
#include "cobalt/browser/mojom/h5vcc_settings.mojom.h"
#include "cobalt/common/cobalt_thread_checker.h"
#include "cobalt/media/audio/cobalt_audio_device_factory.h"
#include "cobalt/media/service/mojom/platform_window_provider.mojom.h"
#include "content/public/renderer/content_renderer_client.h"
#include "media/base/demuxer.h"
#include "media/base/key_systems_support_registration.h"
#include "media/base/starboard/renderer_factory_traits.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/gfx/geometry/size.h"

namespace content {
class RenderFrame;
}  // namespace content

namespace media {
class MediaLog;
class RendererFactory;
}  // namespace media

namespace cobalt {
// This class utilizes embedder API for participating in renderer logic.
// It allows Cobalt to customize content Renderer module.
class CobaltContentRendererClient : public content::ContentRendererClient {
 public:
  CobaltContentRendererClient();

  CobaltContentRendererClient(const CobaltContentRendererClient&) = delete;
  CobaltContentRendererClient& operator=(const CobaltContentRendererClient&) =
      delete;

  ~CobaltContentRendererClient() override;

  // ContentRendererClient implementation.
  void RenderFrameCreated(content::RenderFrame* render_frame) override;
  void RenderThreadStarted() override;
  virtual std::unique_ptr<::media::KeySystemSupportRegistration>
  GetSupportedKeySystems(content::RenderFrame* render_frame,
                         ::media::GetSupportedKeySystemsCB cb) override;
  bool IsDecoderSupportedAudioType(const ::media::AudioType& type) override;
  bool IsDecoderSupportedVideoType(const ::media::VideoType& type) override;
  // JS Injection hook
  void RunScriptsAtDocumentStart(content::RenderFrame* render_frame) override;
  void GetStarboardRendererFactoryTraits(
      ::media::RendererFactoryTraits* traits) override;
  void PostSandboxInitialized() override;
  std::unique_ptr<::media::Demuxer> OverrideDemuxerForUrl(
      content::RenderFrame* render_frame,
      const GURL& url,
      scoped_refptr<base::SequencedTaskRunner> task_runner) override;

  uint64_t GetSbWindowHandle() const { return sb_window_handle_; }

 private:
  void EnsureH5vccSettingsRemoteInitialized();
  void OnGetSbWindow(uint64_t handle);

  // Registers a custom content::AudioDeviceFactory
  ::media::CobaltAudioDeviceFactory cobalt_audio_device_factory_;

  std::unique_ptr<mojo::Remote<cobalt::mojom::H5vccSettings>,
                  base::OnTaskRunnerDeleter>
      h5vcc_settings_remote_;

  base::ScopedClosureRunner unregister_thread_closure;

  gfx::Size viewport_size_;

  std::atomic<uint64_t> sb_window_handle_ = 0;
  bool window_handle_requested_ = false;

  COBALT_THREAD_CHECKER(main_thread_checker_);

  // NOTE: Do not add member variables after weak_factory_
  // It should be the first one destroyed among all members.
  // See base/memory/weak_ptr.h.
  base::WeakPtrFactory<CobaltContentRendererClient> weak_factory_{this};
};

}  // namespace cobalt

#endif  // COBALT_RENDERER_COBALT_CONTENT_RENDERER_CLIENT_H_
