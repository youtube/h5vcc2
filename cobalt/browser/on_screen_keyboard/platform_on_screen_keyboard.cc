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

#include "cobalt/browser/on_screen_keyboard/platform_on_screen_keyboard.h"

namespace on_screen_keyboard {

PlatformOnScreenKeyboard::~PlatformOnScreenKeyboard() = default;

void PlatformOnScreenKeyboard::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PlatformOnScreenKeyboard::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

base::WeakPtr<PlatformOnScreenKeyboard> PlatformOnScreenKeyboard::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void PlatformOnScreenKeyboard::KeyboardBlurred() {
  observers_.Notify(&Observer::KeyboardBlurred);
}

void PlatformOnScreenKeyboard::KeyboardFocused() {
  observers_.Notify(&Observer::KeyboardFocused);
}

void PlatformOnScreenKeyboard::KeyboardTextChanged(const std::string& text) {
  observers_.Notify(&Observer::KeyboardTextChanged, text);
}

}  // namespace on_screen_keyboard
