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

#include "cobalt/browser/android/overlay/cobalt_video_overlay_window.h"

#include "base/android/jni_android.h"
#include "base/logging.h"
#include "cc/slim/surface_layer.h"
#include "cobalt/android/jni_headers/CobaltPictureInPictureActivity_jni.h"
#include "components/viz/common/surfaces/surface_id.h"
#include "content/public/browser/overlay_window.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/android/view_android.h"
#include "ui/android/window_android.h"
#include "ui/android/window_android_compositor.h"
#include "ui/android/window_android_observer.h"

// The static factory method remains in the global namespace (or content
// namespace). static
std::unique_ptr<content::VideoOverlayWindow>
content::VideoOverlayWindow::Create(
    content::VideoPictureInPictureWindowController* controller) {
  LOG(INFO) << "content::VideoOverlayWindow::Create called, instantiating "
               "CobaltVideoOverlayWindow";
  return std::make_unique<cobalt::CobaltVideoOverlayWindow>(controller);
}

namespace cobalt {

CobaltVideoOverlayWindow::CobaltVideoOverlayWindow(
    content::VideoPictureInPictureWindowController* controller)
    : controller_(controller) {
  LOG(INFO) << "CobaltVideoOverlayWindow constructor called";
  surface_layer_ = cc::slim::SurfaceLayer::Create();
  surface_layer_->SetIsDrawable(true);
  surface_layer_->SetStretchContentToFillBounds(true);
  surface_layer_->SetBackgroundColor(SkColors::kBlack);
  CreateJavaActivity();
}

CobaltVideoOverlayWindow::~CobaltVideoOverlayWindow() {
  LOG(INFO) << "CobaltVideoOverlayWindow destructor called";
  if (window_android_) {
    window_android_->RemoveObserver(this);
    window_android_ = nullptr;
  }
  Close();
}

void CobaltVideoOverlayWindow::OnRootWindowVisibilityChanged(bool visible) {}
void CobaltVideoOverlayWindow::OnAttachCompositor() {}
void CobaltVideoOverlayWindow::OnDetachCompositor() {}
void CobaltVideoOverlayWindow::OnAnimate(base::TimeTicks frame_begin_time) {}
void CobaltVideoOverlayWindow::OnActivityStopped() {
  Close();
}
void CobaltVideoOverlayWindow::OnActivityStarted() {}

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
  return gfx::Rect(bounds_);
}

void CobaltVideoOverlayWindow::UpdateNaturalSize(
    const gfx::Size& natural_size) {
  LOG(INFO) << "CobaltVideoOverlayWindow::UpdateNaturalSize called with size: "
            << natural_size.ToString();
}

void CobaltVideoOverlayWindow::SetPlaybackState(PlaybackState playback_state) {
  LOG(INFO) << "CobaltVideoOverlayWindow::SetPlaybackState called: "
            << playback_state;
}

// The following methods are required overrides from the base class
// content::VideoOverlayWindow. They are intentionally left empty because
// Cobalt's Android TV PiP implementation does not currently render custom
// media controls (such as skip ad, camera toggles, or playback buttons)
// directly onto the overlay window surface.
void CobaltVideoOverlayWindow::SetPlayPauseButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetSkipAdButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetNextTrackButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetPreviousTrackButtonVisibility(
    bool is_visible) {}
void CobaltVideoOverlayWindow::SetMicrophoneMuted(bool muted) {}
void CobaltVideoOverlayWindow::SetCameraState(bool turned_on) {}
void CobaltVideoOverlayWindow::SetToggleMicrophoneButtonVisibility(
    bool is_visible) {}
void CobaltVideoOverlayWindow::SetToggleCameraButtonVisibility(
    bool is_visible) {}
void CobaltVideoOverlayWindow::SetHangUpButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetNextSlideButtonVisibility(bool is_visible) {}
void CobaltVideoOverlayWindow::SetPreviousSlideButtonVisibility(
    bool is_visible) {}

void CobaltVideoOverlayWindow::SetMediaPosition(
    const media_session::MediaPosition& position) {}

void CobaltVideoOverlayWindow::SetSourceTitle(
    const std::u16string& source_title) {}

void CobaltVideoOverlayWindow::SetFaviconImages(
    const std::vector<media_session::MediaImage>& images) {}

void CobaltVideoOverlayWindow::SetSurfaceId(const viz::SurfaceId& surface_id) {
  LOG(INFO) << "CobaltVideoOverlayWindow::SetSurfaceId called: "
            << surface_id.ToString();

  const viz::SurfaceId& old_surface_id = surface_layer_->surface_id().is_valid()
                                             ? surface_layer_->surface_id()
                                             : surface_id;
  if (window_android_ && window_android_->GetCompositor() &&
      old_surface_id.frame_sink_id() != surface_id.frame_sink_id()) {
    // On Android, the new frame sink needs to be added before
    // removing the previous surface sink.
    window_android_->GetCompositor()->AddChildFrameSink(
        surface_id.frame_sink_id());
    window_android_->GetCompositor()->RemoveChildFrameSink(
        old_surface_id.frame_sink_id());
  }
  // Set the surface after frame sink hierarchy update.
  surface_layer_->SetSurfaceId(surface_id,
                               cc::DeadlinePolicy::UseDefaultDeadline());
}

void CobaltVideoOverlayWindow::SetJavaActivity(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& activity) {
  LOG(INFO)
      << "CobaltVideoOverlayWindow::SetJavaActivity called, storing reference";
  java_activity_ref_ = base::android::ScopedJavaGlobalRef<jobject>(activity);
  is_visible_ = true;
}

void CobaltVideoOverlayWindow::OnActivityDestroyed(JNIEnv* env) {
  LOG(INFO) << "CobaltVideoOverlayWindow::OnActivityDestroyed called, clearing "
               "reference";
  if (window_android_) {
    window_android_->RemoveObserver(this);
    window_android_ = nullptr;
  }
  java_activity_ref_.Reset();
  is_visible_ = false;

  if (controller_) {
    controller_->OnWindowDestroyed(false /* should_pause_video */);
  }
}

void CobaltVideoOverlayWindow::OnViewSizeChanged(JNIEnv* env,
                                                 int width,
                                                 int height) {
  LOG(INFO) << "CobaltVideoOverlayWindow::OnViewSizeChanged: " << width << "x"
            << height;
  bounds_ = gfx::Size(width, height);
  if (surface_layer_) {
    surface_layer_->SetBounds(bounds_);
  }
}

void CobaltVideoOverlayWindow::CompositorViewCreated(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& compositor_view) {
  LOG(INFO) << "CobaltVideoOverlayWindow::CompositorViewCreated called";

  compositor_view_ =
      thin_webview::android::CompositorView::FromJavaObject(compositor_view);
  DCHECK(compositor_view_);
  compositor_view_->SetRootLayer(surface_layer_);

  base::android::ScopedJavaLocalRef<jobject> j_window_android =
      Java_CobaltPictureInPictureActivity_getWindowAndroid(env,
                                                           java_activity_ref_);

  if (!j_window_android.is_null()) {
    window_android_ = ui::WindowAndroid::FromJavaWindowAndroid(
        base::android::JavaParamRef<jobject>(env, j_window_android.obj()));
    if (window_android_) {
      window_android_->AddObserver(this);
    }
  }
}

void CobaltVideoOverlayWindow::CreateJavaActivity() {
  LOG(INFO) << "CobaltVideoOverlayWindow::CreateJavaActivity called, launching "
               "Android Activity";
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
      env, j_web_contents, reinterpret_cast<jlong>(this));
}

}  // namespace cobalt
