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

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_ON_SCREEN_KEYBOARD_ON_SCREEN_KEYBOARD_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_ON_SCREEN_KEYBOARD_ON_SCREEN_KEYBOARD_H_

#include <optional>

#include "cobalt/browser/on_screen_keyboard/public/mojom/on_screen_keyboard.mojom-blink.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/core/dom/events/event_target.h"
#include "third_party/blink/renderer/modules/modules_export.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/graphics/color.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_receiver.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_wrapper_mode.h"
#include "third_party/blink/renderer/platform/supplementable.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class DOMRectReadOnly;
class ExecutionContext;
class LocalDOMWindow;
class ScriptState;

class MODULES_EXPORT OnScreenKeyboard final
    : public EventTarget,
      public Supplement<LocalDOMWindow>,
      public on_screen_keyboard::mojom::blink::OnScreenKeyboardClient {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static const char kSupplementName[];

  // For window.onScreenKeyboard
  static OnScreenKeyboard* onScreenKeyboard(LocalDOMWindow&);

  explicit OnScreenKeyboard(LocalDOMWindow&);

  // EventTarget implementation.
  const AtomicString& InterfaceName() const override;
  ExecutionContext* GetExecutionContext() const override;

  // mojom::blink::OnScreenKeyboardClient implementation.
  void KeyboardTextChanged(const String&) override;
  void KeyboardBlurred() override;
  void KeyboardFocused() override;

  // Web-exposed interface:
  ScriptPromise<IDLUndefined> show(ScriptState*, ExceptionState&);
  ScriptPromise<IDLUndefined> hide(ScriptState*, ExceptionState&);
  ScriptPromise<IDLUndefined> focus(ScriptState*, ExceptionState&);
  ScriptPromise<IDLUndefined> blur(ScriptState*, ExceptionState&);
  ScriptPromise<IDLUndefined> updateSuggestions(ScriptState*,
                                                const Vector<String>&,
                                                ExceptionState&);
  bool shown();
  bool suggestionsSupported();
  DOMRectReadOnly* boundingRect();
  bool keepFocus() const;
  void setKeepFocus(bool);
  String data() const;
  void setData(const String&);
  String backgroundColor() const;
  void setBackgroundColor(const String&);
  std::optional<bool> lightTheme() const;
  void setLightTheme(std::optional<bool>);

  void Trace(Visitor*) const override;

  DEFINE_ATTRIBUTE_EVENT_LISTENER(show, kShow)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(hide, kHide)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(focus, kFocus)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(blur, kBlur)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(input, kInput)

 private:
  static OnScreenKeyboard* From(LocalDOMWindow& window);

  // Mojo callbacks.
  void DidShow(ScriptPromiseResolver<IDLUndefined>*);
  void DidHide(ScriptPromiseResolver<IDLUndefined>*);
  void DidFocus(ScriptPromiseResolver<IDLUndefined>*);
  void DidBlur(ScriptPromiseResolver<IDLUndefined>*);
  void DidUpdateSuggestions(ScriptPromiseResolver<IDLUndefined>*);

  void EnsureReceiverIsBound();

  String data_;

  bool keep_focus_ = false;

  String background_color_;
  std::optional<Color> parsed_background_color_;

  std::optional<bool> use_light_theme_;

  std::optional<bool> suggestions_supported_;

  HeapMojoRemote<on_screen_keyboard::mojom::blink::OnScreenKeyboard>
      on_screen_keyboard_remote_;
  HeapMojoReceiver<on_screen_keyboard::mojom::blink::OnScreenKeyboardClient,
                   OnScreenKeyboard>
      on_screen_keyboard_client_receiver_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COBALT_ON_SCREEN_KEYBOARD_ON_SCREEN_KEYBOARD_H_
