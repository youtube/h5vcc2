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

#ifndef MEDIA_GPU_STARBOARD_STARBOARD_GPU_FACTORY_H_
#define MEDIA_GPU_STARBOARD_STARBOARD_GPU_FACTORY_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/synchronization/waitable_event.h"
#include "base/unguessable_token.h"
#include "gpu/ipc/service/command_buffer_stub.h"
#include "gpu/ipc/service/gpu_channel_shared_image_interface.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/geometry/size.h"

#if BUILDFLAG(IS_ANDROID)
#include "gpu/command_buffer/service/ref_counted_lock.h"
#endif  // BUILDFLAG(IS_ANDROID)

namespace media {
// StarboardGpuFactory allows to post tasks on gpu thread.
// StarboardRenderer uses this class to post graphical tasks.
class StarboardGpuFactory : public gpu::CommandBufferStub::DestructionObserver {
 public:
  using GetStubCB =
      base::RepeatingCallback<gpu::CommandBufferStub*(base::UnguessableToken,
                                                      int32_t)>;
  StarboardGpuFactory();

  StarboardGpuFactory(const StarboardGpuFactory&) = delete;
  StarboardGpuFactory& operator=(const StarboardGpuFactory&) = delete;

  virtual ~StarboardGpuFactory();

  virtual void Initialize(base::UnguessableToken channel_token,
                          int32_t route_id,
                          base::OnceClosure callback) = 0;
  virtual void RunWithGlesContext(base::OnceClosure callback,
                                  base::WaitableEvent* done_event) = 0;
  virtual void CreateImageOnGpu(
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
      base::WaitableEvent* done_event) = 0;
};

}  // namespace media

#endif  // MEDIA_GPU_STARBOARD_STARBOARD_GPU_FACTORY_H_
