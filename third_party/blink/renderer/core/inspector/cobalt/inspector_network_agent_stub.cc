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

#include "third_party/blink/renderer/core/inspector/inspector_network_agent.h"

namespace blink {

std::unique_ptr<protocol::Network::Initiator>
InspectorNetworkAgent::BuildInitiatorObject(Document*,
                                            const FetchInitiatorInfo&,
                                            int) {
  return nullptr;
}
String InspectorNetworkAgent::GetProtocolAsString(const ResourceResponse&) {
  return String();
}
void InspectorNetworkAgent::DidBlockRequest(const ResourceRequest&,
                                            DocumentLoader*,
                                            const KURL&,
                                            const ResourceLoaderOptions&,
                                            ResourceRequestBlockedReason,
                                            ResourceType) {}
void InspectorNetworkAgent::DidChangeResourcePriority(DocumentLoader*,
                                                      uint64_t,
                                                      WebURLRequest::Priority) {
}
void InspectorNetworkAgent::PrepareRequest(DocumentLoader*,
                                           ResourceRequest&,
                                           ResourceLoaderOptions&,
                                           ResourceType) {}
void InspectorNetworkAgent::WillSendRequest(ExecutionContext*,
                                            DocumentLoader*,
                                            const KURL&,
                                            const ResourceRequest&,
                                            const ResourceResponse&,
                                            const ResourceLoaderOptions&,
                                            ResourceType,
                                            RenderBlockingBehavior,
                                            base::TimeTicks) {}
void InspectorNetworkAgent::WillSendNavigationRequest(uint64_t,
                                                      DocumentLoader*,
                                                      const KURL&,
                                                      const AtomicString&,
                                                      EncodedFormData*) {}
void InspectorNetworkAgent::WillSendWorkerMainRequest(uint64_t, const KURL&) {}
void InspectorNetworkAgent::MarkResourceAsCached(DocumentLoader*, uint64_t) {}
void InspectorNetworkAgent::DidReceiveResourceResponse(uint64_t,
                                                       DocumentLoader*,
                                                       const ResourceResponse&,
                                                       const Resource*) {}
void InspectorNetworkAgent::DidReceiveData(uint64_t,
                                           DocumentLoader*,
                                           base::SpanOrSize<char const>) {}
void InspectorNetworkAgent::DidReceiveBlob(uint64_t,
                                           DocumentLoader*,
                                           scoped_refptr<BlobDataHandle>) {}
void InspectorNetworkAgent::DidReceiveEncodedDataLength(DocumentLoader*,
                                                        uint64_t,
                                                        size_t) {}
void InspectorNetworkAgent::DidFailLoading(CoreProbeSink*,
                                           uint64_t,
                                           DocumentLoader*,
                                           const ResourceError&,
                                           const base::UnguessableToken&) {}
void InspectorNetworkAgent::WillSendEventSourceRequest() {}
void InspectorNetworkAgent::WillDispatchEventSourceEvent(uint64_t,
                                                         const AtomicString&,
                                                         const AtomicString&,
                                                         const String&) {}
void InspectorNetworkAgent::WillLoadXHR(ExecutionContext*,
                                        const AtomicString&,
                                        const KURL&,
                                        bool,
                                        const HTTPHeaderMap&,
                                        bool) {}
void InspectorNetworkAgent::DidFinishXHR(XMLHttpRequest*) {}
void InspectorNetworkAgent::ScriptImported(uint64_t, const String&) {}
void InspectorNetworkAgent::DidReceiveScriptResponse(uint64_t) {}
void InspectorNetworkAgent::DidCommitLoad(LocalFrame*, DocumentLoader*) {}
void InspectorNetworkAgent::FrameScheduledNavigation(LocalFrame*,
                                                     const KURL&,
                                                     base::TimeDelta,
                                                     ClientNavigationReason) {}
void InspectorNetworkAgent::WillCreateWebSocket(
    ExecutionContext*,
    uint64_t,
    const KURL&,
    const String&,
    std::optional<base::UnguessableToken>*) {}
void InspectorNetworkAgent::WillSendWebSocketHandshakeRequest(
    ExecutionContext*,
    uint64_t,
    network::mojom::blink::WebSocketHandshakeRequest*) {}
void InspectorNetworkAgent::DidReceiveWebSocketHandshakeResponse(
    ExecutionContext*,
    uint64_t,
    network::mojom::blink::WebSocketHandshakeRequest*,
    network::mojom::blink::WebSocketHandshakeResponse*) {}
void InspectorNetworkAgent::DidCloseWebSocket(ExecutionContext*, uint64_t) {}
void InspectorNetworkAgent::DidReceiveWebSocketMessage(
    uint64_t,
    int,
    bool,
    const Vector<base::span<const char>>&) {}
void InspectorNetworkAgent::DidSendWebSocketMessage(uint64_t,
                                                    int,
                                                    bool,
                                                    base::span<const char>) {}
void InspectorNetworkAgent::DidReceiveWebSocketMessageError(uint64_t,
                                                            const String&) {}
void InspectorNetworkAgent::WebTransportCreated(ExecutionContext*,
                                                uint64_t,
                                                const KURL&) {}
void InspectorNetworkAgent::WebTransportConnectionEstablished(uint64_t) {}
void InspectorNetworkAgent::WebTransportClosed(uint64_t) {}
void InspectorNetworkAgent::DirectTCPSocketCreated(
    ExecutionContext*,
    uint64_t,
    const String&,
    uint16_t,
    protocol::Network::DirectTCPSocketOptions&) {}
void InspectorNetworkAgent::DirectTCPSocketOpened(uint64_t,
                                                  const String&,
                                                  uint16_t,
                                                  std::optional<String>,
                                                  std::optional<uint16_t>) {}
void InspectorNetworkAgent::DirectTCPSocketAborted(uint64_t, int) {}
void InspectorNetworkAgent::DirectTCPSocketClosed(uint64_t) {}
void InspectorNetworkAgent::FrameClearedScheduledNavigation(LocalFrame*) {}
void InspectorNetworkAgent::DirectTCPSocketChunkSent(
    uint64_t,
    base::span<const unsigned char>) {}
void InspectorNetworkAgent::DirectTCPSocketChunkReceived(
    uint64_t,
    base::span<const unsigned char>) {}
void InspectorNetworkAgent::DirectUDPSocketCreated(
    ExecutionContext*,
    uint64_t,
    protocol::Network::DirectUDPSocketOptions&) {}
void InspectorNetworkAgent::DirectUDPSocketOpened(uint64_t,
                                                  const String&,
                                                  uint16_t,
                                                  std::optional<String>,
                                                  std::optional<uint16_t>) {}
void InspectorNetworkAgent::DirectUDPSocketAborted(uint64_t, int) {}
void InspectorNetworkAgent::DirectUDPSocketClosed(uint64_t) {}
void InspectorNetworkAgent::DirectUDPSocketChunkSent(
    uint64_t,
    base::span<const unsigned char>,
    std::optional<String>,
    std::optional<uint16_t>) {}
void InspectorNetworkAgent::DirectUDPSocketChunkReceived(
    uint64_t,
    base::span<const unsigned char>,
    std::optional<String>,
    std::optional<uint16_t>) {}
void InspectorNetworkAgent::DidFinishLoading(uint64_t identifier,
                                             DocumentLoader*,
                                             base::TimeTicks,
                                             int64_t,
                                             int64_t) {}
void InspectorNetworkAgent::SetDevToolsIds(ResourceRequest&,
                                           const FetchInitiatorInfo&) {}
void InspectorNetworkAgent::IsCacheDisabled(bool* is_cache_disabled) const {}
void InspectorNetworkAgent::ShouldApplyDevtoolsCookieSettingOverrides(
    bool* should_apply_devtools_overrides) const {}
void InspectorNetworkAgent::ShouldBlockRequest(const KURL&, bool*) {}
void InspectorNetworkAgent::ShouldBypassServiceWorker(bool*) {}
void InspectorNetworkAgent::ShouldForceCorsPreflight(bool*) {}
void InspectorNetworkAgent::WillCreateP2PSocketUdp(
    std::optional<base::UnguessableToken>*) {}
void InspectorNetworkAgent::DidReceiveCorsRedirectResponse(
    uint64_t,
    DocumentLoader*,
    const ResourceResponse&,
    Resource*) {}
void InspectorNetworkAgent::Trace(Visitor*) const {}

}  // namespace blink
