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

#ifndef COBALT_BROWSER_ON_SCREEN_KEYBOARD_PLATFORM_ON_SCREEN_KEYBOARD_H_
#define COBALT_BROWSER_ON_SCREEN_KEYBOARD_PLATFORM_ON_SCREEN_KEYBOARD_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "cobalt/browser/on_screen_keyboard/public/mojom/on_screen_keyboard.mojom-forward.h"
#include "ui/gfx/geometry/rect_f.h"

namespace on_screen_keyboard {

class PlatformOnScreenKeyboard {
 public:
  struct Observer : public base::CheckedObserver {
    virtual void KeyboardBlurred() = 0;
    virtual void KeyboardFocused() = 0;
    virtual void KeyboardTextChanged(const std::string& text) = 0;
  };

  virtual ~PlatformOnScreenKeyboard();

  virtual void Show(const std::string& text,
                    mojom::KeyboardOptionsPtr options,
                    base::OnceClosure done_callback) = 0;
  virtual void Hide(base::OnceClosure done_callback) = 0;
  virtual void Focus(base::OnceClosure done_callback) = 0;
  virtual void Blur(base::OnceClosure done_callback) = 0;
  virtual void UpdateSuggestions(const std::vector<std::string>& suggestions,
                                 base::OnceClosure done_callback) = 0;
  virtual void SetKeepFocusOnKeyboard(bool keep_focus) = 0;
  virtual void SupportsSuggestions(
      base::OnceCallback<void(bool)> done_callback) = 0;
  virtual void IsBeingShown(base::OnceCallback<void(bool)> done_callback) = 0;
  virtual void BoundingRect(
      base::OnceCallback<void(const gfx::RectF&)> done_callback) = 0;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  base::WeakPtr<PlatformOnScreenKeyboard> GetWeakPtr();

 protected:
  void KeyboardBlurred();
  void KeyboardFocused();
  void KeyboardTextChanged(const std::string& text);

 private:
  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<PlatformOnScreenKeyboard> weak_ptr_factory_{this};
};

}  // namespace on_screen_keyboard

#endif  // COBALT_BROWSER_ON_SCREEN_KEYBOARD_PLATFORM_ON_SCREEN_KEYBOARD_H_
