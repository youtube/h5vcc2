// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace network {

// static
scoped_refptr<SharedURLLoaderFactory> SharedURLLoaderFactory::Create(
    std::unique_ptr<PendingSharedURLLoaderFactory> pending_factory) {
  DCHECK(pending_factory);
  return pending_factory->CreateFactory();
}

SharedURLLoaderFactory::~SharedURLLoaderFactory() = default;

PendingSharedURLLoaderFactory::PendingSharedURLLoaderFactory() = default;

PendingSharedURLLoaderFactory::~PendingSharedURLLoaderFactory() = default;

bool SharedURLLoaderFactory::BypassRedirectChecks() const {
  return false;
}

#if BUILDFLAG(IS_COBALT)
void SharedURLLoaderFactory::CreateLoaderAndStartWithDirectClient(
    mojo::PendingReceiver<mojom::URLLoader> receiver,
    int32_t request_id,
    uint32_t options,
    const ResourceRequest& resource_request,
    mojo::PendingRemote<mojom::URLLoaderClient> client,
    base::WeakPtr<DirectURLLoaderClient> direct_client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  CreateLoaderAndStart(std::move(receiver), request_id, options,
                       resource_request, std::move(client), traffic_annotation);
}
#endif  // BUILDFLAG(IS_COBALT)

}  // namespace network
