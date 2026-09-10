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

#include "third_party/blink/renderer/modules/cobalt/h5vcc_settings/h_5_vcc_settings.h"

#include "base/types/expected.h"
#include "cobalt/browser/h5vcc_settings/public/mojom/h5vcc_settings.mojom-blink.h"
#include "media/base/decoder_buffer.h"
#include "media/base/demuxer_memory_limit.h"
#include "media/base/stream_parser.h"
#include "media/filters/source_buffer_state.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/renderer/bindings/core/v8/idl_types.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_long_string.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

#if BUILDFLAG(USE_STARBOARD_MEDIA)
#include "media/starboard/decoder_buffer_allocator.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)

namespace blink {
namespace {

using Result = base::expected<void, String>;

ScriptPromise<IDLUndefined> Reject(ScriptState* script_state,
                                   const ExceptionContext& exception_context,
                                   const String& error) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_context);
  ScriptPromise<IDLUndefined> promise = resolver->Promise();
  resolver->Reject(
      V8ThrowException::CreateTypeError(script_state->GetIsolate(), error));
  return promise;
}

ScriptPromise<IDLUndefined> Resolve(ScriptState* script_state,
                                    const ExceptionContext& exception_context) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_context);
  ScriptPromise<IDLUndefined> promise = resolver->Promise();
  resolver->Resolve();
  return promise;
}

template <typename T, typename Callback>
ScriptPromise<IDLUndefined> ProcessSettingAs(
    ScriptState* script_state,
    const ExceptionContext& exception_context,
    const String& name,
    const V8UnionLongOrString& value,
    Callback callback) {
  if (!value.IsLong()) {
    LOG(WARNING) << "The value for '" << name << "' must be a number.";
    return Reject(script_state, exception_context,
                  "The value for '" + name + "' must be a number.");
  }
  const int32_t int_value = value.GetAsLong();

  Result result = callback(static_cast<T>(int_value));
  if (!result.has_value()) {
    LOG(WARNING) << "Failed to set " << name << " to " << int_value << ": "
                 << result.error();
    return Reject(script_state, exception_context, result.error());
  }

  LOG(INFO) << name << " is set to " << int_value;
  return Resolve(script_state, exception_context);
}

template <typename ActionCallback>
ScriptPromise<IDLUndefined> ProcessSettingAsEnableOnly(
    ScriptState* script_state,
    const ExceptionContext& exception_context,
    const String& name,
    const V8UnionLongOrString& value,
    ActionCallback action) {
  return ProcessSettingAs<bool>(
      script_state, exception_context, name, value,
      [&name, action = std::move(action)](bool enable) -> Result {
        if (!enable) {
          return base::unexpected(name + " cannot be disabled.");
        }

        if (!action()) {
          return base::unexpected(name + " is not supported.");
        }
        return base::ok();
      });
}

template <typename ActionCallback>
ScriptPromise<IDLUndefined> ProcessSettingAsPositiveInt(
    ScriptState* script_state,
    const ExceptionContext& exception_context,
    const String& name,
    const V8UnionLongOrString& value,
    ActionCallback action) {
  return ProcessSettingAs<int>(
      script_state, exception_context, name, value,
      [&name, action = std::move(action)](int int_value) -> Result {
        if (int_value <= 0) {
          return base::unexpected(name + " must be a positive integer.");
        }

        if (!action(int_value)) {
          return base::unexpected(name + " is not supported.");
        }
        return base::ok();
      });
}

}  // namespace

H5vccSettings::H5vccSettings(LocalDOMWindow& window)
    : ExecutionContextLifecycleObserver(window.GetExecutionContext()),
      remote_h5vcc_settings_(window.GetExecutionContext()) {}

void H5vccSettings::ContextDestroyed() {
  ongoing_requests_.clear();
}

