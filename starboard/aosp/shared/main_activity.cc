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

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "cobalt/aosp/jni_headers/MainActivity_jni.h"
#include "starboard/android/shared/starboard_bridge.h"
#include "starboard/aosp/shared/application_aosp.h"
#include "starboard/aosp/shared/window_surface.h"
#include "starboard/common/log.h"
#include "starboard/common/time.h"
#include "starboard/system.h"
#include "third_party/jni_zero/jni_zero.h"

int main(int argc, char** argv);

namespace {

// Cobalt normally runs main() on the process's main thread, which has a large
// stack. Here it runs on a dedicated thread instead, and the 1MB (Android
// default) stack size is not enough for InstallationManager, which reads its
// store file into a 1MB stack buffer. Use 2MB, the same size Cobalt 25 and RDK
// use.
constexpr size_t kStarboardMainStackSize = 2 * 1024 * 1024;

// How long SurfaceHolder.surfaceDestroyed() may block waiting to drop the
// surface. AppEventRunner gives each lifecycle transition 2s to complete, and a
// conceal from the started state also goes through the blur transition, so
// allow for two.
constexpr int64_t kSurfaceReleaseTimeoutUsec = 4'000'000;

void* StarboardMain(void* /*context*/) {
  pthread_setname_np(pthread_self(), "StarboardMain");

  JNIEnv* env = jni_zero::AttachCurrentThread();
  // Android starts the process with the working directory at "/" (read-only)
  // nplb (and POSIX code) may expect relative paths to be writable, so
  // cd to a writable app directory before startup.
  char files_dir[PATH_MAX];
  if (SbSystemGetPath(kSbSystemPathFilesDirectory, files_dir,
                      sizeof(files_dir))) {
    if (chdir(files_dir) != 0) {
      SB_LOG(WARNING) << "cobalt_loader: chdir to " << files_dir << " failed";
    }
  }

  std::vector<std::string> args;
  args.push_back("cobalt_loader");
  // Don't use "/dev/shm" for shared memory; it does not exist on Android.
  // With this switch it falls back to GetTempDir().
  args.push_back("--disable-dev-shm-usage");
  starboard::StarboardBridge::GetInstance()->AppendArgs(env, &args);

  // For Android instrumentation test runs the runner provides a stdout file.
  // Redirect stdout/stderr to it.
  const std::string kStdoutFlag = "--android_stdout_file=";
  for (auto it = args.begin(); it != args.end();) {
    if (it->rfind(kStdoutFlag, 0) == 0) {
      const std::string path = it->substr(kStdoutFlag.size());
      int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
      if (fd < 0) {
        SB_LOG(ERROR) << "Failed to open stdout redirect file " << path << ": "
                      << strerror(errno);
        exit(EXIT_FAILURE);
      }
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      close(fd);
      it = args.erase(it);
    } else {
      ++it;
    }
  }

  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (std::string& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  int error_level = main(static_cast<int>(args.size()), argv.data());

  // End the process once SbRunStarboardMain() returns (when the activity is
  // destroyed) so the next launch starts clean. Android keeps the process
  // running and may reuse it to re-create the Activity, and MainActivity would
  // see leftover state such as a non-null BaseStarboardBridge, treat it as a
  // warm start and never start the loader again. Forcing it to start again
  // wouldn't work, SbEventHandle() deletes Cobalt's AppEventDelegate on
  // kSbEventTypeStop and never re-creates it, so the next start event is
  // dropped.
  //
  // _exit() instead exit(): DoStop() already flushed stdio, so the extra
  // teardown exit() runs isn't needed.
  SB_LOG(INFO) << "cobalt_loader: Starboard exited with " << error_level
               << "; ending the process.";

  _exit(error_level);
}

}  // namespace

namespace starboard {

void JNI_MainActivity_StartLoader(JNIEnv* env) {
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0) {
    SB_LOG(ERROR) << "Failed to initialize StarboardMain thread attributes";
    return;
  }

  if (pthread_attr_setstacksize(&attr, kStarboardMainStackSize) != 0 ||
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
    SB_LOG(ERROR) << "Failed to set StarboardMain thread attributes";
    pthread_attr_destroy(&attr);
    return;
  }

  pthread_t thread;
  if (pthread_create(&thread, &attr, &StarboardMain, nullptr) != 0) {
    SB_LOG(ERROR) << "Failed to create StarboardMain thread";
  }

  pthread_attr_destroy(&attr);
}

// MainActivity hands the Activity window's Surface to Starboard here.
void JNI_MainActivity_NativeOnSurfaceCreated(
    JNIEnv* env,
    const jni_zero::JavaParamRef<jobject>& surface) {
  ANativeWindow* native_window = ANativeWindow_fromSurface(env, surface.obj());
  SB_LOG(INFO) << "cobalt_loader: Starboard surface created, native_window="
               << native_window;
  starboard::android::shared::SetWindowSurface(native_window);
}

void JNI_MainActivity_NativeOnSurfaceDestroyed(JNIEnv*) {
  SB_LOG(INFO) << "cobalt_loader: Starboard surface destroyed.";
  ApplicationAOSP* application = ApplicationAOSP::GetIfExists();
  if (application == nullptr) {
    // Nothing is running yet, or it is already gone; just drop the surface.
    starboard::android::shared::SetWindowSurface(nullptr);
    return;
  }
  int64_t start_usec = starboard::CurrentMonotonicTime();
  bool released =
      application->ReleaseWindowSurfaceAndWait(kSurfaceReleaseTimeoutUsec);

  SB_LOG(INFO) << "cobalt_loader: Starboard surface released after "
               << (starboard::CurrentMonotonicTime() - start_usec) / 1000
               << " ms, released=" << released;
}

void JNI_MainActivity_NativeSendBlurEvent(JNIEnv* /*env*/) {
  if (ApplicationAOSP* application = ApplicationAOSP::GetIfExists()) {
    application->Blur(nullptr, nullptr);
  }
}

void JNI_MainActivity_NativeSendFocusEvent(JNIEnv* /*env*/) {
  if (ApplicationAOSP* application = ApplicationAOSP::GetIfExists()) {
    application->Focus(nullptr, nullptr);
  }
}

void JNI_MainActivity_NativeSendConcealEvent(JNIEnv* /*env*/) {
  if (ApplicationAOSP* application = ApplicationAOSP::GetIfExists()) {
    application->Conceal(nullptr, nullptr);
  }
}

void JNI_MainActivity_NativeSendStopEvent(JNIEnv* /*env*/) {
  if (ApplicationAOSP* application = ApplicationAOSP::GetIfExists()) {
    application->Stop(0);
  }
}

jboolean JNI_MainActivity_NativeSendKeyEvent(JNIEnv* /*env*/,
                                             jint key_code,
                                             jint action,
                                             jint unicode_char,
                                             jint meta_state) {
  ApplicationAOSP* application = ApplicationAOSP::GetIfExists();
  if (application == nullptr) {
    return JNI_FALSE;
  }
  return application->InjectKeyEvent(key_code, action, unicode_char, meta_state)
             ? JNI_TRUE
             : JNI_FALSE;
}

}  // namespace starboard
