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

#include "cobalt/browser/h5vcc_memory/h5vcc_memory_impl.h"

#include <utility>

#include "cobalt/browser/h5vcc_memory/low_memory_manager.h"

namespace h5vcc_memory {

H5vccMemoryImpl::H5vccMemoryImpl(
    content::RenderFrameHost& render_frame_host,
    mojo::PendingReceiver<mojom::H5vccMemory> receiver)
    : content::DocumentService<mojom::H5vccMemory>(render_frame_host,
                                                   std::move(receiver)) {
  DETACH_FROM_THREAD(thread_checker_);
}

void H5vccMemoryImpl::Create(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<mojom::H5vccMemory> receiver) {
  new H5vccMemoryImpl(*render_frame_host, std::move(receiver));
}

void H5vccMemoryImpl::AddLowMemoryListener(
    mojo::PendingRemote<mojom::LowMemoryListener> listener) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto* manager = cobalt::browser::LowMemoryManager::GetInstance();
  manager->AddListener(std::move(listener));
}

}  // namespace h5vcc_memory
