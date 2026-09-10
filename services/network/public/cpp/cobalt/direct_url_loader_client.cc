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

#include "services/network/public/cpp/cobalt/direct_url_loader_client.h"

#include <map>
#include <string_view>

#include "base/no_destructor.h"

namespace network {

bool IsDirectBufferRequest(mojom::RequestDestination destination,
                           const GURL& url) {
  if (destination == mojom::RequestDestination::kVideo ||
      destination == mojom::RequestDestination::kAudio ||
      destination == mojom::RequestDestination::kTrack) {
    return true;
  }
  std::string_view host = url.host_piece();
  std::string_view path = url.path_piece();
  return host.find("googlevideo.com") != std::string_view::npos ||
         path.find("videoplayback") != std::string_view::npos ||
         path.find("initplayback") != std::string_view::npos;
}

namespace {
struct Registry {
  base::Lock lock;
  std::map<int32_t, scoped_refptr<DirectURLLoaderClientProxy>> clients;
  std::map<int32_t, scoped_refptr<DirectURLLoaderClientProxy>> cors_loaders;
};

Registry* GetRegistry() {
  static base::NoDestructor<Registry> registry;
  return registry.get();
}
}  // namespace

DirectURLLoaderClientProxy::DirectURLLoaderClientProxy(
    DirectURLLoaderClient* client)
    : client_(client) {}

void DirectURLLoaderClientProxy::OnDirectBufferAvailable(
    scoped_refptr<net::IOBuffer> buffer,
    int bytes_read) {
  base::AutoLock auto_lock(lock_);
  if (client_) {
    client_->OnDirectBufferAvailable(std::move(buffer), bytes_read);
  }
}

void DirectURLLoaderClientProxy::Detach() {
  base::AutoLock auto_lock(lock_);
  client_ = nullptr;
}

void DirectURLLoaderClientProxy::SetPaused(bool paused) {
  bool was_paused = is_paused_.exchange(paused);
  if (was_paused && !paused) {
    base::RepeatingClosure cb;
    scoped_refptr<base::SequencedTaskRunner> runner;
    {
      base::AutoLock auto_lock(resume_lock_);
      cb = resume_callback_;
      runner = resume_task_runner_;
    }
    if (cb && runner) {
      runner->PostTask(FROM_HERE, cb);
    }
  }
}

bool DirectURLLoaderClientProxy::IsPaused() const {
  return is_paused_.load(std::memory_order_relaxed);
}

void DirectURLLoaderClientProxy::SetResumeCallback(
    base::RepeatingClosure resume_callback,
    scoped_refptr<base::SequencedTaskRunner> task_runner) {
  base::AutoLock auto_lock(resume_lock_);
  resume_callback_ = std::move(resume_callback);
  resume_task_runner_ = std::move(task_runner);
}

// static
void DirectURLLoaderClientProxy::Register(
    int32_t request_id,
    scoped_refptr<DirectURLLoaderClientProxy> proxy) {
  Registry* registry = GetRegistry();
  base::AutoLock auto_lock(registry->lock);
  registry->clients[request_id] = std::move(proxy);
}

// static
void DirectURLLoaderClientProxy::Unregister(int32_t request_id) {
  Registry* registry = GetRegistry();
  base::AutoLock auto_lock(registry->lock);
  auto it = registry->clients.find(request_id);
  if (it != registry->clients.end()) {
    if (it->second) {
      it->second->Detach();
    }
    registry->clients.erase(it);
  }
}

// static
scoped_refptr<DirectURLLoaderClientProxy> DirectURLLoaderClientProxy::Get(
    int32_t request_id) {
  Registry* registry = GetRegistry();
  base::AutoLock auto_lock(registry->lock);
  auto it = registry->clients.find(request_id);
  if (it != registry->clients.end()) {
    return it->second;
  }
  return nullptr;
}

// static
void DirectURLLoaderClientProxy::RegisterCorsLoader(
    int32_t request_id,
    scoped_refptr<DirectURLLoaderClientProxy> proxy) {
  Registry* registry = GetRegistry();
  base::AutoLock auto_lock(registry->lock);
  registry->cors_loaders[request_id] = std::move(proxy);
}

// static
void DirectURLLoaderClientProxy::UnregisterCorsLoader(int32_t request_id) {
  Registry* registry = GetRegistry();
  base::AutoLock auto_lock(registry->lock);
  auto it = registry->cors_loaders.find(request_id);
  if (it != registry->cors_loaders.end()) {
    if (it->second) {
      it->second->Detach();
    }
    registry->cors_loaders.erase(it);
  }
}

// static
scoped_refptr<DirectURLLoaderClientProxy>
DirectURLLoaderClientProxy::GetCorsLoader(int32_t request_id) {
  Registry* registry = GetRegistry();
  base::AutoLock auto_lock(registry->lock);
  auto it = registry->cors_loaders.find(request_id);
  if (it != registry->cors_loaders.end()) {
    return it->second;
  }
  return nullptr;
}

}  // namespace network
