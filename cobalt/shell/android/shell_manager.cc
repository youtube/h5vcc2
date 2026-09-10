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

#include "cobalt/shell/android/shell_manager.h"

#include <cstdint>
#include <memory>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/functional/bind.h"
#include "base/lazy_instance.h"
#include "cobalt/shell/android/cobalt_shell_jni_headers/ShellManager_jni.h"
#include "cobalt/shell/browser/migrate_storage_record/migration_manager.h"
#include "cobalt/shell/browser/shell.h"
#include "cobalt/shell/browser/shell_browser_context.h"
#include "cobalt/shell/browser/shell_browser_main_parts.h"
#include "cobalt/shell/browser/shell_content_browser_client.h"
#include "cobalt/shell/common/shell_switches.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

using base::android::JavaParamRef;
using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;

namespace {

struct GlobalState {
  GlobalState() {}
  base::android::ScopedJavaGlobalRef<jobject> j_shell_manager;
  // Accessed on the UI thread, together with the Java manager lifecycle.
  uint64_t generation = 0;
};

base::LazyInstance<GlobalState>::DestructorAtExit g_global_state =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

namespace content {

ScopedJavaLocalRef<jobject> CreateShellView(Shell* shell) {
  JNIEnv* env = base::android::AttachCurrentThread();
  if (g_global_state.Get().j_shell_manager.is_null()) {
    return ScopedJavaLocalRef<jobject>();
  }
  return Java_ShellManager_createShell(env,
                                       g_global_state.Get().j_shell_manager,
                                       reinterpret_cast<intptr_t>(shell));
}

void RemoveShellView(const JavaRef<jobject>& shell_view,
                     uint64_t manager_generation) {
  JNIEnv* env = base::android::AttachCurrentThread();
  if (g_global_state.Get().generation != manager_generation) {
    return;
  }
  ScopedJavaLocalRef<jobject> manager(g_global_state.Get().j_shell_manager);
  if (!manager.is_null()) {
    Java_ShellManager_removeShell(env, manager, shell_view);
  }
}

uint64_t GetShellManagerGeneration() {
  return g_global_state.Get().generation;
}

static void JNI_ShellManager_Init(JNIEnv* env,
                                  const JavaParamRef<jobject>& obj) {
  auto& state = g_global_state.Get();
  ++state.generation;
  state.j_shell_manager.Reset(obj);
}

static void JNI_ShellManager_Destroy(JNIEnv* env,
                                     const JavaParamRef<jobject>& obj) {
  auto& j_shell_manager = g_global_state.Get().j_shell_manager;
  if (!j_shell_manager.is_null() &&
      env->IsSameObject(j_shell_manager.obj(), obj.obj())) {
    ++g_global_state.Get().generation;
    j_shell_manager.Reset();
  }
}

void JNI_ShellManager_LaunchShell(JNIEnv* env,
                                  const JavaParamRef<jobject>& obj,
                                  const JavaParamRef<jstring>& jurl,
                                  const JavaParamRef<jstring>& jdeeplink_url) {
  auto& state = g_global_state.Get();
  if (state.j_shell_manager.is_null() ||
      !env->IsSameObject(state.j_shell_manager.obj(), obj.obj())) {
    return;
  }
  GURL url(base::android::ConvertJavaStringToUTF8(env, jurl));
  std::string deeplink_url =
      base::android::ConvertJavaStringToUTF8(env, jdeeplink_url);

  // We wrap the WebContents creation into a task because we must ensure
  // that any asynchronous storage migration is completely finished before
  // the browser accesses the default storage partition. If WebContents
  // is created too early, the JS environment will start with empty data.
  auto create_window_task = base::BindOnce(
      [](uint64_t generation, GURL url, std::string deeplink_url) {
        const auto& state = g_global_state.Get();
        // Storage migration may finish after the Activity was destroyed or
        // replaced. Do not retain its Java owner or launch into a new owner.
        if (state.generation != generation || state.j_shell_manager.is_null()) {
          return;
        }
        ShellBrowserContext* browserContext =
            ShellContentBrowserClient::Get()->browser_context();
        Shell::CreateNewWindow(browserContext, url, nullptr, gfx::Size(),
                               switches::ShouldCreateSplashScreen(),
                               deeplink_url);
      },
      state.generation, std::move(url), std::move(deeplink_url));

  auto* parts = ShellContentBrowserClient::Get()->shell_browser_main_parts();
  if (parts) {
    // Defers the execution of the task if the migration is still in progress.
    // If the migration is already complete, the task is executed synchronously.
    parts->PostOrRunIfStorageMigrationFinished(std::move(create_window_task));
  } else {
    std::move(create_window_task).Run();
  }
}

void DestroyShellManager() {
  ScopedJavaLocalRef<jobject> j_shell_manager(
      g_global_state.Get().j_shell_manager);
  if (j_shell_manager.is_null()) {
    return;
  }

  JNIEnv* env = base::android::AttachCurrentThread();
  Java_ShellManager_destroy(env, j_shell_manager);
}

base::android::ScopedJavaLocalRef<jstring>
JNI_ShellManager_AppendMigrationStatus(
    JNIEnv* env,
    const base::android::JavaParamRef<jstring>& jurl) {
  GURL url(base::android::ConvertJavaStringToUTF8(env, jurl));
  const std::string status_param = cobalt::migrate_storage_record::
      MigrationManager::GetMigrationStatusUrlParameter();
  if (!status_param.empty()) {
    // If a migration occurred on this launch, append its outcome directly
    // to the URL's query parameters (e.g. "?migration_status=0-0-0").
    // This makes the migration telemetry accessible to the loaded web
    // app.
    GURL::Replacements replacements;
    std::string query = url.query();
    if (!query.empty()) {
      query += "&";
    }
    query += status_param;
    replacements.SetQueryStr(query);
    url = url.ReplaceComponents(replacements);
  }
  return base::android::ConvertUTF8ToJavaString(env, url.spec());
}

}  // namespace content
