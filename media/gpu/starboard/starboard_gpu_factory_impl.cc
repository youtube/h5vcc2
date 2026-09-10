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

#include "media/gpu/starboard/starboard_gpu_factory_impl.h"

#include "base/memory/scoped_refptr.h"
#include "gpu/command_buffer/service/shared_image/shared_image_factory.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager.h"
#include "gpu/ipc/service/shared_image_stub.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/scoped_binders.h"
#include "ui/gl/scoped_make_current.h"
#include "ui/gl/scoped_restore_texture.h"

namespace media {
namespace {
bool MakeContextCurrent(gpu::CommandBufferStub* stub) {
  return stub && stub->decoder_context() &&
         stub->decoder_context()->MakeCurrent();
}
}  // namespace

StarboardGpuFactoryImpl::StarboardGpuFactoryImpl(GetStubCB get_stub_cb)
    : get_stub_cb_(std::move(get_stub_cb)) {
  DETACH_FROM_THREAD(thread_checker_);
}

StarboardGpuFactoryImpl::~StarboardGpuFactoryImpl() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (stub_) {
    stub_->RemoveDestructionObserver(this);
  }
}

void StarboardGpuFactoryImpl::Initialize(base::UnguessableToken channel_token,
                                         int32_t route_id,
                                         base::OnceClosure callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  stub_ = get_stub_cb_.Run(channel_token, route_id);
  if (stub_) {
    stub_->AddDestructionObserver(this);
  }
  std::move(callback).Run();
}

void StarboardGpuFactoryImpl::RunSbDecodeTargetFunctionOnGpu(
    SbDecodeTargetGlesContextRunnerTarget target_function,
    void* target_function_context,
    base::WaitableEvent* done_event) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // Punch-Out playback mode decodes directly to hardware overlays rather than GL textures,
  // so no CommandBufferStub exists (stub_ == nullptr). Standalone target functions (such as
  // ClearNativeWindow) manage their own EGL context and do not require a CommandBufferStub.
  const bool is_punch_out_playback = (stub_ == nullptr);

  if (is_punch_out_playback || MakeContextCurrent(stub_)) {
    target_function(target_function_context);
  }
  done_event->Signal();
}

void StarboardGpuFactoryImpl::RunCallbackOnGpu(
    base::OnceCallback<void()> callback,
    base::WaitableEvent* done_event) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (MakeContextCurrent(stub_)) {
    std::move(callback).Run();
  }
  done_event->Signal();
}

void StarboardGpuFactoryImpl::PostCallbackToGpu(
    base::OnceCallback<void()> callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!MakeContextCurrent(stub_)) {
    return;
  }
  std::move(callback).Run();
}

void StarboardGpuFactoryImpl::CreateImageOnGpu(
    const gfx::Size& coded_size,
    const gfx::ColorSpace& color_space,
    viz::SharedImageFormat format,
    scoped_refptr<gpu::ClientSharedImage>& shared_image,
    const std::vector<uint32_t>& texture_service_ids,
    const std::vector<uint32_t>& texture_targets,
    uint64_t decode_target,
#if BUILDFLAG(IS_ANDROID)
    scoped_refptr<gpu::RefCountedLock> drdc_lock,
#endif  // BUILDFLAG(IS_ANDROID)
    base::WaitableEvent* done_event) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (MakeContextCurrent(stub_)) {
    DCHECK_EQ(texture_service_ids.size(), texture_targets.size());

    scoped_refptr<gpu::GpuChannelSharedImageInterface>
        gpu_channel_shared_image_interface =
            stub_->channel()->shared_image_stub()->shared_image_interface();

    shared_image = gpu_channel_shared_image_interface
                       ->CreateSharedImageForStarboardGLTexture(
                           format, coded_size, color_space, texture_service_ids,
                           texture_targets, decode_target
#if BUILDFLAG(IS_ANDROID)
                           ,
                           std::move(drdc_lock)
#endif
                       );
  }
  done_event->Signal();
}

void StarboardGpuFactoryImpl::OnWillDestroyStub(bool /*have_context*/) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(stub_);
  stub_ = nullptr;
}

}  // namespace media