ScriptPromise<IDLUndefined> H5vccSettings::set(
    ScriptState* script_state,
    const WTF::String& name,
    const V8UnionLongOrString* value,
    ExceptionState& exception_state) {
#if BUILDFLAG(USE_STARBOARD_MEDIA)
  const ExceptionContext& exception_context = exception_state.GetContext();

  if (name.StartsWith("DecoderBuffer.")) {
    return ProcessSettingAs<int>(
        script_state, exception_context, name, *value,
        [name](int value) -> Result {
          auto result =
              ::media::DecoderBufferAllocator::SetSetting(name.Utf8(), value);
          if (!result.has_value()) {
            return base::unexpected(String::FromUTF8(result.error()));
          }
          return base::ok();
        });
  }

  if (name == "Media.AppendFirstSegmentSynchronously") {
    return ProcessSettingAs<bool>(script_state, exception_context, name, *value,
                                  [this](bool enable) -> Result {
                                    append_first_segment_synchronously_ =
                                        enable;
                                    return base::ok();
                                  });
  }
  if (name == "Media.ExperimentalMaxPendingBytesPerParse") {
    return ProcessSettingAsPositiveInt(
        script_state, exception_context, name, *value, [](int int_value) {
          ::media::SourceBufferState::SetMaxPendingBytesPerParseOverride(
              int_value);
          return true;
        });
  }
  if (name == "Media.IncrementalParseLookAhead") {
    return ProcessSettingAsEnableOnly(
        script_state, exception_context, name, *value, [] {
          ::media::StreamParser::SetEnableIncrementalParseLookAhead(true);
          return true;
        });
  }
  if (name == "Media.VideoBufferSizeReductionPercent") {
    return ProcessSettingAsPositiveInt(
        script_state, exception_context, name, *value, [](int int_value) {
          ::media::SetVideoBufferSizeReductionPercent(int_value);
          return true;
        });
  }
  if (name == "Media.RecordVideoDownloadThroughput") {
    return ProcessSettingAs<bool>(
        script_state, exception_context, name, *value,
        [](bool enable) -> Result {
          ResourceFetcher::SetRecordVideoDownloadThroughput(enable);
          return base::ok();
        });
  }
#else   // BUILDFLAG(USE_STARBOARD_MEDIA)
  // Fall back to Mojo if BUILDFLAG(USE_STARBOARD_MEDIA) isn't defined. The
  // settings will be stored in the browser but won't take effect. This is safe
  // as USE_STARBOARD_MEDIA is always enabled in production releases.
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)

  EnsureReceiverIsBound();

  h5vcc_settings::mojom::blink::ValuePtr mojo_value;
  if (value->IsString()) {
    mojo_value = h5vcc_settings::mojom::blink::Value::NewStringValue(
        value->GetAsString());
  } else if (value->IsLong()) {
    mojo_value =
        h5vcc_settings::mojom::blink::Value::NewIntValue(value->GetAsLong());
  } else {
    NOTREACHED();
  }

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_state.GetContext());
  auto promise = resolver->Promise();
  ongoing_requests_.insert(resolver);
  remote_h5vcc_settings_->SetValue(
      name, std::move(mojo_value),
      WTF::BindOnce(&H5vccSettings::OnSetValueFinished, WrapPersistent(this),
                    WrapPersistent(resolver)));
  return promise;
}

void H5vccSettings::OnSetValueFinished(
    ScriptPromiseResolver<IDLUndefined>* resolver) {
  ongoing_requests_.erase(resolver);
  resolver->Resolve();
}

void H5vccSettings::OnConnectionError() {
  remote_h5vcc_settings_.reset();
  HeapHashSet<Member<ScriptPromiseResolver<IDLUndefined>>>
      h5vcc_settings_promises;
  // Script may execute during a call to Resolve(). Swap these sets to prevent
  // concurrent modification.
  ongoing_requests_.swap(h5vcc_settings_promises);
  for (auto& resolver : h5vcc_settings_promises) {
    resolver->Reject("Mojo connection error.");
  }
}

void H5vccSettings::EnsureReceiverIsBound() {
  CHECK(GetExecutionContext());

  if (remote_h5vcc_settings_.is_bound()) {
    return;
  }

  auto task_runner =
      GetExecutionContext()->GetTaskRunner(TaskType::kMiscPlatformAPI);
  GetExecutionContext()->GetBrowserInterfaceBroker().GetInterface(
      remote_h5vcc_settings_.BindNewPipeAndPassReceiver(task_runner));
  remote_h5vcc_settings_.set_disconnect_handler(WTF::BindOnce(
      &H5vccSettings::OnConnectionError, WrapWeakPersistent(this)));
}

void H5vccSettings::Trace(Visitor* visitor) const {
  visitor->Trace(remote_h5vcc_settings_);
  visitor->Trace(ongoing_requests_);
  ExecutionContextLifecycleObserver::Trace(visitor);
  ScriptWrappable::Trace(visitor);
}

}  // namespace blink
