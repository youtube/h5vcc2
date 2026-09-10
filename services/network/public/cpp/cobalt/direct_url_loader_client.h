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

#ifndef SERVICES_NETWORK_PUBLIC_CPP_COBALT_DIRECT_URL_LOADER_CLIENT_H_
#define SERVICES_NETWORK_PUBLIC_CPP_COBALT_DIRECT_URL_LOADER_CLIENT_H_

#include <stdint.h>

#include <atomic>
#include <string_view>

#include "base/component_export.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/thread_annotations.h"
#include "net/base/io_buffer.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "url/gurl.h"

namespace network {

// Returns true if the request destination or URL pattern matches direct buffer
// loading.
COMPONENT_EXPORT(NETWORK_CPP)
bool IsDirectBufferRequest(mojom::RequestDestination destination,
                           const GURL& url);

class COMPONENT_EXPORT(NETWORK_CPP) DirectURLLoaderClient {
 public:
  virtual ~DirectURLLoaderClient() = default;
  virtual void OnDirectBufferAvailable(scoped_refptr<net::IOBuffer> buffer,
                                       int bytes_read) = 0;
};

class COMPONENT_EXPORT(NETWORK_CPP) DirectURLLoaderClientProxy
    : public base::RefCountedThreadSafe<DirectURLLoaderClientProxy> {
 public:
  explicit DirectURLLoaderClientProxy(DirectURLLoaderClient* client);

  void OnDirectBufferAvailable(scoped_refptr<net::IOBuffer> buffer,
                               int bytes_read);
  void Detach();

  static void Register(int32_t request_id,
                       scoped_refptr<DirectURLLoaderClientProxy> proxy);
  static void Unregister(int32_t request_id);
  static scoped_refptr<DirectURLLoaderClientProxy> Get(int32_t request_id);

  static void RegisterCorsLoader(
      int32_t request_id,
      scoped_refptr<DirectURLLoaderClientProxy> proxy);
  static void UnregisterCorsLoader(int32_t request_id);
  static scoped_refptr<DirectURLLoaderClientProxy> GetCorsLoader(
      int32_t request_id);

  void SetPaused(bool paused);
  bool IsPaused() const;
  void SetResumeCallback(base::RepeatingClosure resume_callback,
                         scoped_refptr<base::SequencedTaskRunner> task_runner);

 private:
  friend class base::RefCountedThreadSafe<DirectURLLoaderClientProxy>;
  ~DirectURLLoaderClientProxy() = default;

  base::Lock lock_;
  raw_ptr<DirectURLLoaderClient> client_ GUARDED_BY(lock_) = nullptr;
  std::atomic<bool> is_paused_{false};
  base::Lock resume_lock_;
  base::RepeatingClosure resume_callback_ GUARDED_BY(resume_lock_);
  scoped_refptr<base::SequencedTaskRunner> resume_task_runner_
      GUARDED_BY(resume_lock_);
};

}  // namespace network

#endif  // SERVICES_NETWORK_PUBLIC_CPP_COBALT_DIRECT_URL_LOADER_CLIENT_H_
