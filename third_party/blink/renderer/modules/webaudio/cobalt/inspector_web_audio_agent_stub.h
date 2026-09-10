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

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_WEBAUDIO_COBALT_INSPECTOR_WEB_AUDIO_AGENT_STUB_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_WEBAUDIO_COBALT_INSPECTOR_WEB_AUDIO_AGENT_STUB_H_

#include <cstdint>

#include "third_party/blink/renderer/core/inspector/inspector_base_agent.h"
#include "third_party/blink/renderer/modules/modules_export.h"

namespace blink {

class AudioListener;
class AudioNode;
class AudioParam;
class BaseAudioContext;
class Page;

// A stub implementation of InspectorWebAudioAgent used when the DevTools
// backend is disabled (enable_devtools_backend = false). Its lifetime is
// managed by the Page/InspectorSession, and it is thread-affine to the main
// renderer thread.
class MODULES_EXPORT InspectorWebAudioAgent final : public InspectorAgent {
 public:
  explicit InspectorWebAudioAgent(Page*) {}
  void Init(CoreProbeSink*,
            protocol::UberDispatcher*,
            InspectorSessionState*) override {}
  void Dispose() override {}
  void Trace(Visitor* visitor) const override {
    InspectorAgent::Trace(visitor);
  }

  void DidCreateBaseAudioContext(BaseAudioContext*) {}
  void WillDestroyBaseAudioContext(BaseAudioContext*) {}
  void DidChangeBaseAudioContext(BaseAudioContext*) {}
  void DidCreateAudioListener(AudioListener*) {}
  void WillDestroyAudioListener(AudioListener*) {}
  void DidCreateAudioNode(AudioNode*) {}
  void WillDestroyAudioNode(AudioNode*) {}
  void DidCreateAudioParam(AudioParam*) {}
  void DidDestroyAudioNode(AudioNode*) {}
  void DidCreateAudioParam(AudioParam*, AudioNode*) {}
  void DidDestroyAudioParam(AudioParam*) {}
  void DidConnectNodes(AudioNode*, AudioNode*, unsigned = 0, unsigned = 0) {}
  void DidDisconnectNodes(AudioNode*,
                          AudioNode* = nullptr,
                          unsigned = 0,
                          unsigned = 0) {}
  void DidConnectNodeParam(AudioNode*, AudioParam*, unsigned = 0) {}
  void DidDisconnectNodeParam(AudioNode*, AudioParam*, unsigned = 0) {}
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_WEBAUDIO_COBALT_INSPECTOR_WEB_AUDIO_AGENT_STUB_H_
