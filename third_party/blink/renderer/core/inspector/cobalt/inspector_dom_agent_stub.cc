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

#include "third_party/blink/renderer/core/inspector/inspector_dom_agent.h"

namespace blink {

void InspectorDOMAgent::DomContentLoadedEventFired(LocalFrame*) {}
void InspectorDOMAgent::DidCommitLoad(LocalFrame*, DocumentLoader*) {}
void InspectorDOMAgent::DidRestoreFromBackForwardCache(LocalFrame*) {}
void InspectorDOMAgent::FrameDocumentUpdated(LocalFrame*) {}
void InspectorDOMAgent::FrameOwnerContentUpdated(LocalFrame*,
                                                 HTMLFrameOwnerElement*) {}
void InspectorDOMAgent::PseudoElementCreated(PseudoElement*) {}
void InspectorDOMAgent::TopLayerElementsChanged() {}
void InspectorDOMAgent::PseudoElementDestroyed(PseudoElement*) {}
void InspectorDOMAgent::NodeCreated(Node*) {}
void InspectorDOMAgent::UpdateScrollableFlag(Node*, std::optional<bool>) {}
void InspectorDOMAgent::Trace(Visitor*) const {}
void InspectorDOMAgent::DidInsertDOMNode(Node*) {}
void InspectorDOMAgent::WillRemoveDOMNode(Node*) {}
void InspectorDOMAgent::WillModifyDOMAttr(Element*,
                                          const AtomicString&,
                                          const AtomicString&) {}
void InspectorDOMAgent::DidModifyDOMAttr(Element*,
                                         const QualifiedName&,
                                         const AtomicString&) {}
void InspectorDOMAgent::DidRemoveDOMAttr(Element*, const QualifiedName&) {}
void InspectorDOMAgent::CharacterDataModified(CharacterData*) {}
void InspectorDOMAgent::DidInvalidateStyleAttr(Node*) {}
void InspectorDOMAgent::DidPerformSlotDistribution(HTMLSlotElement*) {}
void InspectorDOMAgent::DidPushShadowRoot(Element*, ShadowRoot*) {}
void InspectorDOMAgent::WillPopShadowRoot(Element*, ShadowRoot*) {}

}  // namespace blink
