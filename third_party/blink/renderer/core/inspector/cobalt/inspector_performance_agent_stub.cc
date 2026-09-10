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

#include "third_party/blink/renderer/core/inspector/inspector_performance_agent.h"

namespace blink {

InspectorPerformanceAgent::InspectorPerformanceAgent(
    InspectedFrames* inspected_frames)
    : InspectorBaseAgent<protocol::Performance::Metainfo>(
          protocol::Performance::Metainfo::domainName),
      inspected_frames_(inspected_frames),
      enabled_(&agent_state_, /*default_value=*/false),
      use_thread_ticks_(&agent_state_, /*default_value=*/false) {}

InspectorPerformanceAgent::~InspectorPerformanceAgent() = default;

void InspectorPerformanceAgent::Restore() {}

protocol::Response InspectorPerformanceAgent::enable(
    std::optional<String> time_domain) {
  return protocol::Response::Success();
}

protocol::Response InspectorPerformanceAgent::disable() {
  return protocol::Response::Success();
}

protocol::Response InspectorPerformanceAgent::setTimeDomain(
    const String& time_domain) {
  return protocol::Response::Success();
}

protocol::Response InspectorPerformanceAgent::getMetrics(
    std::unique_ptr<protocol::Array<protocol::Performance::Metric>>*
        out_result) {
  return protocol::Response::Success();
}

void InspectorPerformanceAgent::WillProcessTask(base::TimeTicks) {}
void InspectorPerformanceAgent::DidProcessTask(base::TimeTicks,
                                               base::TimeTicks) {}

void InspectorPerformanceAgent::Will(const probe::RecalculateStyle&) {}
void InspectorPerformanceAgent::Did(const probe::RecalculateStyle&) {}
void InspectorPerformanceAgent::Will(const probe::UpdateLayout&) {}
void InspectorPerformanceAgent::Did(const probe::UpdateLayout&) {}
void InspectorPerformanceAgent::Will(const probe::ExecuteScript&) {}
void InspectorPerformanceAgent::Did(const probe::ExecuteScript&) {}
void InspectorPerformanceAgent::Will(const probe::CallFunction&) {}
void InspectorPerformanceAgent::Did(const probe::CallFunction&) {}
void InspectorPerformanceAgent::Will(const probe::V8Compile&) {}
void InspectorPerformanceAgent::Did(const probe::V8Compile&) {}
void InspectorPerformanceAgent::ConsoleTimeStamp(v8::Isolate*,
                                                 v8::Local<v8::String>) {}
void InspectorPerformanceAgent::WillStartDebuggerTask() {}
void InspectorPerformanceAgent::DidFinishDebuggerTask() {}
void InspectorPerformanceAgent::Trace(Visitor*) const {}

}  // namespace blink
