// Copyright 2026 The Cobalt Authors. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cobalt/browser/android/overlay/cobalt_video_overlay_window.h"

#include "base/android/jni_android.h"
#include "base/logging.h"
#include "content/public/browser/overlay_window.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "ui/android/view_android.h"
#include "ui/android/window_android.h"

// Include the generated JNI header.
#include "cobalt/android/jni_headers/CobaltPictureInPictureActivity_jni.h"

// The static factory method remains in the global namespace (or content namespace).
// static
std::unique_ptr<content::VideoOverlayWindow> content::VideoOverlayWindow::Create(
    content::VideoPictureInPictureWindowController* controller) {
  LOG(INFO) << "content::VideoOverlayWindow::Create called, instantiating CobaltVideoOverlayWindow";
  return std::make_unique<cobalt::CobaltVideoOverlayWindow>(controller);
}

namespace cobalt {

CobaltVideoOverlayWindow::CobaltVideoOverlayWindow(
    content::VideoPictureInPictureWindowController* controller)
    : controller_(controller) {
  LOG(INFO) << "CobaltVideoOverlayWindow constructor called";
  CreateJavaActivity();
}

CobaltVideoOverlayWindow::~CobaltVideoOverlayWindow() {
  LOG(INFO) << "CobaltVideoOverlayWindow destructor called";
  Close();
}

bool CobaltVideoOverlayWindow::IsActive() const {
  return is_visible_;
}

void CobaltVideoOverlayWindow::Close() {
  LOG(INFO) << "CobaltVideoOverlayWindow::Close called";
  if (!java_activity_ref_.is_null()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_CobaltPictureInPictureActivity_closeActivity(env, java_activity_ref_);
    java_activity_ref_.Reset();
  }
  is_visible_ = false;
}

void CobaltVideoOverlayWindow::ShowInactive() {
  LOG(INFO) << "CobaltVideoOverlayWindow::ShowInactive called";
  is_visible_ = true;
}

void CobaltVideoOverlayWindow::Hide() {
  LOG(INFO) << "CobaltVideoOverlayWindow::Hide called";
  is_visible_ = false;

  if (!java_activity_ref_.is_null()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_CobaltPictureInPictureActivity_closeActivity(env, java_activity_ref_);
    java_activity_ref_.Reset();
  }

  if (controller_) {
    controller_->OnWindowDestroyed(false /* should_pause_video */);
  }
}

bool CobaltVideoOverlayWindow::IsVisible() const {
  return is_visible_;
}

gfx::Rect CobaltVideoOverlayWindow::GetBounds() {
  // Return dummy bounds for now
  return gfx::Rect(0, 0, 100, 100);
}

void CobaltVideoOverlayWindow::UpdateNaturalSize(const gfx::Size& natural_size) {
  LOG(INFO) << "CobaltVideoOverlayWindow::UpdateNaturalSize called with size: " 
            << natural_size.ToString();
}

void CobaltVideoOverlayWindow::SetPlaybackState(PlaybackState playback_state) {
  LOG(INFO) << "CobaltVideoOverlayWindow::SetPlaybackState called: " << playback_state;
}

void CobaltVideoOverlayWindow::SetPlayPauseButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetSkipAdButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetNextTrackButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetPreviousTrackButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetMicrophoneMuted(bool muted) {}
void CobaltVideoOverlayWindow::SetCameraState(bool turned_on) {}
void CobaltVideoOverlayWindow::SetToggleMicrophoneButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetToggleCameraButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetHangUpButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetNextSlideButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetPreviousSlideButtonVisibility(bool is_visible) {}

void CobaltVideoOverlayWindow::SetMediaPosition(
    const media_session::MediaPosition& position) {}

void CobaltVideoOverlayWindow::SetSourceTitle(const std::u16string& source_title) {}

void CobaltVideoOverlayWindow::SetFaviconImages(
    const std::vector<media_session::MediaImage>& images) {}

void CobaltVideoOverlayWindow::SetSurfaceId(const viz::SurfaceId& surface_id) {
  LOG(INFO) << "CobaltVideoOverlayWindow::SetSurfaceId called";
}

void CobaltVideoOverlayWindow::SetJavaActivity(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& activity) {
  LOG(INFO) << "CobaltVideoOverlayWindow::SetJavaActivity called, storing reference";
  java_activity_ref_ = base::android::ScopedJavaGlobalRef<jobject>(activity);
  is_visible_ = true;
}

void CobaltVideoOverlayWindow::OnActivityDestroyed(JNIEnv* env) {
  LOG(INFO) << "CobaltVideoOverlayWindow::OnActivityDestroyed called, clearing reference";
  java_activity_ref_.Reset();
  is_visible_ = false;

  if (controller_) {
    controller_->OnWindowDestroyed(false /* should_pause_video */);
  }
}

void CobaltVideoOverlayWindow::CreateJavaActivity() {
  LOG(INFO) << "CobaltVideoOverlayWindow::CreateJavaActivity called, launching Android Activity";
  if (!controller_) {
    LOG(ERROR) << "No controller, cannot get context";
    return;
  }
  content::WebContents* web_contents = controller_->GetWebContents();
  if (!web_contents) {
    LOG(ERROR) << "No WebContents, cannot get context";
    return;
  }
  base::android::ScopedJavaLocalRef<jobject> j_web_contents =
      web_contents->GetJavaWebContents();
  JNIEnv* env = base::android::AttachCurrentThread();
  LOG(INFO) << "CoAT PiP Step 12: Calling JNI launchActivity with WebContents=" 
            << web_contents << ", window_ptr=" << this;
  Java_CobaltPictureInPictureActivity_launchActivity(
      env, j_web_contents,
      reinterpret_cast<jlong>(this));
}

}  // namespace cobalt
