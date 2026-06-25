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

#include "cobalt/browser/on_screen_keyboard/on_screen_keyboard_impl.h"

#include "build/build_config.h"
#include "content/public/browser/web_contents_user_data.h"

namespace on_screen_keyboard {

// static
OnScreenKeyboardImpl* OnScreenKeyboardImpl::GetOrCreate(
    content::WebContents* web_contents,
    base::WeakPtr<PlatformOnScreenKeyboard> platform_on_screen_keyboard) {
  CHECK(web_contents);
  content::WebContentsUserData<OnScreenKeyboardImpl>::CreateForWebContents(
      web_contents, std::move(platform_on_screen_keyboard));
  return content::WebContentsUserData<OnScreenKeyboardImpl>::FromWebContents(
      web_contents);
}

OnScreenKeyboardImpl::OnScreenKeyboardImpl(
    content::WebContents* web_contents,
    base::WeakPtr<PlatformOnScreenKeyboard> platform_on_screen_keyboard)
    : content::WebContentsUserData<OnScreenKeyboardImpl>(*web_contents),
      platform_on_screen_keyboard_(std::move(platform_on_screen_keyboard)) {
  if (platform_on_screen_keyboard_) {
    platform_on_screen_keyboard_->AddObserver(this);
  }
}

OnScreenKeyboardImpl::~OnScreenKeyboardImpl() {
  if (platform_on_screen_keyboard_) {
    platform_on_screen_keyboard_->RemoveObserver(this);
  }
}

void OnScreenKeyboardImpl::Bind(
    mojo::PendingReceiver<mojom::OnScreenKeyboard> receiver) {
  receiver_set_.Add(this, std::move(receiver));
}

void OnScreenKeyboardImpl::RegisterClient(
    mojo::PendingRemote<mojom::OnScreenKeyboardClient> client) {
  on_screen_keyboard_client_.Bind(std::move(client));
}

void OnScreenKeyboardImpl::Show(const std::string& text,
                                mojom::KeyboardOptionsPtr options,
                                ShowCallback callback) {
  if (!platform_on_screen_keyboard_) {
    LOG(ERROR)
        << "No platform on-screen keyboard for this WebContents instance";
    std::move(callback).Run();
    return;
  }
  platform_on_screen_keyboard_->Show(text, std::move(options),
                                     std::move(callback));
}

void OnScreenKeyboardImpl::Hide(HideCallback callback) {
  if (!platform_on_screen_keyboard_) {
    LOG(ERROR)
        << "No platform on-screen keyboard for this WebContents instance";
    std::move(callback).Run();
    return;
  }
  platform_on_screen_keyboard_->Hide(std::move(callback));
}

void OnScreenKeyboardImpl::Focus(FocusCallback callback) {
  if (!platform_on_screen_keyboard_) {
    LOG(ERROR)
        << "No platform on-screen keyboard for this WebContents instance";
    std::move(callback).Run();
    return;
  }
  platform_on_screen_keyboard_->Focus(std::move(callback));
}

void OnScreenKeyboardImpl::Blur(BlurCallback callback) {
  if (!platform_on_screen_keyboard_) {
    LOG(ERROR)
        << "No platform on-screen keyboard for this WebContents instance";
    std::move(callback).Run();
    return;
  }
  platform_on_screen_keyboard_->Blur(std::move(callback));
}

void OnScreenKeyboardImpl::UpdateSuggestions(
    const std::vector<std::string>& suggestions,
    UpdateSuggestionsCallback callback) {
  if (!platform_on_screen_keyboard_) {
    LOG(ERROR)
        << "No platform on-screen keyboard for this WebContents instance";
    std::move(callback).Run();
    return;
  }
  platform_on_screen_keyboard_->UpdateSuggestions(suggestions,
                                                  std::move(callback));
}

void OnScreenKeyboardImpl::KeepFocus(bool keep_focus) {
  if (!platform_on_screen_keyboard_) {
    LOG(ERROR)
        << "No platform on-screen keyboard for this WebContents instance";
    return;
  }
  platform_on_screen_keyboard_->SetKeepFocusOnKeyboard(keep_focus);
}

void OnScreenKeyboardImpl::SupportsSuggestions(
    SupportsSuggestionsCallback callback) {
  if (!platform_on_screen_keyboard_) {
    LOG(ERROR)
        << "No platform on-screen keyboard for this WebContents instance";
    std::move(callback).Run(false);
    return;
  }
  platform_on_screen_keyboard_->SupportsSuggestions(std::move(callback));
}

void OnScreenKeyboardImpl::IsShown(IsShownCallback callback) {
  if (!platform_on_screen_keyboard_) {
    LOG(ERROR)
        << "No platform on-screen keyboard for this WebContents instance";
    std::move(callback).Run(false);
    return;
  }
  platform_on_screen_keyboard_->IsBeingShown(std::move(callback));
}

void OnScreenKeyboardImpl::BoundingRect(BoundingRectCallback callback) {
  if (!platform_on_screen_keyboard_) {
    LOG(ERROR)
        << "No platform on-screen keyboard for this WebContents instance";
    std::move(callback).Run(gfx::RectF());
    return;
  }
  platform_on_screen_keyboard_->BoundingRect(std::move(callback));
}

void OnScreenKeyboardImpl::KeyboardBlurred() {
  if (!on_screen_keyboard_client_.is_bound()) {
    return;
  }
  on_screen_keyboard_client_->KeyboardBlurred();
}

void OnScreenKeyboardImpl::KeyboardFocused() {
  if (!on_screen_keyboard_client_.is_bound()) {
    return;
  }
  on_screen_keyboard_client_->KeyboardFocused();
}

void OnScreenKeyboardImpl::KeyboardTextChanged(const std::string& text) {
  if (!on_screen_keyboard_client_.is_bound()) {
    return;
  }
  on_screen_keyboard_client_->KeyboardTextChanged(text);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(OnScreenKeyboardImpl);

}  // namespace on_screen_keyboard
