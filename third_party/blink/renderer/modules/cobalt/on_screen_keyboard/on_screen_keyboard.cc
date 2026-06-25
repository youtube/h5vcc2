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

#include "third_party/blink/renderer/modules/cobalt/on_screen_keyboard/on_screen_keyboard.h"

#include "cobalt/browser/on_screen_keyboard/public/mojom/on_screen_keyboard.mojom-blink-forward.h"
#include "cobalt/browser/on_screen_keyboard/public/mojom/on_screen_keyboard.mojom-blink.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/event_type_names.h"
#include "third_party/blink/renderer/core/events/input_event.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/geometry/dom_rect_read_only.h"
#include "third_party/blink/renderer/modules/event_target_modules_names.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "ui/gfx/geometry/rect_f.h"

namespace blink {

// static
const char OnScreenKeyboard::kSupplementName[] = "OnScreenKeyboard";

OnScreenKeyboard::OnScreenKeyboard(LocalDOMWindow& window)
    : Supplement<LocalDOMWindow>(window),
      on_screen_keyboard_remote_(window.GetExecutionContext()),
      on_screen_keyboard_client_receiver_(this, GetSupplementable()) {}

// static
OnScreenKeyboard* OnScreenKeyboard::From(LocalDOMWindow& window) {
  OnScreenKeyboard* on_screen_keyboard =
      Supplement<LocalDOMWindow>::From<OnScreenKeyboard>(window);
  if (!on_screen_keyboard) {
    on_screen_keyboard = MakeGarbageCollected<OnScreenKeyboard>(window);
    ProvideTo(window, on_screen_keyboard);
  }
  return on_screen_keyboard;
}

// static
OnScreenKeyboard* OnScreenKeyboard::onScreenKeyboard(LocalDOMWindow& window) {
  return From(window);
}

const AtomicString& OnScreenKeyboard::InterfaceName() const {
  return event_target_names::kOnScreenKeyboard;
}

ExecutionContext* OnScreenKeyboard::GetExecutionContext() const {
  return GetSupplementable();
}

void OnScreenKeyboard::KeyboardTextChanged(const String& text) {
  DispatchEvent(*InputEvent::CreateInput(InputEvent::InputType::kNone, text,
                                         InputEvent::kNotComposing, nullptr));
}

void OnScreenKeyboard::KeyboardBlurred() {
  DispatchEvent(*Event::Create(event_type_names::kBlur));
}

void OnScreenKeyboard::KeyboardFocused() {
  DispatchEvent(*Event::Create(event_type_names::kFocus));
}

ScriptPromise<IDLUndefined> OnScreenKeyboard::show(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  EnsureReceiverIsBound();

  if (!on_screen_keyboard_client_receiver_.is_bound()) {
    auto task_runner =
        GetExecutionContext()->GetTaskRunner(TaskType::kMiscPlatformAPI);
    on_screen_keyboard_remote_->RegisterClient(
        on_screen_keyboard_client_receiver_.BindNewPipeAndPassRemote(
            task_runner));
  }

  auto options = on_screen_keyboard::mojom::blink::KeyboardOptions::New();
  if (parsed_background_color_.has_value()) {
    options->background_color =
        on_screen_keyboard::mojom::blink::BackgroundColor::New(
            parsed_background_color_->Red(), parsed_background_color_->Green(),
            parsed_background_color_->Blue());
  }
  if (use_light_theme_.has_value()) {
    options->theme_override =
        *use_light_theme_
            ? on_screen_keyboard::mojom::blink::ThemeOverride::kLightTheme
            : on_screen_keyboard::mojom::blink::ThemeOverride::kDarkTheme;
  }

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_state.GetContext());
  on_screen_keyboard_remote_->Show(
      data_.IsNull() ? g_empty_string : data_, std::move(options),
      WTF::BindOnce(&OnScreenKeyboard::DidShow, WrapPersistent(this),
                    WrapPersistent(resolver)));
  return resolver->Promise();
}

ScriptPromise<IDLUndefined> OnScreenKeyboard::hide(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  EnsureReceiverIsBound();

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_state.GetContext());
  on_screen_keyboard_remote_->Hide(WTF::BindOnce(&OnScreenKeyboard::DidHide,
                                                 WrapPersistent(this),
                                                 WrapPersistent(resolver)));
  return resolver->Promise();
}

ScriptPromise<IDLUndefined> OnScreenKeyboard::focus(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  EnsureReceiverIsBound();

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_state.GetContext());
  on_screen_keyboard_remote_->Focus(WTF::BindOnce(&OnScreenKeyboard::DidFocus,
                                                  WrapPersistent(this),
                                                  WrapPersistent(resolver)));
  return resolver->Promise();
}

ScriptPromise<IDLUndefined> OnScreenKeyboard::blur(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  EnsureReceiverIsBound();

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_state.GetContext());
  on_screen_keyboard_remote_->Blur(WTF::BindOnce(&OnScreenKeyboard::DidBlur,
                                                 WrapPersistent(this),
                                                 WrapPersistent(resolver)));
  return resolver->Promise();
}

