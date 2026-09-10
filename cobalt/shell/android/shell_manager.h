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

#ifndef COBALT_SHELL_ANDROID_SHELL_MANAGER_H_
#define COBALT_SHELL_ANDROID_SHELL_MANAGER_H_

#include <jni.h>

#include <cstdint>

#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"

class Shell;

namespace cc {
class Layer;
}

namespace content {

// Creates an Android specific shell view, which is our version of a shell
// window.  This view holds the controls and content views necessary to
// render a shell window.  Returns the java object representing the shell view.
// object.
base::android::ScopedJavaLocalRef<jobject> CreateShellView(Shell* shell);

// Removes a previously created shell view.
void RemoveShellView(const base::android::JavaRef<jobject>& shell_view,
                     uint64_t manager_generation);

// Returns the generation of the current Java ShellManager owner.
uint64_t GetShellManagerGeneration();

void ShellAttachLayer(cc::Layer* layer);
void ShellRemoveLayer(cc::Layer* layer);

// Destroys the ShellManager on app exit. Must not use the above functions
// after this is called.
void DestroyShellManager();

}  // namespace content

#endif  // COBALT_SHELL_ANDROID_SHELL_MANAGER_H_
