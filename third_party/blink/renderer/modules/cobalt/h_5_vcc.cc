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

#include "third_party/blink/renderer/modules/cobalt/h_5_vcc.h"

#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/modules/cobalt/crash_log/crash_log.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_accessibility/h_5_vcc_accessibility.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_experiments/h_5_vcc_experiments.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_memory/h_5_vcc_memory.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_metrics/h_5_vcc_metrics.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_native_stability/h_5_vcc_native_stability.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_runtime/h_5_vcc_runtime.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_settings/h_5_vcc_settings.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_storage/h_5_vcc_storage.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_system/h_5_vcc_system.h"
#include "third_party/blink/renderer/modules/cobalt/h5vcc_updater/h_5_vcc_updater.h"

namespace blink {

// static
const char H5vcc::kSupplementName[] = "H5vcc";

// static
H5vcc* H5vcc::h5vcc(LocalDOMWindow& window) {
  H5vcc* h5vcc = Supplement<LocalDOMWindow>::From<H5vcc>(window);
  if (!h5vcc && window.GetExecutionContext()) {
    h5vcc = MakeGarbageCollected<H5vcc>(window);
    ProvideTo(window, h5vcc);
  }
  return h5vcc;
}

H5vcc::H5vcc(LocalDOMWindow& window)
    : Supplement<LocalDOMWindow>(window),
      crash_log_(MakeGarbageCollected<CrashLog>(window)),
      accessibility_(MakeGarbageCollected<H5vccAccessibility>(window)),
      experiments_(MakeGarbageCollected<H5vccExperiments>(window)),
      memory_(MakeGarbageCollected<H5vccMemory>(window)),
      metrics_(MakeGarbageCollected<H5vccMetrics>(window)),
      system_(MakeGarbageCollected<H5vccSystem>(window)),
      runtime_(MakeGarbageCollected<H5vccRuntime>(window)),
      storage_(MakeGarbageCollected<H5vccStorage>(window)),
      settings_(MakeGarbageCollected<H5vccSettings>(window)),
      updater_(MakeGarbageCollected<H5vccUpdater>(window)),
      native_stability_(MakeGarbageCollected<H5vccNativeStability>(window)) {}

void H5vcc::Trace(Visitor* visitor) const {
  visitor->Trace(crash_log_);
  visitor->Trace(accessibility_);
  visitor->Trace(experiments_);
  visitor->Trace(memory_);
  visitor->Trace(metrics_);
  visitor->Trace(system_);
  visitor->Trace(runtime_);
  visitor->Trace(storage_);
  visitor->Trace(settings_);
  visitor->Trace(updater_);
  visitor->Trace(native_stability_);
  Supplement<LocalDOMWindow>::Trace(visitor);
  ScriptWrappable::Trace(visitor);
}

}  // namespace blink