ScriptPromise<IDLUndefined> OnScreenKeyboard::updateSuggestions(
    ScriptState* script_state,
    const Vector<String>& suggestions,
    ExceptionState& exception_state) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver<IDLUndefined>>(
      script_state, exception_state.GetContext());

  if (suggestionsSupported()) {
    EnsureReceiverIsBound();
    on_screen_keyboard_remote_->UpdateSuggestions(
        suggestions,
        WTF::BindOnce(&OnScreenKeyboard::DidUpdateSuggestions,
                      WrapPersistent(this), WrapPersistent(resolver)));
  } else {
    LOG(WARNING)
        << "On-screen keyboard suggestions are not supported on this platform";
    resolver->Reject();
  }

  return resolver->Promise();
}

bool OnScreenKeyboard::shown() {
  EnsureReceiverIsBound();
  bool result;
  on_screen_keyboard_remote_->IsShown(&result);
  return result;
}

bool OnScreenKeyboard::suggestionsSupported() {
  if (!suggestions_supported_.has_value()) {
    EnsureReceiverIsBound();
    bool supported;
    on_screen_keyboard_remote_->SupportsSuggestions(&supported);
    suggestions_supported_ = supported;
  }
  return *suggestions_supported_;
}

DOMRectReadOnly* OnScreenKeyboard::boundingRect() {
  EnsureReceiverIsBound();
  gfx::RectF bounding_rect;
  on_screen_keyboard_remote_->BoundingRect(&bounding_rect);
  return DOMRectReadOnly::FromRectF(bounding_rect);
}

bool OnScreenKeyboard::keepFocus() const {
  return keep_focus_;
}

void OnScreenKeyboard::setKeepFocus(bool keep_focus) {
  if (keep_focus != keep_focus_) {
    EnsureReceiverIsBound();
    on_screen_keyboard_remote_->KeepFocus(keep_focus);
    keep_focus_ = keep_focus;
  }
}

String OnScreenKeyboard::data() const {
  return data_;
}

void OnScreenKeyboard::setData(const String& data) {
  data_ = data;
}

String OnScreenKeyboard::backgroundColor() const {
  return background_color_;
}

void OnScreenKeyboard::setBackgroundColor(const String& background_color) {
  background_color_ = background_color;

  Color parsed_color;
  if (!CSSParser::ParseColor(parsed_color, background_color)) {
    LOG(WARNING) << "Invalid on-screen keyboard background color "
                 << background_color;
    parsed_background_color_.reset();
  } else {
    parsed_background_color_ = parsed_color;
  }
}

std::optional<bool> OnScreenKeyboard::lightTheme() const {
  return use_light_theme_;
}

void OnScreenKeyboard::setLightTheme(std::optional<bool> light_theme) {
  use_light_theme_ = light_theme;
}

void OnScreenKeyboard::DidShow(ScriptPromiseResolver<IDLUndefined>* resolver) {
  resolver->Resolve();
  DispatchEvent(*Event::Create(event_type_names::kShow));
}

void OnScreenKeyboard::DidHide(ScriptPromiseResolver<IDLUndefined>* resolver) {
  resolver->Resolve();
  DispatchEvent(*Event::Create(event_type_names::kHide));
}

void OnScreenKeyboard::DidFocus(ScriptPromiseResolver<IDLUndefined>* resolver) {
  resolver->Resolve();
  DispatchEvent(*Event::Create(event_type_names::kFocus));
}

void OnScreenKeyboard::DidBlur(ScriptPromiseResolver<IDLUndefined>* resolver) {
  resolver->Resolve();
  DispatchEvent(*Event::Create(event_type_names::kBlur));
}

void OnScreenKeyboard::DidUpdateSuggestions(
    ScriptPromiseResolver<IDLUndefined>* resolver) {
  resolver->Resolve();
  // TODO: b/513162149 - Support for suggestions and the corresponding
  // suggestionsUpdated event was removed from classic Cobalt in PR #2785.
}

void OnScreenKeyboard::EnsureReceiverIsBound() {
  CHECK(GetExecutionContext());

  if (on_screen_keyboard_remote_.is_bound()) {
    return;
  }

  auto task_runner =
      GetExecutionContext()->GetTaskRunner(TaskType::kMiscPlatformAPI);
  GetExecutionContext()->GetBrowserInterfaceBroker().GetInterface(
      on_screen_keyboard_remote_.BindNewPipeAndPassReceiver(task_runner));
}

void OnScreenKeyboard::Trace(Visitor* visitor) const {
  visitor->Trace(on_screen_keyboard_remote_);
  visitor->Trace(on_screen_keyboard_client_receiver_);
  EventTarget::Trace(visitor);
  Supplement<LocalDOMWindow>::Trace(visitor);
}

}  // namespace blink
