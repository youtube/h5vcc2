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

#ifndef COBALT_BROWSER_ON_SCREEN_KEYBOARD_ON_SCREEN_KEYBOARD_IMPL_H_
#define COBALT_BROWSER_ON_SCREEN_KEYBOARD_ON_SCREEN_KEYBOARD_IMPL_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "cobalt/browser/on_screen_keyboard/platform_on_screen_keyboard.h"
#include "cobalt/browser/on_screen_keyboard/public/mojom/on_screen_keyboard.mojom.h"
#include "content/public/browser/web_contents_user_data.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace on_screen_keyboard {

// Implements the OnScreenKeyboard Mojo interface and interfaces with the
// platform-specific implementation in PlatformOnScreenKeyboard.
//
// There is one instance of this class per WebContents, since the on-screen
// keyboard is expected to be a per-window feature.
class OnScreenKeyboardImpl
    : public mojom::OnScreenKeyboard,
      public content::WebContentsUserData<OnScreenKeyboardImpl>,
      public PlatformOnScreenKeyboard::Observer {
 public:
  static OnScreenKeyboardImpl* GetOrCreate(
      content::WebContents*,
      base::WeakPtr<PlatformOnScreenKeyboard>);

  OnScreenKeyboardImpl(const OnScreenKeyboardImpl&) = delete;
  OnScreenKeyboardImpl& operator=(const OnScreenKeyboardImpl&) = delete;

  ~OnScreenKeyboardImpl() override;

  void Bind(mojo::PendingReceiver<mojom::OnScreenKeyboard> receiver);

 protected:
  friend content::WebContentsUserData<OnScreenKeyboardImpl>;
  WEB_CONTENTS_USER_DATA_KEY_DECL();

  OnScreenKeyboardImpl(
      content::WebContents* web_contents,
      base::WeakPtr<PlatformOnScreenKeyboard> platform_on_screen_keyboard);

  // mojom::OnScreenKeyboard overrides
  void Show(const std::string& text,
            mojom::KeyboardOptionsPtr options,
            ShowCallback) override;
  void Hide(HideCallback) override;
  void Focus(FocusCallback) override;
  void Blur(BlurCallback) override;
  void UpdateSuggestions(const std::vector<std::string>& suggestions,
                         UpdateSuggestionsCallback) override;
  void KeepFocus(bool) override;
  void SupportsSuggestions(SupportsSuggestionsCallback) override;
  void IsShown(IsShownCallback) override;
  void BoundingRect(BoundingRectCallback) override;

  // PlatformOnScreenKeyboard::Observer overrides
  void KeyboardBlurred() override;
  void KeyboardFocused() override;
  void KeyboardTextChanged(const std::string& text) override;

 private:
  void RegisterClient(
      mojo::PendingRemote<mojom::OnScreenKeyboardClient> client) override;

  mojo::Remote<mojom::OnScreenKeyboardClient> on_screen_keyboard_client_;
  mojo::ReceiverSet<mojom::OnScreenKeyboard> receiver_set_;

  base::WeakPtr<on_screen_keyboard::PlatformOnScreenKeyboard>
      platform_on_screen_keyboard_;
};

}  // namespace on_screen_keyboard

#endif  // COBALT_BROWSER_ON_SCREEN_KEYBOARD_ON_SCREEN_KEYBOARD_IMPL_H_
